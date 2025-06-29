Scriba - Thingino Programming App
=================================

Scriba is a version of SNANDer that doesn't include any code that isn't used in
Linux. It has been reorganized and streamlined to serve one sole purpose:
to work with flash chips in IP cameras that are used with the Thingino project.

Scriba works well with the first generation of CH341A programmers that have been
modified to work at 3.3 volts. You can find more information about the necessary
change at https://github.com/themactep/thingino-firmware/wiki/CH341A-Programmer.

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

The udev rule `40-persistent-ch341a.rules` grants access to CH341A USB devices.

Usage
-----

```
scriba [options]

General:
  -h             Display help
  -L             List supported chips
  -i             Read chip ID

Automation:
  -R <file>      Read chip (read twice and compare)
  -W <file>      Write chip (erase + write + verify)

Operations:
  -r <file>      Read chip to file
  -w <file>      Write file to chip
  -v             Verify after write
  -e             Erase chip
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


Authors
-------

Original code by [McMCC](https://github.com/McMCCRU/SNANDer),
modified by [Droid-MAX](https://github.com/Droid-MAX/),
modified by [Paul Philippov](https://github.com/themactep).
