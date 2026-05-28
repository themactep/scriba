# About the project

Scriba - a universal programming firmware for NOR and NAND flash chips.

# Source files structure

- Keep compilable files in src/
- Keep business logic separate from user interface: src/ui/
- Wire for multiple interfaces:
    src/ui/cli/
    src/ui/gtk/
    src/ui/tk/
    src/ui/web/
- Keep udev rules, icons, configs and supporting files in resources/ directory.
- Keep documentation in docs/, download datasheets for each programmer and save
  in docs/, refer to them as the source of truth when writing programmer specific code.

# Multiple programmers support

- Abstract common programming logic via HAL:
    src/hal/
  Common operations: initialize, reset, erase, read, write.
- Keep programmer specific code in separate files per programmer vendor/model:
    src/hal/ch341a/
    src/hal/ezp/2019/
    src/hal/ezp/2023/
    src/hal/xgecu/t48/
    src/hal/xgecu/t76/
    ...

# Business logic:

- check presence of the programmer
- check presence of the flash chip
- check the chip is known and supported
- check the app can write to a temporary file
- if arguments have a file to flash:
  - check the file presence
  - check the file readable
  - check filesize is not larger than the chip size
- initialize the programmer

# Command line arguments:
  -h) show help
  -i) display chip info
  -e) erase the chip
  -w) write provided file to the chip
  -r) read the chip to a given filename
  -W) macro operation:
    - erase flash chip
    - write content of the provided file to the flash
    - read flash content to a temp file
    - compare the temp file to the original file
      if match - erase the temp file
      if not - raise an error and leave the temp file for inspection
  -R) macro operation:
    - read flash content to a temp file
    - read flash content to another temp file
    - compare both temp files
      if match - move one temp file to a target file and delete the other temp file
      if not - raise an error and leave both temp files for inspection
