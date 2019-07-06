#include "loader.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <uefi/uefi.h>
#include <kboot/kboot.h>
#include <elf64/elf64.h>

// this is a shellcode we will load in order to switch the address space
__attribute__((flatten))
static void virtual_load_stub(uint64_t cr3) {
    
}
__attribute__((flatten))
static void virtual_load_stub_end() {}

#define ALIGN_DOWN(n, a) (((uint64_t)n) & ~((a) - 1))
#define ALIGN_UP(n, a) ALIGN_DOWN(((uint64_t)n) + (a) - 1, (a))

#define ASSERT(cond, fmt, ...) \
    do { \
        if(!(cond)) { \
            printf(L"Assertion failed: "fmt"\n\r", ## __VA_ARGS__); \
            goto failed; \
        } \
    } while(0)

static void wide_to_ascii(const wchar_t* in, char* out) {
    while(*in) {
        *out++ = (char)*in;
        in++;
    }
    *out = 0;
}

void load_kernel(boot_entry_t* entry) {
    gST->ConOut->SetAttribute(gST->ConOut, EFI_TEXT_ATTR(EFI_WHITE, EFI_BLACK));
    gST->ConOut->ClearScreen(gST->ConOut);
    printf(L"Booting `%s` (%s)\n\r", entry->name, entry->filename);
    printf(L"Command line: `%s`\n\r", entry->command_line);
    char name_buffer[256];
 
    // get the root of the file system
    EFI_GUID sfpGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* sfp = 0;
    gBS->LocateProtocol(&sfpGuid, 0, (VOID**)&sfp);
    EFI_FILE_PROTOCOL* rootDir = 0;
	sfp->OpenVolume(sfp, &rootDir);

    // load the kernel
    EFI_FILE_PROTOCOL* file = OpenFile(rootDir, entry->filename, EFI_FILE_MODE_READ, 0);
    if(file == NULL) goto failed;

    Elf64_Ehdr header;
    size_t read_size = sizeof(Elf64_Ehdr);
    ReadFile(file, &read_size, &header);
    ASSERT(read_size == sizeof(Elf64_Ehdr), L"File too small");

    // verify elf
    ASSERT(header.e_ident[EI_MAG0] == ELFMAG0, L"Elf magic incorrect");
    ASSERT(header.e_ident[EI_MAG1] == ELFMAG1, L"Elf magic incorrect");
    ASSERT(header.e_ident[EI_MAG2] == ELFMAG2, L"Elf magic incorrect");
    ASSERT(header.e_ident[EI_MAG3] == ELFMAG3, L"Elf magic incorrect");
    ASSERT(header.e_ident[EI_CLASS] == ELFCLASS64, L"Elf class incorrect (only elf64 is supported)");
    ASSERT(header.e_ident[EI_DATA] == ELFDATA2LSB, L"Elf data incorrect (only little endian is supported)");
    ASSERT(header.e_type == ET_EXEC, L"Elf type incorrect (only executable is supported)");
    ASSERT(header.e_machine == EM_X86_64, L"Elf machine incorrect (only x86_64 is supported)");

    // make sure we have the sections we need
    ASSERT(header.e_shoff, "Elf missing section table");
    ASSERT(header.e_shstrndx, "Elf missing the str table index");
    
    // get the string table
    Elf64_Shdr strtab;
    read_size = sizeof(Elf64_Shdr);
    SetPosition(file, header.e_shoff + header.e_shentsize * header.e_shstrndx);   
    ReadFile(file, &read_size, &strtab);
    ASSERT(read_size == sizeof(Elf64_Ehdr), L"File too small");

    // find the .kboot.header section
    Elf64_Shdr section_header;
    bool found = false;
    for(int i = 0; i < header.e_shnum; i++) {
        read_size = sizeof(Elf64_Shdr);
        SetPosition(file, header.e_shoff + header.e_shentsize * i);   
        ReadFile(file, &read_size, &section_header);
        
        read_size = 256;
        SetPosition(file, strtab.sh_offset + section_header.sh_name);
        ReadFile(file, &read_size, &name_buffer);
        
        if(strcmp(name_buffer, ".kboot.header") == 0) {
            found = true;
            break;
        }
    }
    ASSERT(found, L"Could not find the `.kboot.header` section");

    // read the header
    kboot_header_t kheader = {0};
    read_size = sizeof(kboot_header_t);
    SetPosition(file, section_header.sh_offset);
    ReadFile(file, &read_size, &kheader);
    ASSERT(read_size == sizeof(kboot_header_t), L"File too small");

    // print the info for debugging
    printf(L"kboot header:\n\r");
    printf(L"  mapping: \n\r");
    printf(L"    type: %s\n\r", kheader.mapping.type == KBOOT_MAPPING_VIRTUAL ? L"virtual" : L"identity");
    if(kheader.mapping.type == KBOOT_MAPPING_VIRTUAL) {
       printf(L"    direct mapping base: 0x%016llp\n\r", kheader.mapping.direct_mapping_base);
    }

    if(kheader.mapping.type == KBOOT_MAPPING_VIRTUAL) {
        ASSERT(false, L"Virtual mapping not supported yet\n\r");
    }else {
        printf(L"Loading using identity maps\n\r");
    }

    // we are going to put this in the actual 
    // physical address as specified
    Elf64_Phdr pheader;
    for(int i = 0; i < header.e_phoff; i++) {
        read_size = sizeof(Elf64_Phdr);            
        SetPosition(file, header.e_phoff + header.e_phentsize * i);   
        ReadFile(file, &read_size, &pheader);            
        ASSERT(read_size == sizeof(Elf64_Phdr), L"File too small");

        // start the loading
        switch(pheader.p_type) {
            case PT_LOAD: {
                // allocate as the EfiRuntimeServicesCode/EfiRuntimeServicesData
                uintptr_t addr = (uintptr_t)pheader.p_paddr;
                if((pheader.p_flags & PF_X) != 0) {
                    ASSERT(
                        gBS->AllocatePages(
                            AllocateAddress, 
                            EfiRuntimeServicesCode, 
                            ALIGN_UP(pheader.p_memsz, 4096) / 4096, 
                            &addr) == EFI_SUCCESS,
                        L"Failed to set pages as EfiRuntimeServicesCode");
                }else {
                    ASSERT(
                        gBS->AllocatePages(
                            AllocateAddress, 
                            EfiRuntimeServicesData, 
                            ALIGN_UP(pheader.p_memsz, 4096) / 4096, 
                            &addr) == EFI_SUCCESS,
                        L"Failed to set pages as EfiRuntimeServicesData");
                }

                // read the data
                read_size = pheader.p_filesz;
                SetPosition(file, pheader.p_offset);
                ReadFile(file, &read_size, (void*)pheader.p_paddr);
                ASSERT(read_size == pheader.p_filesz, L"Could not read everything");

                // zero out the rest
                memset((void*)pheader.p_paddr + read_size, 0, pheader.p_memsz - read_size);
            } break;
        }
    }

    // file data
    CloseFile(file);

    // allocate boot info
    kboot_info_t* addr;
    gBS->AllocatePages(AllocateAnyPages, EfiBootServicesData, ALIGN_UP(sizeof(kboot_info_t), 4096), (uintptr_t*)&addr);
    
    // prepare the boot info
    addr->cmdline.length = wcslen(entry->command_line);
    wide_to_ascii(entry->command_line, addr->cmdline.cmdline);

    // set graphics mode
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = 0;
	EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	gBS->LocateProtocol(&gopGuid, 0, (VOID**)&gop);
    ASSERT(gop != NULL, L"Failed to locate Graphics Output Protocol");
    UINT32 width = 1920;
	UINT32 height = 1080;
    EFI_GRAPHICS_PIXEL_FORMAT pixel_format = PixelRedGreenBlueReserved8BitPerColor;
    UINT32 gopModeIndex = GetGraphicsMode(gop, &width, &height, &pixel_format);
    gop->SetMode(gop, gopModeIndex);

    // set the entries
    addr->framebuffer.width = width;
    addr->framebuffer.height = height;
    addr->framebuffer.framebuffer_addr = gop->Mode->FrameBufferBase;

    // allocate memory for the efi mmap
    size_t mapSize;
    size_t mapKey;
    size_t descSize;
    uint32_t descVersion;
    gBS->GetMemoryMap(&mapSize, NULL, &mapKey, &descSize, &descVersion);
    mapSize += 64 * descSize; // take into account some changes after exiting boot services
    EFI_MEMORY_DESCRIPTOR* descs;
    gBS->AllocatePages(AllocateAnyPages, EfiBootServicesData, ALIGN_UP(mapSize, 4096) / 4096, (uintptr_t*)&descs);
    addr->mmap.descriptors = descs;

    // jump to the kernel
    if(kheader.mapping.type == KBOOT_MAPPING_VIRTUAL) {
        // for virtual addressing we need to add some shellcode 
        // before doing anything
        ASSERT(false, L"Virtual mapping not supported yet\n\r");
    }else {
        // for identity mapped we should be able to just exit boot services and call it
        kboot_entry_function func = (kboot_entry_function)header.e_entry;

        printf(L"Bai Bai\n\r");
        gBS->ExitBootServices(gImageHandle, mapKey);

        // disable interrupts
        asm volatile("cli");
        asm volatile("lidt (0)");

        // finish the table by getting the memory map
        // NOTE: This is completely valid according to the
        //       the spec
        gBS->GetMemoryMap(&mapSize, descs, &mapKey, &descSize, &descVersion);
        addr->mmap.descriptor_size = descSize;
        addr->mmap.counts = mapSize / descSize;

        func(0xCAFEBABE, addr);
    }

failed:
    // TODO: press any key to shutdown
    while(1);
}
