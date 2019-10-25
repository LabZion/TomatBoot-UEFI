#include <Uefi.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/DebugLib.h>
#include <util/gop_utils.h>
#include <Library/BaseLib.h>

#include "config.h"

boot_entries_t boot_entries;

CHAR8* read_line(EFI_FILE_PROTOCOL* file) {
    CHAR8* path = NULL;
    ASSERT_EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, 255, (VOID**)&path));
    UINTN length = 0;

    // max length of 255 for simplicity
    while(length < 255) {
        // read 1 byte
        UINTN one = 1;
        ASSERT_EFI_ERROR(file->Read(file, &one, &path[length]));

        // got to new line or eof
        if(one == 0 || path[length] == '\n') {
            // if was end of line and nothing was read, just return null to indicate nothing to read
            if(one == 0 && length == 0) {
                ASSERT_EFI_ERROR(gBS->FreePool(path));
                return NULL;
            }

            path[length] = 0;
            break;
        }

        // read next character
        length++;
    }

    ASSERT(length < 255);

    return path;
}

void get_boot_entries(boot_entries_t* entries) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* filesystem = NULL;
    ASSERT_EFI_ERROR(gBS->LocateProtocol(&gEfiSimpleFileSystemProtocolGuid, NULL, (VOID**)&filesystem));

    EFI_FILE_PROTOCOL* root = NULL;
    ASSERT_EFI_ERROR(filesystem->OpenVolume(filesystem, &root));

    EFI_FILE_PROTOCOL* file = NULL;
    ASSERT_EFI_ERROR(root->Open(root, &file, L"tomatboot.cfg", EFI_FILE_MODE_READ, 0));

    // first count how many entries there are
    ASSERT_EFI_ERROR(file->SetPosition(file, 0));
    UINTN entryCount = 0;
    while(TRUE) {
        CHAR8* line = read_line(file);
        if(line == NULL) break;

        if(line[0] == ':') {
            entryCount++;
        }

        ASSERT_EFI_ERROR(gBS->FreePool(line));
    }


    // allocate entries
    entries->count = entryCount;
    ASSERT_EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, sizeof(boot_entry_t) * entryCount, (VOID**)&entries->entries));

    // now do the actual processing of everything
    ASSERT_EFI_ERROR(file->SetPosition(file, 0));
    INTN index = -1;
    while(TRUE) {
        CHAR8* line = read_line(file);
        if(line == NULL) break;

        if(line[0] == ':') {
            // got new entry (this is the name)
            index++;
            entries->entries[index].name = line + 1;
            entries->entries[index].path = NULL;
            entries->entries[index].cmd = NULL;

        }else {
            // this is just a parameter
            ASSERT(index != -1);

            // path of the bootable (only one)
            if(AsciiStrnCmp(line, "PATH=", 5) == 0) {
                entries->entries[index].path = line + 5;

            // command line arguments (only one)
            }else if(AsciiStrnCmp(line, "CMDLINE=", 8) == 0) {
                entries->entries[index].cmd = line + 8;

            // module path (can have multiple)
            }else if(AsciiStrnCmp(line, "MODULE=", 7) == 0) {
                // TODO: Add module

            // if nothing just free the line
            }else {
                ASSERT_EFI_ERROR(gBS->FreePool(line));
            }
        }
    }
}

void load_boot_config(boot_config_t* config) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* filesystem = NULL;
    ASSERT_EFI_ERROR(gBS->LocateProtocol(&gEfiSimpleFileSystemProtocolGuid, NULL, (VOID**)&filesystem));

    EFI_FILE_PROTOCOL* root = NULL;
    ASSERT_EFI_ERROR(filesystem->OpenVolume(filesystem, &root));

    EFI_FILE_PROTOCOL* configFile = NULL;
    EFI_STATUS status = root->Open(root, &configFile, L"tomatboot.bin", EFI_FILE_MODE_READ, 0);

    // if not found just set default config
    if(status == EFI_NOT_FOUND) {
        config->boot_delay = 4;
        config->default_os = 0;
        config->gfx_mode = get_prev_mode(0); // get the highest resolution possible

    // otherwise see if can read the config
    }else {
        ASSERT_EFI_ERROR(status);

        UINTN size = sizeof(boot_config_t);
        ASSERT_EFI_ERROR(configFile->Read(configFile, &size, config));
        ASSERT_EFI_ERROR(configFile->Close(configFile));
    }

    // verify config and change if needed
    if(config->boot_delay > 30 || config->boot_delay < 1) {
        config->boot_delay = 30;
    }

    // TODO: Verify graphics config?
    // TODO: Verify default os config

    // close everything
    ASSERT_EFI_ERROR(root->Close(root));
}

void save_boot_config(boot_config_t* config) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* filesystem = NULL;
    ASSERT_EFI_ERROR(gBS->LocateProtocol(&gEfiSimpleFileSystemProtocolGuid, NULL, (VOID**)&filesystem));

    EFI_FILE_PROTOCOL* root = NULL;
    ASSERT_EFI_ERROR(filesystem->OpenVolume(filesystem, &root));

    EFI_FILE_PROTOCOL* configFile = NULL;
    ASSERT_EFI_ERROR(root->Open(root, &configFile, L"tomatboot.bin", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0));

    UINTN size = sizeof(boot_config_t);
    ASSERT_EFI_ERROR(configFile->Write(configFile, &size, config));

    // close everything
    ASSERT_EFI_ERROR(configFile->Flush(configFile));
    ASSERT_EFI_ERROR(configFile->Close(configFile));
    ASSERT_EFI_ERROR(root->Close(root));
}