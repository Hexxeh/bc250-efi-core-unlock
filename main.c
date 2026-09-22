#include <efi.h>
#include "smu.h"
#include "unlock.h"
#include "patches.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

// Set to 1 to halt the system after successful warm reboot validation instead of booting OS
#define TEST_MODE 0

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    // Read mask register
    unsigned int before = smn_rd(MASK_REG);
    unsigned char mask = before & 0xFF;

#if TEST_MODE
    print(SystemTable, L"BC-250 Core Unlocker EFI Utility\r\n");
    print(SystemTable, L"Current Core Mask: ");
    print_hex(SystemTable, mask);
    print(SystemTable, L"\r\n");
#endif

    if (mask == 0xFF) {
        // Cores active in hardware. Unlock SMU & apply firmware patches before OS boot.
        EFI_STATUS status = unlock_smu(SystemTable);
        if (EFI_ERROR(status)) {
            print(SystemTable, L"Error: Failed to unlock SMU secure access!\r\n");
            SystemTable->BootServices->Stall(5000000);
            return status;
        }

        status = apply_smu_patches(SystemTable);
        if (EFI_ERROR(status)) {
            print(SystemTable, L"Error: Failed to apply SMU firmware patches!\r\n");
            SystemTable->BootServices->Stall(5000000);
            return status;
        }

#if TEST_MODE
        print(SystemTable, L"SMU unlocked and firmware patched! Verification successful! Halting...\r\n");
        while (1) {
            __asm__ __volatile__("hlt");
        }
#endif
        // Cores unlocked and SMU patched! Return EFI_SUCCESS to allow UEFI Boot Manager to proceed natively
        return EFI_SUCCESS;
    } else {
        // Cores not yet active (mask != 0xFF). Unlock SMU to allow SMN write, set mask to 0xFF, and warm reboot.
#if TEST_MODE
        print(SystemTable, L"Unlocking SMU to set core mask...\r\n");
#endif
        EFI_STATUS status = unlock_smu(SystemTable);
        if (EFI_ERROR(status)) {
            print(SystemTable, L"Error: Failed to unlock SMU secure access!\r\n");
            SystemTable->BootServices->Stall(5000000);
            return status;
        }

#if TEST_MODE
        print(SystemTable, L"Writing core mask 0xFF...\r\n");
#endif
        int st = smn_write32(SystemTable, SMN_CORE_MASK_ADDR, 0xFF);
        if (st != 0x01) {
            print(SystemTable, L"Error: Failed to write core mask via SMN write!\r\n");
            SystemTable->BootServices->Stall(5000000);
            return EFI_LOAD_ERROR;
        }

        unsigned int after = smn_rd(MASK_REG);
        if ((after & 0xFF) != 0xFF) {
            print(SystemTable, L"Error: Core mask write failed to take effect! Read back: ");
            print_hex(SystemTable, after & 0xFF);
            print(SystemTable, L"\r\n");
            SystemTable->BootServices->Stall(5000000);
            return EFI_LOAD_ERROR;
        }

#if TEST_MODE
        print(SystemTable, L"Core mask updated to 0xFF! Triggering warm reset...\r\n");
#endif
        // Instant warm reboot
        SystemTable->RuntimeServices->ResetSystem(EfiResetWarm, EFI_SUCCESS, 0, NULL);

        while (1) {
            __asm__ __volatile__("hlt");
        }
    }
}
