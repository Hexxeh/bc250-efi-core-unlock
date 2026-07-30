# BC-250 Core Unlocker EFI Utility

This is a freestanding EFI application that automatically unlocks the two disabled cores on the BC-250 APU at boot time, eliminating the need to run the Python script and manually reboot the OS on every cold boot.

Credit to https://github.com/rw-r-r-0644/bc250-core-unlock for originally discovering this unlock approach - this repo just turns it into a form that's easier to apply on boot automatically.

## How It Works

1. The EFI application starts before your primary bootloader.
2. It queries the core presence mask at SMN `0x0115A870`.
3. If the mask is `0x77` (cold boot default):
   * It sends a message to the SMU via Queue 3 to write `0xFF` to the mask.
   * It triggers a **warm reboot**.
4. If the mask is `0xFF` (cores already active after the warm reboot):
   * It skips the patch and chainloads the real bootloader by exiting, deferring to the next boot option.
5. The OS boots with all 8 cores active automatically.

## Compilation

You can compile this utility on both macOS (OS X) and Linux.

### Compiling on macOS (OS X)

Because Xcode's default compiler lacks the PE/COFF linker (`lld-link`), you must use one of the following methods:

#### Method 1: Using Homebrew LLVM (Recommended)
1. Install LLVM (which includes `lld`):
   ```bash
   brew install llvm
   ```
2. Build the EFI binary:
   ```bash
   make clang
   ```
   *(The Makefile automatically detects the Homebrew LLVM installation path).*

#### Method 2: Using Homebrew MinGW-w64
1. Install the Mingw-w64 cross-compiler:
   ```bash
   brew install mingw-w64
   ```
2. Build the EFI binary:
   ```bash
   make mingw
   ```

---

### Compiling on Linux (e.g., target BC-250 host)

#### Method 1: Using MinGW-w64
1. Install the compiler:
   ```bash
   # Debian / Ubuntu / Proxmox
   sudo apt install gcc-mingw-w64-x86-64 make

   # RHEL / Fedora / AlmaLinux
   sudo dnf install mingw64-gcc make
   ```
2. Build the EFI binary:
   ```bash
   make mingw
   ```

#### Method 2: Using LLVM/Clang + LLD
1. Install the toolchain:
   ```bash
   # Debian / Ubuntu / Proxmox
   sudo apt install clang lld make
   ```
2. Build the EFI binary:
   ```bash
   make clang
   ```

---

## Installation (USB)

1. Copy the built bc250-unlock.efi to a FAT32 formatted USB stick at path `EFI/BOOT/BOOTX64.EFI`.
2. Change the boot order in your BIOS to boot from the USB stick first.

## Installation (NVME)

1. Copy the built bc250-unlock.efi to /boot/EFI/BOOT/COREUNLOCK.EFI.
2. Run `sudo efibootmgr --create --disk /dev/nvme0n1 --part 1 --label "CoreUnlock" --loader "\\EFI\\BOOT\\COREUNLOCK.EFI"`
