# TomatBoot for UEFI

TomatBoot is a simple kernel loader for 64bit UEFI based systems.

The idea of TomatBoot is to provide a simple interface for kernel developers to load their kernel into a 64bit environment 
(unlike most bootloaders which load you to 32bit). The reason for using a loader and not implementing UEFI directly is so
you won't have to mix UEFI and your kernel code.

![Main Menu](screenshots/mainmenu.png)

![Setup Menu](screenshots/setup.png)

![Boot Menu](screenshots/bootmenu.png)

## Features

* Boot menu
	* change width and height
	* change default entry and delay 
	* modify the command line at boot
* Support for static ELF64 kernels
	* the kernel entry must be sysv abi
* Passing boot information to the kernel
	* Command line
	* Framebuffer
	* ACPI table 
	* Memory Map (to be changed)
	* TSC frequency (ticks per second)
	* Boot modules (additional files to load) (not implemented yet)
	* Boot Device Path (?) (not implemented yet)

## Boot Protocol
### TomatBoot
Currently this is the only supported boot protocol, the protocol is very simple, give a 64bit elf binary, we load it 
as a static binary, and call it with two parameters, one being a magic, and the other being the boot info struct.

For the header file with more definitions you can [click here](lib/tboot/tboot.h). 

## How to

### Building
Being a loader it needs to be used in conjunction with another loader. For a project using TomatBoot you can see 
[TomatOS](https://github.com/TomatOrg/TomatOS).

Before anything we need to actually build the bootloader. To do so simply run the following:
```shell script
git clone git@github.com:TomatOrg/TomatBoot-UEFI.git
cd TomatBoot-UEFI
make
```

This will make the efi executable which will be found under `bin/BOOTX64.EFI`. in the final image you would want to 
place it under `EFI/BOOT/BOOTX64.EFI`. Of course you will need to make sure the image is GPT partitioned and has a 
EFI partition (in that partition you want to place the binary).

Other than the binary, you will also need to provide a configuration file. For an example you can see the 
[example config](config/test-tomatboot.cfg). The config file needs to be placed at the root of the efi partition 
with the name `tomatboot.cfg`

### Config format
The configuration format is straight forward, it is a list of entries where each entry has the format:
```
:<name>
PATH=<path to elf executeable>
CMDLINE=<optional command line options>
```

## UEFI Library

The uefi library consists mainly of headers and source files taken directly from [EDK2](https://github.com/tianocore/edk2). The reason for that is 
to cut on development time and use existing headers, but not using EDK2 build system.

The reason that I am not just using a submodle of edk2 is for two reasons:
* EDK2 is HUGE and I don't want the builder to have to clone that all
* I make some changes to make it actually work nicely without EDK2 build system

The license of EDK2 can be found [here](lib/uefi/License.txt) and is redistributed under that license. Note that the rest of the code is
distributed as the main license file shows.
