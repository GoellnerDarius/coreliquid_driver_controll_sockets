// Compilation: 
// gcc my_msi_driver.c -lhidapi-hidraw -lsensors -o /where/you/want/my_msi_driver

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <hidapi/hidapi.h>
#include <sensors/sensors.h>

// Where the daemon listens for control commands
#define CONTROL_DIR   "/run/coreliquid"
#define CONTROL_SOCK  CONTROL_DIR "/control.sock"
// Members of this group may talk to the daemon. If it doesn't exist on the
// system, the socket stays root-only.
#define CONTROL_GROUP "coreliquid"

// Fan modes, unused
typedef enum FanMode {
    SILENT = 0,
    BALANCE = 1,
    GAME = 2,
    CUSTOMIZE = 3,
    DEFAULT = 4,
    SMART = 5
} FanMode;

// Flag to stop the daemon
int stop = 0;

// State reported to control clients
int current_mode = SMART;
int current_temp = 0;

void set_fan_mode(hid_device *handle, int fan_mode);

/**
 * Current value of the monotonic clock, in milliseconds.
 */
long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/**
 * Create the control socket the user interface connects to.
 *
 * \return the listening socket, or -1 if it could not be created
 */
int control_open(void)
{
    int fd;
    struct sockaddr_un addr;
    struct group *grp;

    if ((mkdir(CONTROL_DIR, 0750) != 0) && (errno != EEXIST)) {
        fprintf(stderr, "Cannot create %s: %s\n", CONTROL_DIR, strerror(errno));
        return -1;
    }
    // A socket left behind by a previous run would make bind() fail
    unlink(CONTROL_SOCK);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "Cannot create control socket: %s\n", strerror(errno));
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CONTROL_SOCK, sizeof(addr.sun_path) - 1);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "Cannot bind %s: %s\n", CONTROL_SOCK, strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, 4) != 0) {
        fprintf(stderr, "Cannot listen on %s: %s\n", CONTROL_SOCK, strerror(errno));
        close(fd);
        unlink(CONTROL_SOCK);
        return -1;
    }

    // Hand the socket over to the control group, so that the interface doesn't
    // have to run as root
    grp = getgrnam(CONTROL_GROUP);
    if (grp != NULL) {
        chown(CONTROL_DIR, 0, grp->gr_gid);
        chown(CONTROL_SOCK, 0, grp->gr_gid);
    }
    else
        fprintf(stderr, "No %s group, control socket restricted to root\n", CONTROL_GROUP);
    chmod(CONTROL_DIR, 0750);
    chmod(CONTROL_SOCK, 0660);

    return fd;
}

/**
 * Execute one control command and build the answer sent back to the client.
 *
 * \param handle handle on the AIO device
 * \param cmd the command line received from the client
 * \param answer buffer the answer is written to
 * \param answer_size size of the answer buffer
 */
void control_execute(hid_device *handle, const char *cmd, char *answer, size_t answer_size)
{
    int mode;

    if (sscanf(cmd, "MODE %d", &mode) == 1) {
        if ((mode < 0) || (mode > 5) || (mode == CUSTOMIZE)) {
            snprintf(answer, answer_size, "ERR unsupported mode %d\n", mode);
            return;
        }
        set_fan_mode(handle, mode);
        current_mode = mode;
        snprintf(answer, answer_size, "OK\n");
    }
    else if (!strcmp(cmd, "STATUS"))
        snprintf(answer, answer_size, "OK mode=%d temp=%d\n", current_mode, current_temp);
    else if (!strcmp(cmd, "PING"))
        snprintf(answer, answer_size, "OK\n");
    else
        snprintf(answer, answer_size, "ERR unknown command\n");
}

/**
 * Accept one client, answer its command, and hang up.
 *
 * \param listen_fd the listening control socket
 * \param handle handle on the AIO device
 */
void control_serve(int listen_fd, hid_device *handle)
{
    int fd;
    ssize_t n;
    char cmd[256], answer[256];
    struct timeval tv;

    fd = accept(listen_fd, NULL, NULL);
    if (fd < 0)
        return;

    // A client that connects and says nothing must not stall the temperature loop
    tv.tv_sec = 0;
    tv.tv_usec = 200*1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    n = recv(fd, cmd, sizeof(cmd) - 1, 0);
    if (n > 0) {
        cmd[n] = '\0';
        cmd[strcspn(cmd, "\r\n")] = '\0';
        control_execute(handle, cmd, answer, sizeof(answer));
        send(fd, answer, strlen(answer), 0);
    }
    close(fd);
}

/**
 * Serve control clients for at most timeout_ms milliseconds. This replaces the
 * plain sleep the temperature loop used to do between two reads.
 *
 * \param listen_fd the listening control socket, or -1 if there is none
 * \param handle handle on the AIO device
 * \param timeout_ms how long to wait before returning
 */
void control_poll(int listen_fd, hid_device *handle, int timeout_ms)
{
    struct pollfd pfd;
    long deadline = now_ms() + timeout_ms;
    long remaining;
    int ret;

    while (!stop) {
        remaining = deadline - now_ms();
        if (remaining <= 0)
            break;
        if (listen_fd < 0) {
            // No control socket, just wait out the rest of the period
            usleep(remaining * 1000);
            break;
        }
        pfd.fd = listen_fd;
        pfd.events = POLLIN;
        ret = poll(&pfd, 1, (int)remaining);
        if (ret > 0)
            control_serve(listen_fd, handle);
        else if (ret == 0)
            break;
        else if (errno != EINTR) {
            // Don't spin on a broken poll
            fprintf(stderr, "poll failed: %s\n", strerror(errno));
            usleep(remaining * 1000);
            break;
        }
        // EINTR: the stop flag is checked on the next turn
    }
}

/**
 * Monitor the CPU temperature and send it to the AIO.
 *
 * \param handle handle on the AIO device
 * \param listen_fd the listening control socket, or -1 if there is none
 */
void monitor_cpu_temperature(hid_device *handle, int listen_fd)
{
    int nr, ret;
    unsigned char buf[65];
    const sensors_chip_name *chip;
    const sensors_feature *feature;
    const sensors_subfeature *subfeature;
    int ifreq = 3000, itemp;
    double temp;
    
    // Initialize the libsensor library
    ret = sensors_init(NULL);
    if (ret != 0) {
        fprintf(stderr, "Error while initializing libsensor: %d\n", ret);
        return;
    }
    
    memset(buf,0,sizeof(buf));
    buf[0] = 0xD0;
    buf[1] = 0x85;
    // The AIO doesn't care about CPU frequency to adapt fan speed. Set it to a dummy value.
    buf[2] = ifreq & 0xFF;
    buf[3] = (ifreq >> 8) & 0xFF;
    // Loop on chips
    nr = 0;
    while (!stop && ((chip = sensors_get_detected_chips(NULL, &nr)) != NULL)) {
        if (!strcmp(chip->prefix, "coretemp") || !strcmp(chip->prefix, "k10temp") || !strcmp(chip->prefix, "k10temp") || !strcmp(chip->prefix, "k10temp")) { // This chip gives CPU temperatures
            // Loop on features for this chip
            int nf = 0;
            while (!stop && ((feature = sensors_get_features(chip, &nf)) != NULL)) {
                if (feature->type == SENSORS_FEATURE_TEMP) {
                    if (!strcmp(feature->name, "temp1")) { // This feature is the global core CPU temperature
                        // Loop on subfeatures for this chip feature
                        int ns = 0;
                        while (!stop && ((subfeature = sensors_get_all_subfeatures(chip, feature, &ns)) != NULL)) {
                            if (subfeature->type == SENSORS_SUBFEATURE_TEMP_INPUT) {
                                // Temperature subfeature found, initialize the hidapi library
                                hid_init();
                                // Listen to temperature in an infinite loop
                                while (!stop) {
                                    ret = sensors_get_value(chip, subfeature->number, &temp);
                                    if (ret == 0) {
                                        itemp = (int)temp;
                                        current_temp = itemp;
                                        // Set CPU status (cmd 0x85)
                                        buf[4] = itemp & 0xFF;
                                        buf[5] = (itemp >> 8) & 0xFF;
                                        hid_write(handle, buf, 65);
                                    }
                                    // Wait 2s, serving control clients meanwhile
                                    control_poll(listen_fd, handle, 2000);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Free resources
    sensors_cleanup();
}


/**
 * Set the fan mode.
 * 
 * \param handle handle on the AIO device
 * \param fan_mode the choosen fan mode
 */
void set_fan_mode(hid_device *handle, int fan_mode) 
{
    unsigned char buf[65];    

    memset(buf,0,sizeof(buf));
    buf[0] = 0xD0;
    buf[1] = 0x40;
    buf[2] = fan_mode;
    buf[10] = fan_mode;
    buf[18] = fan_mode;
    buf[26] = fan_mode;
    buf[34] = fan_mode;
    hid_write(handle, buf, 65);
    buf[1] = 0x41;
    hid_write(handle, buf, 65);  
}

/**
 * Signal handler to stop the daemon.
 * Can take up to 2s to stop (sleeping time between temperature reads).
 */
void stopit(int dummy)
{
    stop = 1;
}

/**
 * Main program
 */
int main(int argc, char *argv[]) 
{
    int i, fan_mode = SMART;
    int start_daemon = 0;
    int listen_fd = -1;
    hid_device *handle = NULL;

    // Check options
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-M")) {
            fan_mode = atoi(argv[++i]);
            if ((fan_mode < 0) || (fan_mode > 5) || (fan_mode == 3)) {
                printf("Allowed modes:\n");
                printf("0 : silent\n");
                printf("1 : balance\n"),
                printf("2 : game\n");
                printf("4 : default (constant)\n");
                printf("5 : smart\n");
                exit(0);
            }
        }
        else if (!strcmp(argv[i], "startd"))
            start_daemon = 1;
    }
    
    // Initialize the hidapi library
    hid_init();
    // Open the device using the VID, PID
    handle = hid_open(0x0db0, 0x6a05, NULL);
    if (handle == NULL) {
        fprintf(stderr, "Cannot open the AIO device. Is it plugged in, and do you have the rights?\n");
        hid_exit();
        exit(1);
    }
    set_fan_mode(handle, fan_mode);
    current_mode = fan_mode;
    // Start daemon if requested
    if (start_daemon) {
        signal(SIGTERM, stopit);
        // A client hanging up mid-answer must not kill the daemon
        signal(SIGPIPE, SIG_IGN);
        listen_fd = control_open();
        monitor_cpu_temperature(handle, listen_fd);
        if (listen_fd >= 0) {
            close(listen_fd);
            unlink(CONTROL_SOCK);
        }
    }
    // Close the device
    hid_close(handle);
    // Finalize the hidapi library
    hid_exit();
    
    exit(0);
}
