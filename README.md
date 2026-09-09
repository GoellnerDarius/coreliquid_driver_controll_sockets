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

You rarely need to speak the protocol by hand, though: see
[Command line client](#command-line-client) below.

## Command line client

Talking to the socket by hand gets old quickly, so `coreliquidctl` wraps it in a
plain command. It is a POSIX shell script with no dependency beyond one of
`socat`, `nc` or `python3`, whichever your system already has.

### Install

```bash
sudo install -m 755 coreliquidctl /usr/local/bin/
```

The name is deliberately not `coreliquid`: that is the daemon binary, and the
two live side by side in `/usr/local/bin`.

Fish users can also install the completions, which offer the mode names, the
mode numbers after `mode`, and the protocol lines after `raw`:

```bash
install -m 644 completions/coreliquidctl.fish ~/.config/fish/completions/
# or, system wide:
sudo install -m 644 completions/coreliquidctl.fish /usr/share/fish/vendor_completions.d/
```

### Use

```bash
coreliquidctl            # status, the default with no argument
mode: game (2)
temp: 41 C

coreliquidctl game       # switch cooling mode
coreliquidctl silent
coreliquidctl ping
daemon alive
```

Every mode has a one-letter alias, taken from the first letter of its name:

| Command | Alias | Mode |
| --- | --- | --- |
| `status` | `st` | — |
| `silent` | `s` | 0 |
| `balance` | `b` | 1 |
| `game` | `g` | 2 |
| `default` | `d` | 4 |
| `smart` | `m`, `sm` | 5 |
| `ping` | `p` | — |

Note that `s` is **silent**, not smart and not status. Smart is `m`, and status
is `st` or simply no argument at all.

Two commands take an argument: `coreliquidctl mode `*n* sets a mode by number,
for the same values as **-M**, and `coreliquidctl raw `*line* sends one protocol
line unchanged, which is how you reach anything this script does not know about
yet.

The client exits non-zero when the daemon answers `ERR`, or when it cannot be
reached at all, so it composes in scripts:

```bash
coreliquidctl game || echo "the AIO did not take it" >&2
```

`CORELIQUID_SOCK` overrides the socket path, which is useful against a stub
daemon when you develop without the hardware.

### When it cannot connect

The socket is `root:coreliquid` mode 0660, so the two failures you will actually
meet are a stopped daemon and a missing group. `coreliquidctl` tells them apart
and says what to do, including the case that catches everyone once: you have
been added to the `coreliquid` group, but your session started before that, and
a running process never picks up groups granted after it started. Re-running
`usermod` does nothing there. Either start a subshell that has the group, with
`newgrp coreliquid`, or use `sudo`. Logging out and back in fixes it for good.
