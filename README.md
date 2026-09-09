# coreliquid_driver


A simple linux driver for MSI MEG Coreliquid S360 AIO watercooling

## About this fork
This is a fork of [sarzeaud/coreliquid_driver](https://github.com/sarzeaud/coreliquid_driver).

Upstream, the cooling mode is fixed when the program starts: changing it means
restarting the daemon. This fork adds a **control socket**, a Unix socket the
running daemon listens on, so that other programs can interface with it — read
the current mode and CPU temperature, or switch the cooling mode on the fly,
without a restart. See [Control socket](#control-socket) below for the protocol.

Everything else behaves as upstream; the rest of this README describes the
driver as a whole.

## Why
Having unsuccessfully tried to use liquidctl (https://github.com/liquidctl/liquidctl) 
to drive my MSI MEG coreliquid AIO watercooling under linux (specifically Debian 12), 
I decided to build mine. I don't care about the display on this AIO, I just want it 
to react to CPU temperature. I only allow 5 predefined modes to make the link between 
temperature and fan and pump speed:
    SILENT = 0,
    BALANCE = 1,
    GAME = 2,
    DEFAULT = 4,
    SMART = 5.
In this first version, the CUSTOMIZE = 3 mode is not allowed, as the 5 others
should do the job. Here are pictures from the MSI soft for Windows, showing the link
between temperature and fan speeds for three modes.

![SILENT mode](pictures/silent.png)

![BALANCE mode](pictures/balance.png)

![GAME mode](pictures/game.png)

## Compilation
You need to install libsensors-dev and libhidapi-dev to compile. Then, it is as simple as:

```bash
gcc my_msi_driver.c -lhidapi-hidraw -lsensors -o /where/you/want/my_msi_driver
```

Here, I use libhidapi-hidraw, but I guess it would work as well with libhidapi-libusb0.
I choose the primer because it seems to be the recommanded one these days.

## Usage
my_msi_driver **-M** *mode* [ **startd** ]

**-M** sets the cooling mode to *mode*.

**startd** starts the driver daemon. This should rather be done through systemctl (see below).

## Daemon
If you want this program to run as a daemon on your system, just:

```bash
sudo cp my_msi_driver.service /etc/systemd/system/
```

In this file, adapt the ExecStart path for where you put the executable, and choose
you cooling mode. Then run:

```bash
sudo systemctl daemon-reload
sudo systemctl enable my_msi_driver.service
```

The driver will start as a daemon on next boot. If you want it to run at once:

```bash
sudo systemctl start my_msi_driver
```

## Control socket
While running as a daemon, the driver listens on a Unix socket at
`/run/coreliquid/control.sock`, so that the cooling mode can be changed without
restarting it. The socket belongs to the `coreliquid` group and is only readable
by root if that group doesn't exist, so create it and add yourself to it:

```bash
sudo groupadd -f coreliquid
sudo usermod -aG coreliquid $USER
```

You need to log out and back in for the new group to apply.

The protocol is one line of text per connection, and the daemon answers with a
single line starting with either `OK` or `ERR`:

| Command | Answer | Effect |
| --- | --- | --- |
| `MODE `*n* | `OK` | Sets the cooling mode, same values as **-M** |
| `STATUS` | `OK mode=5 temp=42` | Current mode and last CPU temperature read |
| `PING` | `OK` | Checks that the daemon is alive |

For instance:

```bash
echo MODE 0 | nc -U /run/coreliquid/control.sock
```

The daemon serves control clients between two temperature readings, so a command
can take up to 2s to be answered.
