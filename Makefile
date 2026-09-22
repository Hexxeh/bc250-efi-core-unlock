# Target executable name
TARGET = bc250-unlock.efi

# Submodule HEX patch file & generator script
HEX_FILE = bc250-smu-unlock/bc250_smu/patches.hex
HEX_GEN = gen_patches.py

# Source files
SRCS = main.c smu.c unlock.c patches.c

# Include directories for yoppeh-efi submodule (MIT Licensed)
INCLUDES = -I. -I yoppeh-efi -DEFI_PLATFORM=1

# Detect OS and locate clang if on macOS
ifeq ($(shell uname), Darwin)
  BREW_LLVM_ARM = /opt/homebrew/opt/llvm/bin/clang
  BREW_LLVM_INTEL = /usr/local/opt/llvm/bin/clang
  ifneq ($(wildcard $(BREW_LLVM_ARM)),)
    CLANG = $(BREW_LLVM_ARM)
  else ifneq ($(wildcard $(BREW_LLVM_INTEL)),)
    CLANG = $(BREW_LLVM_INTEL)
  else
    CLANG = clang
  endif
  MINGW = x86_64-w64-mingw32-gcc
else
  CLANG = clang
  MINGW = x86_64-w64-mingw32-gcc
endif

# Default target
all: mingw

# Generate patches.data.h from submodule patches.hex
patches.data.h: $(HEX_FILE) $(HEX_GEN)
	python3 $(HEX_GEN) $(HEX_FILE) patches.data.h

# Build using MinGW-w64 (works on Linux or macOS with 'brew install mingw-w64')
mingw: patches.data.h
	$(MINGW) $(INCLUDES) -nostdlib -mno-red-zone -shared -Wl,--subsystem,10 -e efi_main -o $(TARGET) $(SRCS)

# Build using Clang + LLD (works on Linux or macOS with 'brew install llvm')
clang: patches.data.h
	$(CLANG) $(INCLUDES) -target x86_64-unknown-windows -ffreestanding -mno-red-zone -nostdlib -fuse-ld=lld -Wl,-entry:efi_main -Wl,-subsystem:efi_application -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET) patches.data.h

.PHONY: all mingw clang clean
