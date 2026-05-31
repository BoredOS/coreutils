# BoredOS Coreutils

This repository packages basic Unix commands and system status utilities tailored for BoredOS.

## Component Features
- **Basic CLI Commands**: Standard commands (`grep`, `uname`, `df`, `pwd`, `clear`, `echo`, etc.).
- **System Utilities**: Low-level status and diagnostic commands (`uptime`, `meminfo`, `lsblk`, `pci_list`, `crash`, `beep`, `reboot`, `shutdown`).

## Decoupled Building

This repository is designed to compile **either within the main BoredOS tree OR completely standalone**.

### 1. Integrated Build (Within BoredOS)
If built from the BoredOS root tree, the build system passes `BOREDOS_SDK` to the Makefile. It immediately compiles all CLI binaries against the shared SDK without duplicate downloads:
```bash
make BOREDOS_SDK=/path/to/shared/sdk
```

### 2. Standalone Build (Isolated Clone)
If cloned completely separately in isolation, running `make` will **automatically bootstrap standard dependencies**:
```bash
make
```
If `build/sdk` is missing, the Makefile automatically clones the pure standard library dependency from `https://github.com/boredos/libc.git`, compiles it, installs it to `build/sdk`, and uses it to build all utility ELFs standalone!

## Staging Installation
To stage the compiled commands and assets into your target initrd root filesystem directory:
```bash
make DESTDIR=/path/to/initrd/root install
```
- All utility executables (`*.elf`) are routed to `/bin/`
- Configuration assets (`sysfetch.cfg`) are routed to `/Library/conf/`
- System ASCII artwork (`boredos.txt`) is routed to `/Library/art/`
