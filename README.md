Scriba - Thingino Programming App
=================================

Scriba is a version of SNANDer that doesn't include any code that isn't used in
Linux. It has been reorganized and streamlined to serve one sole purpose:
to work with flash chips in IP cameras that are used with the Thingino project.

### Supported Programmers

Scriba supports two programmer types and auto-detects the connected one:

| Programmer | VID:PID | Notes |
|---|---|---|
| **CH341A** | 1a86:5512 | First generation only, must be modified for 3.3 V |
| **EZP2019** | 1fc8:310b | Also supports EZP2019+ (1fc8:310c) and EZP2023 (1fc8:310d) |

For CH341A voltage modification details, see
https://github.com/themactep/thingino-firmware/wiki/CH341A-Programmer.

You can force a specific programmer with `-P`:
```
scriba -P ch341a -i
scriba -P ezp2019 -i
```

Building
--------

### Prerequisites

- GCC compiler
- libusb-1.0 development files
- make

### Build Options

# Standard build (dynamically linked)
```
make
```

### Static build (includes libusb)
```
make static
```

### Install
```
sudo make install
```

Configuration
-------------

### USB Permissions

Scriba installs two udev rules automatically via `make install`:

- `40-persistent-ch341a.rules` — grants access to CH341A USB devices
- `40-persistent-ezp2019.rules` — grants access to EZP2019/EZP2019+/EZP2023 devices

`make install` also reloads udev rules. If the programmer is already connected,
unplug and replug it after installation for the rules to take effect.

Usage
-----

```
scriba [options]

Automation:
  -R <file>      Read chip (read twice and compare)
  -W <file>      Write chip (erase + write + verify)

Single operations:
  -i             Read chip ID
  -e             Erase chip
  -r <file>      Read chip to file
  -w <file>      Write file to chip
  -v             Verify after write

Granularity:
  -a <address>   Set address
  -l <bytes>     Set length

SPI NAND:
  -d             Disable internal ECC
  -o <bytes>     Set OOB size
  -I             Ignore ECC errors
  -k             Skip BAD pages

EEPROM:
  -E <chip>      Select EEPROM type
  -8             Set 8-bit organization
  -f <bits>      Set address size
  -s <bytes>     Set page size

General:
  -h             Display help
  -L             List supported chips
  -P <prog>      Programmer type: ch341a, ezp2019, auto (default: auto)
  --debug        Enable debug messages for USB communication
  --trace        Dump SPI commands and data (implies --debug)
```

Examples
--------

### Get flash info
```
scriba -i
```

### Read and save flash
```
scriba -r output.bin
```

### Write and verify
```
scriba -w data.bin -v
```

### Write chip (automatic erase + write + verify)
```
scriba -W firmware.bin
```

### Read chip (read twice and compare)
```
scriba -R verified_backup.bin
```

### EEPROM operations
```
scriba -E 93c46 -r eeprom.bin
```

### Force programmer type
```
scriba -P ezp2019 -i
scriba -P ch341a -r backup.bin
```

### Debug USB communication
```
scriba --debug -i
scriba --trace -r dump.bin
```


Authors
-------

Original code by [McMCC](https://github.com/McMCCRU/SNANDer),
modified by [Droid-MAX](https://github.com/Droid-MAX/),
modified by [Paul Philippov](https://github.com/themactep).
