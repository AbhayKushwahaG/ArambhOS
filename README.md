# Aarambh OS (v0.4)

[![Target Architecture](https://img.shields.io/badge/architecture-x86_32--bit_Protected_Mode-blue.svg)](#system-architecture)
[![Filesystem](https://img.shields.io/badge/filesystem-AarFS_v1.0_(Double--Buffered)-green.svg)](#aarfs-filesystem-and-storage-persistence)
[![Python Runtime](https://img.shields.io/badge/runtime-Native_MicroPython_32--bit-yellow.svg)](#native-micropython-runtime)
[![Networking](https://img.shields.io/badge/networking-IPv4_/_TCP_/_DHCP_/_DNS_/_HTTP_/_Git-brightgreen.svg)](#networking-internet-stack-and-native-git)
[![Desktop GUI](https://img.shields.io/badge/gui-1024x768_TrueColor_Desktop-purple.svg)](#graphical-desktop-environment-and-applications)
[![Verification](https://img.shields.io/badge/tests-16%2F16_Suites_Passing_(100%25)-success.svg)](#automated-testing-and-verification)
[![Binary Compatibility](https://img.shields.io/badge/compatibility-ELF32_/_RPM_/_PE32_/_Wine--Check-orange.svg)](#binary-inspection-and-multi-os-compatibility-engine)

Aarambh OS is a custom, freestanding 32-bit x86 protected-mode hobby operating system designed for educational systems programming and low-level development. Developed in x86 Assembly and freestanding C99, Aarambh OS boots directly from BIOS into a flat 32-bit environment featuring its own custom crash-resilient disk filesystem (AarFS), an interactive full-screen text editor, an in-guest native MicroPython interpreter, Linux-inspired user and permission management, and an in-kernel TCP/IP network stack supporting Realtek RTL8139 and Intel 82540EM (e1000) Ethernet controllers.

The target test environment is **QEMU i386** emulation alongside physical validation objectives for legacy BIOS hardware such as the **Dell Latitude E6420** laptop (Intel Core 2nd Gen / QM67 chipset).

---

## Table of Contents

1. [Project Overview and Design Goals](#project-overview-and-design-goals)
2. [Dual Architecture: PM32 vs Legacy 16-Bit](#dual-architecture-pm32-vs-legacy-16-bit)
3. [System Architecture and Boot Flow](#system-architecture-and-boot-flow)
4. [Complete Shell Command Reference](#complete-shell-command-reference)
5. [Core Subsystems](#core-subsystems)
   - [AarFS: Filesystem and Storage Persistence](#aarfs-filesystem-and-storage-persistence)
   - [Interactive Full-Screen Text Editor](#interactive-full-screen-text-editor)
   - [Native MicroPython Runtime](#native-micropython-runtime)
   - [User, Group, and Permission Security](#user-group-and-permission-security)
   - [Networking, Internet Stack, and Native Git](#networking-internet-stack-and-native-git)
   - [Graphical Desktop Environment and Applications](#graphical-desktop-environment-and-applications)
   - [Scripting and Debian Package Management](#scripting-and-debian-package-management)
   - [Virtual Filesystem (VFS) and POSIX File Descriptors](#virtual-filesystem-vfs-and-posix-file-descriptors)
   - [Binary Inspection and Multi-OS Compatibility Engine](#binary-inspection-and-multi-os-compatibility-engine)
   - [Unified System Diagnostics: sysinfo, hwinfo, compat](#unified-system-diagnostics-sysinfo-hwinfo-compat)
   - [Host-Assisted Python and Calculator Bridge](#host-assisted-python-and-calculator-bridge)
6. [Repository Structure](#repository-structure)
7. [Build and Launch Instructions](#build-and-launch-instructions)
8. [Automated Testing and Verification](#automated-testing-and-verification)
9. [Security Boundaries and Technical Limitations](#security-boundaries-and-technical-limitations)
10. [Roadmap: Implemented vs Future Work](#roadmap-implemented-vs-future-work)
11. [License and Attribution](#license-and-attribution)

---

## Project Overview and Design Goals

Aarambh OS was created to explore operating system construction from first principles—avoiding external C runtime dependencies, third-party userland utilities, or embedded Linux kernels.

### Primary Objectives
- **Freestanding Operation**: Run directly on bare metal without an underlying host OS or external service dependencies.
- **Crash-Resilient Storage**: Implement a custom disk filesystem (AarFS) with alternating superblocks, double-buffered directory tables, and CRC verification to ensure file persistence across hard reboots.
- **Real Language Execution**: Embed a complete, native Python execution engine inside the operating system itself to run scripts natively from local disk storage.
- **Auditable Security**: Provide a multi-user model with unique numeric UIDs/GIDs, salted SHA-256 password hashing, user session tracking, and POSIX-style file permissions.
- **Native Internet Connectivity**: Implement real PCI bus enumeration, NIC drivers, and an RFC-compliant network stack capable of resolving DNS, fetching files over HTTP (`wget`, `curl`), and cloning repositories via native Git (`git clone`).

---

## Dual Architecture: PM32 vs Legacy 16-Bit

The repository maintains two independent operating system variants:

```
aarambh-v04/
├── PM32 Variant (Active Focus)
│   ├── Kernel: 32-bit Protected Mode (kernel/kernel32.asm + micropy32/*.c)
│   ├── Filesystem: AarFS (128 MiB disk: aarambh-v04-pm32-fs.img)
│   ├── Subsystems: Shell, Editor, Native MicroPython, Users/Auth, TCP/IP, Git
│   └── Build Script: build-protected.ps1
│
└── Legacy Variant (Historical Baseline)
    ├── Kernel: 16-bit Real Mode (kernel/kernel.asm)
    ├── Filesystem: FAT32 (128 MiB disk: aarambh-v04-fat32-rw.img)
    ├── Subsystems: Basic Real-Mode CLI, FAT32 cluster chain reader/writer
    └── Build Script: build.ps1
```

*Note: All current features (editor, users, native Python, networking, Git) operate exclusively within the **PM32** variant. The legacy 16-bit build is preserved as a verified historical baseline and is never modified by PM32 builds.*

---

## System Architecture and Boot Flow

The 32-bit protected-mode kernel initializes hardware in a staged sequence:

```mermaid
flowchart TD
    A["BIOS POST & MBR Bootloader (bootloader/boot.asm)"] -->|"Loads 63 sectors (32,256 B max) via INT 13h"| B["Protected-Mode Switch (kernel/kernel32.asm)"]
    
    subgraph PM32_Init [PM32 Kernel Core]
        B --> C["Load Global Descriptor Table (GDT)"]
        C --> D["Enable CR0.PE (Protected Mode 32-bit)"]
        D --> E["Initialize Interrupt Descriptor Table (IDT) & Remap 8259 PIC"]
        E --> F["Initialize PIT Timer (IRQ0, ~1 kHz) & PS/2 Keyboard (IRQ1)"]
        F --> G["Parse E820 Memory Map & Setup Conventional Bump Allocator"]
        G --> H["Mount AarFS Disk via Primary ATA Master PIO"]
    end

    subgraph C_Subsystem [Native C Subsystem (LBA 132000)]
        H --> I["Load micropy32.bin to 0x00200000"]
        I --> J["auth_init: Mount /etc/passwd & /etc/group"]
        J --> K["net_boot_init: Scan PCI Bus, Init RTL8139 / e1000, Start DHCP"]
    end

    K --> L["Interactive Shell Prompt (root@AR32:/ >)"]
```

### Architectural Specifications
- **CPU Execution Mode**: 32-bit Protected Mode (flat memory segmentation, CS=0x08, DS=ES=FS=GS=SS=0x10).
- **GDT Configuration**: Ring 0 Kernel Code/Data selectors (`0x08`, `0x10`) alongside configured Ring 3 User Code/Data selectors (`0x1B`, `0x23`).
- **Interrupts & Timers**: Programmable Interrupt Controller (8259 PIC) remapped to vectors 0x20..0x2F. Vector `0x80` registered with DPL=3 for the system call dispatcher. PIT Timer running on IRQ0 (~1 kHz).
- **Console & Serial**: Memory-mapped VGA 80x25 colour text buffer at physical address `0x000B8000`. Diagnostic logging mirrored to UART COM1 (`0x3F8`, 115200 8N1).
- **Memory Allocation**: Bounded bump allocator initializing conventional heap memory up to `0x00090000`, reserving high memory (`0x00200000`+) for the C runtime and GC heap.

---

## Complete Shell Command Reference

The Aarambh OS PM32 shell features a dual-dispatch command engine: core file/system commands are handled directly by the kernel assembly dispatcher (`kernel/commands32.asm`), while authentication, network, and Python commands are routed to the C runtime dispatcher (`micropy32/auth.c`).

### 1. General & Navigation Commands

| Command | Syntax / Arguments | Function | Example | Status / Notes |
| :--- | :--- | :--- | :--- | :--- |
| `help` | `help [category \| command]` | Displays the paged, indexed manual or details on a specific command. | `help files`<br>`help read` | **Fully Implemented**. Pager keys: Space (next), I (index), Q (quit). |
| `guide` | `guide` | Interactive beginner tutorial covering shell navigation, file creation, and Python. | `guide` | **Fully Implemented**. Multi-page static walkthrough. |
| `identity` | `identity` | Prints the kernel identifier and active execution mode. | `identity` | **Fully Implemented**. Displays "Aarambh OS PM32". |
| `say` | `say <text>` | Echoes text to the VGA console and COM1 serial. | `say Hello world` | **Fully Implemented**. 126 characters maximum. |
| `clear` | `clear` | Clears the VGA text screen and resets cursor position. | `clear` | **Fully Implemented**. Alias: `wipe`. |
| `pwd` | `pwd` | Prints the absolute path of the current directory. | `pwd` | **Fully Implemented**. Alias: `where`. |
| `cd` | `cd [<path>]` | Changes current AarFS working directory. Supports `/`, `.`, `..`, and `~`. | `cd /home/aarambh`<br>`cd ~` | **Fully Implemented**. Bare `cd` or `cd ~` navigates to user home. |
| `mem` | `mem` | Displays BIOS conventional memory and bump-heap bytes remaining. | `mem` | **Fully Implemented**. Real-time allocator report. |
| `ticks` | `ticks` | Displays cumulative PIT timer ticks elapsed since boot. | `ticks` | **Fully Implemented**. ~1 kHz tick counter. |
| `reboot` | `reboot` | Soft reboots the virtual machine via PS/2 keyboard controller pulse. | `reboot` | **Fully Implemented**. Port `0x64` reset pulse. |
| `fault` | `fault` | Deliberately triggers an INT 3 breakpoint to verify kernel panic handling. | `fault` | **Fully Implemented**. Diagnostic halt. |

### 2. Filesystem & Editor Commands

| Command | Syntax / Arguments | Function | Example | Status / Notes |
| :--- | :--- | :--- | :--- | :--- |
| `ls` | `ls` | Lists files and directories in the current working directory. | `ls` | **Fully Implemented**. Alias: `peek`. Directories end with `/`. |
| `mkdir` | `mkdir <name>` | Creates a new directory in the current working directory. | `mkdir project` | **Fully Implemented**. Validates 39-character name limits. |
| `touch` | `touch <file>` | Creates an empty file if absent, or touches timestamp. | `touch main.py` | **Fully Implemented**. Alias: `forge`. |
| `read` | `read <file>` | Outputs file contents directly to console. | `read notes.txt` | **Fully Implemented**. Alias: `cat`. Enforces file read permissions. |
| `scribe` | `scribe <file> <text>` | Overwrites or creates file with the specified text string. | `scribe msg.txt Hello` | **Fully Implemented**. 16 KiB size boundary enforced. |
| `grow` | `grow <file> <text>` | Appends text to an existing file in the current directory. | `grow log.txt [OK]` | **Fully Implemented**. Enforces file write permissions. |
| `shift` | `shift <old> <new>` | Renames an entry within the current directory. | `shift old.txt new.txt` | **Fully Implemented**. Alias: `mv`. Prevents collision. |
| `cp` | `cp <src> <dst>` | Creates a duplicate copy of a file. | `cp main.py copy.py` | **Fully Implemented**. Preserves permissions and content. |
| `rm` | `rm <name>` | Deletes a file or an empty directory. | `rm junk.txt` | **Fully Implemented**. Alias: `erase`. Blocks non-empty directory deletion. |
| `df` | `df` | Displays AarFS logical slot utilization and capacity. | `df` | **Fully Implemented**. Reports slot allocation out of 2048. |
| `edit` | `edit <file>` | Full-screen interactive visual text editor with save and scroll. | `edit script.py` | **Fully Implemented**. Ctrl-S save, Esc exit, arrows, 16 KiB limit. |

### 3. User, Group & Security Commands

| Command | Syntax / Arguments | Function | Example | Status / Notes |
| :--- | :--- | :--- | :--- | :--- |
| `whoami` | `whoami` | Displays the username of the active session. | `whoami` | **Fully Implemented**. Output: `root` or active user. |
| `id` | `id [username]` | Prints numeric UID, primary GID, and supplementary GID list. | `id`<br>`id aarambh` | **Fully Implemented**. |
| `users` | `users` | Lists all registered accounts in `/etc/passwd`. | `users` | **Fully Implemented**. Formatted table of UID, GID, home. |
| `groups` | `groups [username]` | Lists group memberships for a user. | `groups` | **Fully Implemented**. |
| `useradd` | `useradd [-u uid] [-g gid] [-d home] [-p pass] <name>` | Registers a new user account with duplicate collision checks. | `useradd -u 1001 dev` | **Fully Implemented**. Superuser (root) only. Silent password prompt. |
| `userdel` | `userdel <name>` | Deletes user record from `/etc/passwd`. | `userdel dev` | **Fully Implemented**. Root only; blocks deleting self or root. |
| `usermod` | `usermod [-g gid] [-d home] <name>` | Modifies an existing user's primary group or home directory. | `usermod -g 1001 dev` | **Fully Implemented**. Root only. |
| `groupadd`| `groupadd [-g gid] <name>` | Creates a new group record in `/etc/group`. | `groupadd -g 1001 devs` | **Fully Implemented**. Validates duplicate IDs/names. |
| `groupdel`| `groupdel <name>` | Deletes a group from `/etc/group`. | `groupdel devs` | **Fully Implemented**. Root only. |
| `login` | `login <name>` | Transitions session to specified user with password verification. | `login aarambh` | **Fully Implemented**. Silent password prompt. |
| `logout` | `logout` | Ends current session and returns to unprivileged user or root. | `logout` | **Fully Implemented**. |
| `su` | `su [name]` | Switches user context. Prompts for password unless called by root. | `su aarambh`<br>`su root` | **Fully Implemented**. Defaults to `root`. |
| `passwd` | `passwd [name]` | Updates account password using salted SHA-256 hashing. | `passwd` | **Fully Implemented**. Non-root users must confirm current pass. |
| `chmod` | `chmod <octal> <file>` | Updates file/directory permission bits (e.g. 0644, 0755, 0600). | `chmod 0600 secret.txt` | **Fully Implemented**. Enforced for owner or root. |
| `chown` | `chown <name> <file>` | Changes user ownership of a file or directory. | `chown aarambh doc.txt` | **Fully Implemented**. Superuser only. |
| `chgrp` | `chgrp <name> <file>` | Changes group ownership of a file or directory. | `chgrp devs doc.txt` | **Fully Implemented**. Owner or superuser. |
| `stat` | `stat <file>` | Displays detailed file metadata, UID, GID, octal mode, and rwx string. | `stat main.py` | **Fully Implemented**. Detailed security inspection. |

### 4. Networking & Internet Commands

| Command | Syntax / Arguments | Function | Example | Status / Notes |
| :--- | :--- | :--- | :--- | :--- |
| `ifconfig` | `ifconfig` | Displays active Ethernet adapter, MAC address, IP, mask, gateway, DNS, and RX/TX packet counts. | `ifconfig` | **Fully Implemented**. Automatically detects RTL8139 or e1000. |
| `ping` | `ping <ip \| hostname>` | Transmits 4 ICMP echo packets with 1000ms timeout and RTT statistics. | `ping 8.8.8.8`<br>`ping google.com` | **Fully Implemented**. Resolves hostnames via built-in DNS. |
| `network` | `network` | Interactive console GUI (inspired by `raspi-config`) for adapter configuration and diagnostics. | `network` | **Fully Implemented**. 11 menus; persists to `/etc/network.conf`. |
| `wget` | `wget [-O <output>] <url>` | Downloads remote HTTP resource directly into AarFS. | `wget http://10.0.2.2:8088/test.txt` | **Fully Implemented**. Streams body directly to disk. |
| `curl` | `curl [-o <file>] [-I] <url>` | HTTP client for terminal display, file output, or HEAD header inspection. | `curl http://10.0.2.2:8088`<br>`curl -I http://10.0.2.2` | **Fully Implemented**. Supports `-o` and `-I` options. |
| `git` | `git [--version \| clone <url> [<dir>]]` | Native Smart HTTP Git client; discovers refs, fetches packfile, decompresses objects, and checks out files. | `git clone http://10.0.2.2:8088/repo.git` | **Fully Implemented**. Packfile inflate via embedded `uzlib`. |

### 5. Desktop, Scripting & Computational Commands

| Command | Syntax / Arguments | Function | Example | Status / Notes |
| :--- | :--- | :--- | :--- | :--- |
| `desktop` | `desktop` | Launches the 1024x768 32-bit TrueColor desktop environment with Fedora GNOME panel, Windows XP Luna taskbar & start menu, multi-window manager, native apps, and login screen. | `desktop` | **Fully Implemented**. Alias: `gui`. Press `Esc` or `Q` (or click Exit) to return cleanly to text shell. |
| `run` | `run <script.sh>` | Shell script interpreter supporting comments (`#`), variables (`VAR=val`, `$VAR`, `$?`, `$USER`, `$HOME`), sequential execution, and error handling. | `run setup.sh` | **Fully Implemented**. Alias: `sh`. |
| `dpkg` | `dpkg [-I \| -c \| -i \| -l \| -r] <package.deb>` | Debian package manager for metadata inspection, content listing, payload installation into AarFS, and `/var/lib/dpkg/status` tracking. | `dpkg -I app.deb`<br>`dpkg -i app.deb`<br>`dpkg -l` | **Fully Implemented**. Alias: `deb`. Transparently notes POSIX binary execution boundaries. |
| `python` | `python <file.py>` | Executes a Python script natively inside Aarambh OS via MicroPython. | `python hello.py` | **Fully Implemented**. Runs on-metal with zero host dependencies. |
| `py` | `py <file.py> \| py --version \| py help` | Runs script via host bridge on COM2; automatically falls back to native MicroPython if helper absent. | `py hello.py`<br>`py native hello.py` | **Fully Implemented**. `py native` forces in-guest execution. |
| `calc` | `calc [expression \| host \| help]` | Signed 32-bit integer calculator with precedence and parentheses. | `calc (10 + 20) * 3`<br>`calc` | **Fully Implemented**. Bare `calc` opens REPL or asks host helper. |

### 6. Binary Compatibility & System Diagnostics Commands

| Command | Syntax / Arguments | Function | Example | Status / Notes |
| :--- | :--- | :--- | :--- | :--- |
| `elfinfo` | `elfinfo <file.elf>` | Validates ELF32 header, machine architecture, entry point, segments (`PT_LOAD`/`PT_INTERP`), and sections. | `elfinfo app.elf`<br>`elf app.elf` | **Fully Implemented**. Alias: `elf`. Distinguishes static freestanding vs dynamic glibc binaries. |
| `rpm` | `rpm [-qi \| -ql \| -qR \| -qp] <pkg.rpm>` | Inspects Red Hat RPM packages; reads Lead and Header tags, lists metadata, files, and requirements. | `rpm app.rpm`<br>`rpm -ql app.rpm`<br>`rpm -qR app.rpm` | **Fully Implemented**. Endian-safe tag parsing. Identifies x86/noarch vs 64-bit packages. |
| `peinfo` | `peinfo <file.exe>` | Inspects Windows PE32 executables; parses DOS/PE headers, sections, and Import Directory DLLs/IAT symbols. | `peinfo app.exe`<br>`wine-check app.exe` | **Fully Implemented**. Alias: `wine-check`. Evaluates Win32 KERNEL32/USER32 emulation readiness. |
| `sysinfo` | `sysinfo` | Displays comprehensive OS specifications, CPU mode, uptime, physical memory map, VFS, BGA display, and networking. | `sysinfo` | **Fully Implemented**. Full system specification report. |
| `hwinfo` | `hwinfo` | Scans PCI bus; enumerates all physical devices, vendor/device IDs, device classes, IRQs, and BAR addresses. | `hwinfo` | **Fully Implemented**. Live PCI bus enumeration. |
| `compat` | `compat` | Displays categorical software compatibility matrix comparing functional features vs multi-phase roadmap targets. | `compat` | **Fully Implemented**. Explicit capability roadmap report. |

---

## Core Subsystems

### AarFS: Filesystem and Storage Persistence

AarFS is a custom, non-FAT filesystem designed specifically for crash-resilience on 128 MiB block devices.

```
AarFS 128 MiB Disk Layout:
[ LBA 0       ] MBR Boot Sector (512 bytes)
[ LBA 1..63   ] Kernel Binary Sectors (Reserved up to 32,256 bytes)
[ LBA 64      ] Primary Superblock
[ LBA 65      ] Alternate Superblock
[ LBA 66..321 ] Directory Table Copy A (256 sectors, 2048 entries)
[ LBA 322..577] Directory Table Copy B (256 sectors, 2048 entries)
[ LBA 578..   ] Double-buffered Data Slots (32 sectors / 16 KiB per slot)
[ LBA 132000..] Native MicroPython Freestanding Binary (micropy32.bin)
```

- **Crash-Resilience Architecture**: AarFS alternates between Superblock A and Superblock B. Metadata commits write to the inactive directory table first, verify table CRC32, and then atomically flip the active superblock pointer. A power loss during writes remounts the previous valid generation without filesystem corruption.
- **File Metadata**: Each 64-byte directory entry tracks entry type (file/dir), parent directory ID, 39-character filename, file size, owner UID, group GID, and octal permission mode bits (`0644`, `0755`, `0600`).
- **ATA Driver**: Primary ATA master PIO driver with automatic 8-attempt retry, soft bus reset, and mandatory write cache flushes (`0xE7`).

### Interactive Full-Screen Text Editor

Typing `edit <file>` launches an interactive visual editor built directly into the kernel:
- **Display Matrix**: 25 rows by 80 columns VGA text mode. Rows 0..22 display document lines; row 23 displays contextual shortcut reminders; row 24 displays a real-time status bar (`EDIT <name>`, file size, line/col coordinates, and `READY`/`MODIFIED`/`SAVED` indicators).
- **Navigation & Editing**: Printable characters insert at cursor; full navigation via Arrow keys, `Home`, `End`, `PgUp`, `PgDn`; deletion via `Backspace` and `Delete`.
- **Safety**: Unsaved changes prompt for confirmation on `Esc`; `Ctrl-S` invokes the AarFS atomic write and commit routine.
- **Limits**: Files up to 16,384 bytes (16 KiB). Lines wider than 80 columns scroll horizontally around the active cursor.

### Native MicroPython Runtime

Aarambh OS embeds a complete, freestanding 32-bit port of MicroPython located at LBA 132,000.
- **On-Metal Execution**: Running `python script.py` or `py native script.py` loads `micropy32.bin` into physical memory at `0x00200000`, establishes an internal 128 KiB garbage-collected heap, and executes bytecode natively.
- **Exception Containment**: Execution occurs inside an NLR (non-local return) handler. Python exceptions (such as `ZeroDivisionError` or syntax errors) print standard Python tracebacks to the VGA terminal without halting or crashing the kernel.
- **Built-in `aarambh` Module**:
  ```python
  import aarambh
  print("User:", aarambh.getuser(), "UID:", aarambh.getuid())
  stat_info = aarambh.stat("notes.txt")
  print("File size:", stat_info["st_size"])
  data = aarambh.read("notes.txt")
  print("Ping gateway:", aarambh.ping("10.0.2.2"))
  ```

### User, Group, and Permission Security

A complete multi-user authentication model is implemented across kernel and filesystem:
- **Accounts & Groups**: Root administrator (UID 0, GID 0) and unprivileged user (UID 1000, GID 1000) initialized by default, with dynamic account creation stored in `/etc/passwd` and `/etc/group`.
- **Cryptographic Password Security**: User passwords are encrypted with a pseudorandom salt and SHA-256 cryptographic hashing. Plaintext passwords are never stored. Console password input is silent to prevent shoulder-surfing.
- **Enforcement Rules**:
  - Root (UID 0) bypasses read/write restrictions.
  - File owner matches check bits 6..8 (`rwx------`); group matches check bits 3..5 (`---rwx---`); other users check bits 0..2 (`------rwx`).
  - Read operations (`read`, `cat`, `python`) require read permission (`r`).
  - Write operations (`scribe`, `grow`, `rm`, `edit`) require write permission (`w`).
  - Directory traversal (`cd`) requires execute permission (`x`).
- **System Call Interface**: GDT user selectors (`0x1B`, `0x23`) and IDT vector `0x80` expose kernel system calls (`SYS_GETUID`, `SYS_SETUID`, `SYS_GETGID`, `SYS_SETGID`, `SYS_WRITE`).

### Networking, Internet Stack, and Native Git

The networking subsystem connects Aarambh OS to local networks and the internet:
- **Hardware Drivers**:
  - **Realtek RTL8139**: PCI Fast Ethernet controller (I/O port registers, 4 TX ring descriptors, 64 KiB + 16-byte wrap RX ring buffer).
  - **Intel 82540EM (e1000)**: PCI Gigabit Ethernet controller (MMIO registers, 64-descriptor circular TX/RX DMA descriptor rings).
  - **Wi-Fi Diagnostics**: Scans PCI bus for Intel Centrino Advanced-N 6205; honestly reports missing firmware microcode blobs rather than fabricating simulated APs.
- **Protocols Implemented**: Ethernet II, ARP (cache with request/reply handling), IPv4 (fragmentation rejection, checksum validation), ICMP (echo reply generation, echo request ping), UDP, DHCP client (automatic configuration of IP, subnet, gateway, DNS), DNS resolver (UDP port 53 query encoding and A-record decoding), and TCP (RFC 793 state machine, 3-way handshake, SYN/ACK, retransmission timeouts, FIN/ACK teardown).
- **Interactive `network` GUI**: Keyboard-driven configuration utility allowing manual IP/DNS assignment, live gateway pinging, and connection diagnostics. Persists settings to `/etc/network.conf`.
- **Native Smart HTTP Git Client**: `git clone <url>` implements the Git Smart HTTP protocol over raw TCP sockets. Discovers remote references, fetches packfiles via HTTP POST, decompresses compressed Git objects using embedded `uzlib`, parses tree objects, and checks out files directly into AarFS directories.

### Host-Assisted Python and Calculator Bridge

For workflows requiring full desktop Python packages or Windows applications, an optional COM2 bridge is provided:
- **Physical Link**: UART COM2 (`0x2F8`, IRQ3, 115200 8N1). QEMU establishes a loopback-only TCP socket listener (`127.0.0.1:40242`); the host script `host_python_bridge.py` connects to this port.
- **Sandboxing & Framing**: Structured binary framing (`APY1` / `APR1`) with strict timeouts (15s default), 16 KiB script limit, and 8 KiB output truncation. Launch capability is strictly allow-listed to `calc.exe`.

### Graphical Desktop Environment and Applications

Aarambh OS features a full, high-resolution, mouse-driven desktop operating system combining a clean **Fedora GNOME** desktop layout with signature **Windows XP Luna** window styling and applications:

```
+---------------------------------------------------------------------------------------------------+
| Activities  |  Aarambh Control Center          Aarambh OS  14:32:05            [Gentix]  [LAN UP] | <-- GNOME Panel (28px)
+---------------------------------------------------------------------------------------------------+
|  [Computer]       +------------------------ Aarambh Control Center ------------------[-] [+] [X]+ |
|  [Explorer]       | Appearance  |  Wallpapers:                                                   | |
|  [Notepad]        | Display     |  [1] XP Bliss        [2] GNOME Adwaita                         | |
|  [Paint]          | Users       |  [3] Classic Teal    [4] Charcoal Dark                         | |
|  [Snake]          | Diagnostics |                                                                | |
|  [Settings]       |             |  [ Apply & Save ]                                              | |
|  [Terminal]       +------------------------------------------------------------------------------+ |
|                                                                                                   |
+---------------------------------------------------------------------------------------------------+
| [start]  [E] [N] [P] [S] | [ Aarambh Control Center ]                             14:32:05 | <-- XP Taskbar (36px)
+---------------------------------------------------------------------------------------------------+
```

- **High-Resolution Framebuffer Pipeline**:
  - Operates in high-resolution **1024x768 32-bit TrueColor** (16.7 million colors) via Bochs BGA / VBE Linear Framebuffer at physical address `0xFD000000` (queried from PCI BAR0).
  - Double-buffered backbuffer located safely at physical RAM `0x00800000` (8 MiB physical offset), eliminating collisions with kernel code and MicroPython binary memory.
  - Reusable graphics primitives: `gfx_put_pixel`, `gfx_fill_rect`, `gfx_draw_rect`, `gfx_draw_line` (Bresenham), `gfx_draw_rect_3d`, `gfx_gradient_v`, `gfx_gradient_h`, `gfx_draw_text`, `gfx_draw_text_scaled`, `gfx_draw_icon`.
  - Windows BMP image encoder (`gfx_save_bmp`) and decoder (`gfx_load_bmp`) integrated with AarFS.
  - Hardware clipping rectangle stack (`gfx_set_clip`, `gfx_reset_clip`) preventing window contents from painting outside window bounds.
- **PS/2 Interrupt-Driven Mouse Subsystem**:
  - Intel 8042 controller auxiliary device initialization, IRQ12 interrupt vector 44, streaming packet parser with 64-byte kernel ring buffer (`kernel/kernel32.asm`).
  - Bit 3 packet synchronization, relative X/Y coordinate decoding, screen boundary clamping, and single/double-click/drag detection.
  - Crisp 12x18 arrow pointer with black border, pure white interior, and drop shadow.
- **Reusable Multi-Window Manager (`micropy32/wm.c`)**:
  - Manages up to 16 concurrent windows with Z-ordering, active focus, title-bar dragging with desktop bounds clamping, and window controls (Minimize `[-]`, Maximize `[+]`, Close `[X]`).
  - Active vs inactive title bar styling: Windows XP Luna Blue gradient for focused windows vs muted slate gray for inactive windows.
  - Strict window content clipping preventing applications from drawing outside client bounds.
- **Graphical Login Screen (`micropy32/login.c`)**:
  - Centered login dialog box with drop shadow, Aarambh OS branding, and user account selector (`Gentix` or `root`).
  - Masked password entry (`*`), blinking cursor, and error reporting.
  - Pre-configured bootstrap user `Gentix` with initial default password `password`.
  - Forced first-time password change dialog requiring new password and confirmation, validated and persisted into AarFS `/etc/passwd` with 16-character salt and SHA-256 hash.
- **Five Core Native Applications**:
  - **Notepad (`micropy32/app_notepad.c`)**: Visual text editor with AarFS Open/Save/Save-As support, text navigation, character/line counters, and status bar.
  - **Paint (`micropy32/app_paint.c`)**: 480x320 drawing canvas with Pen, Brush, Eraser, Line, and Rectangle tools, brush size selection (1, 2, 4, 8px), 16-color palette picker, and BMP format save/load to AarFS.
  - **File Explorer (`micropy32/app_filemgr.c`)**: Interactive AarFS filesystem navigation with directory tree, file icons, size, permissions, and launch associations (opens `.txt`/`.sh` in Notepad, `.bmp` in Paint).
  - **Snake Game (`micropy32/app_snake.c`)**: Windowed arcade game with 20x20 grid, real-time score tracking, food generation, self-collision detection, and both keyboard arrow and on-screen mouse D-Pad controls.
  - **Settings App (`micropy32/app_settings.c`)**: Control center featuring 4 categories: Appearance (wallpaper/theme selection with live thumbnail preview), Display resolution, User Accounts (password modification), and System Diagnostics.
- **Desktop Shell & Customization Engine**:
  - **Fedora GNOME Top Panel**: 28px dark slate panel with "Activities" button, live digital clock (`HH:MM:SS`), active window title, and user/network status pills.
  - **Windows XP Luna Taskbar**: 36px bottom taskbar with "Start" button, quick-launch icons, open window taskbar buttons, and system tray clock.
  - **Windows XP Start Menu**: Two-column popup menu with user profile banner, application launcher list, places shortcuts, Log Off, and Exit to Shell.
  - **Desktop Icons**: Vector icons on desktop (My Computer, File Explorer, Notepad, Paint, Snake Game, Settings, Terminal) with double-click execution.
  - **4 Procedural Wallpapers**: `XP Bliss` (azure sky + sun radiance + rolling green hills), `GNOME Adwaita` (indigo/slate polygonal gradient), `Classic Teal` (Windows 95/XP teal with dotted grid), `Charcoal Dark`.
  - Persistent `/etc/desktop.conf` configuration in AarFS across reboots.
- **Terminal Safety & Clean Restoration**:
  - Pressing `Esc` or `Q` (or clicking `Exit to Shell`) cleanly resets video mode back to standard 80x25 text mode (Mode 03h) and returns directly to the shell prompt (`Gentix@AR32:/ >`).

### Scripting and Debian Package Management

- **Shell Script Runner (`run` / `sh`)**:
  - Executes shell scripts stored on AarFS line-by-line (`run <script.sh>` or `sh <script.sh>`).
  - Supports comments (`#`), variable assignments (`VAR=val`), variable expansions (`$VAR`, `$?`, `$USER`, `$HOME`), sequential execution, and line-numbered error reporting.
  - Integrated with the kernel dispatcher, allowing scripts to execute both built-in filesystem commands and native C subsystem commands.
- **Debian Package Manager (`dpkg` / `deb`)**:
  - Standard Debian binary package format inspection and installation:
    - `dpkg -I <pkg.deb>`: Inspects package metadata and control information from the `control` archive member.
    - `dpkg -c <pkg.deb>`: Lists all archive members and their payload sizes.
    - `dpkg -i <pkg.deb>`: Extracts package payloads into AarFS, enforces strict path traversal defense (rejecting `..` and relative escape sequences), and records package state in `/var/lib/dpkg/status`.
    - `dpkg -l`: Displays all installed packages formatted as standard Debian status columns (`ii <Name> <Version> <Architecture> <Description>`).
    - `dpkg -r <pkg_name>`: Removes installed package records from `/var/lib/dpkg/status`.
  - **Binary Compatibility Boundary**: Debian packages containing native Linux x86 ELF executables require Linux POSIX system calls (`int 0x80` Linux ABI) which are documented on the kernel roadmap. Aarambh OS honestly reports this execution boundary while providing full payload archive handling and status tracking.

### Virtual Filesystem (VFS) and POSIX File Descriptors

Aarambh OS includes a unified Virtual Filesystem (VFS) layer (`micropy32/vfs.c`, `micropy32/vfs.h`) that decouples application file operations from raw underlying disk extents:
- **POSIX File Descriptors**: Supports up to 16 concurrent file descriptors (`0` through `15`). Standard descriptors `0` (`/dev/stdin`), `1` (`/dev/stdout`), and `2` (`/dev/stderr`) are pre-allocated at boot.
- **Mount Table**: Supports multiple filesystem providers across mount points. The root partition (`/`) mounts directly to AarFS v0.4 through kernel API dispatchers, providing a clean abstraction for future mounts such as `/dev` (virtual device nodes), `/proc` (kernel state reporting), and FAT32 removable flash media.
- **Path Normalization**: Canonicalizes user paths (`vfs_normalize_path`), collapsing duplicate slashes (e.g. `//etc///passwd` -> `/etc/passwd`), handling relative directories, and resolving path boundaries.
- **Standard VFS API**: Exposes `vfs_open`, `vfs_read`, `vfs_write`, `vfs_close`, `vfs_stat`, `vfs_read_entire_file`, and `vfs_write_entire_file`.

### Binary Inspection and Multi-OS Compatibility Engine

Aarambh OS incorporates real, native parsers for the primary binary executable and packaging formats across Linux and Windows:

1. **Native ELF32 Binary Inspector (`elfinfo` / `elf`)**:
   - Parses Standard 32-bit x86 ELF executables (`Elf32_Ehdr`, `Elf32_Phdr`, `Elf32_Shdr`).
   - Validates ELF magic (`0x7F 'E' 'L' 'F'`), Machine (`EM_386`), Type (`ET_EXEC` vs `ET_DYN` vs `ET_REL`), and Entry Point Virtual Address.
   - Scans Program Headers for `PT_LOAD` code/data memory segments, file offsets, and memory footprints.
   - Detects `PT_INTERP` dynamic linker paths (e.g. `/lib/ld-linux.so.2`) and dynamic symbol sections.
   - Formulates an honest compatibility diagnostic: statically linked freestanding binaries are classified as immediately loadable via the Aarambh process execution layer, while dynamically linked binaries report their Linux glibc and POSIX system call prerequisites.

2. **Linux Red Hat RPM Package Inspector (`rpm`)**:
   - Native parser for RPM packages (`.rpm`) used by Fedora, RHEL, and CentOS.
   - Validates RPM Lead structure (`0xedabeedb`, major version 3, binary package type, arch ID, OS ID).
   - Endian-safe parsing of RPM Header structures (`0x8eade801`) and tag index tables.
   - Query modes: `rpm <file.rpm>` (full report), `rpm -qi` (package metadata, version, license, summary), `rpm -ql` (package file contents list), `rpm -qR` (dependencies and requirements such as `libc.so.6`), and `rpm -qp` (package brief summary).
   - Identifies compatible 32-bit x86/noarch packages vs incompatible 64-bit packages.

3. **Windows PE32 / Wine Compatibility Tool (`peinfo` / `wine-check`)**:
   - Inspects 32-bit Windows Portable Executables (`.exe`, `.dll`).
   - Validates MS-DOS MZ header (`0x5A4D`) and reads NT header offset `e_lfanew`.
   - Validates NT PE signature (`PE\0\0`), COFF Machine (`IMAGE_FILE_MACHINE_I386` = `0x014C`), and Optional Header PE32 magic (`0x10B`).
   - Discovers ImageBase (`0x00400000`), AddressOfEntryPoint, Subsystem (`Windows GUI` vs `Windows CUI Console` vs `Native`), and Section Table (`.text`, `.rdata`, `.data`, `.rsrc`).
   - Translates RVAs to raw file offsets and parses the Import Directory Table (Directory 1); resolves imported DLLs (`KERNEL32.dll`, `USER32.dll`, `GDI32.dll`) and parses Import Name Tables to identify Win32 API functions (`ExitProcess`, `GetStdHandle`, `WriteFile`, `MessageBoxA`).
   - Produces Wine-style compatibility evaluations distinguishing between achievable KERNEL32 console apps and windowed applications requiring USER32/GDI32 window manager bridging.

### Unified System Diagnostics: sysinfo, hwinfo, compat

- **`sysinfo`**: Unified system specifications and operational telemetry:
  - Kernel architecture: 32-bit Protected Mode, Ring 0, flat 4 GB segmentation.
  - PIC 8259A interrupt vectors and PIT 8254 timer uptime.
  - Physical memory layout: Low conventional memory, protected kernel at `0x10000`, C subsystem at `0x200000`, MicroPython GC heap, and high-resolution VESA backbuffer at `0x800000`.
  - Storage & VFS status: ATA/IDE master 0, AarFS v0.4 partition at LBA 2048, and active VFS file descriptors.
  - Graphics: Bochs/QEMU BGA controller, BAR0 MMIO address, and 1024x768 TrueColor video mode.
  - Networking: Active NIC driver (e1000 / rtl8139), MAC address, link status, and IPv4 DHCP/DNS network configuration.
  - Supported runtimes: Assembly, freestanding C, MicroPython, Shell scripts, DEB, RPM, ELF, and PE32.
- **`hwinfo`**: Live PCI bus scan (`pci_scan()`) enumerating all detected hardware peripherals, PCI bus/slot/function, Vendor ID, Device ID, class codes, IRQ lines, and Base Address Registers (BARs).
- **`compat`**: Categorical software compatibility matrix explicitly comparing working in-tree features against multi-phase architectural roadmap targets.

---

## Repository Structure

```
aarambh-v04/
├── .gitignore                   # Toolchain artifacts, disk images, and debug logs
├── README.md                    # Official technical documentation
├── build-protected.ps1          # Primary build script for PM32 AarFS kernel
├── build.ps1                    # Legacy build script for 16-bit FAT32 kernel
├── format_aarfs.py              # AarFS virtual disk formatter and integrity checker
├── make_image.py                # FAT32 virtual disk formatter
├── host_python_bridge.py        # COM2 host bridge helper for Python & Calculator
│
├── bootloader/
│   └── boot.asm                 # BIOS MBR bootloader (512 bytes, INT 13h loader)
│
├── kernel/
│   ├── kernel32.asm             # PM32 kernel entry, GDT, IDT, PIC, PIT, VGA, Allocator
│   ├── aarfs32.asm              # AarFS crash-resilient disk filesystem driver
│   ├── commands32.asm           # Shell command dispatcher, suggestion engine, table
│   ├── editor32.asm             # Full-screen interactive visual text editor
│   ├── calc32.asm               # Signed 32-bit recursive-descent integer calculator
│   ├── help32.asm               # Indexed, paged interactive help system
│   ├── python_bridge32.asm      # Native Python runner & COM2 bridge client
│   ├── kernel.asm               # Legacy 16-bit real-mode kernel
│   └── fat32.asm                # Legacy FAT32 filesystem driver
│
├── micropy32/                   # Freestanding C Subsystem, Desktop & Network Stack
│   ├── Makefile                 # MinGW-w64 Makefile producing micropy32.bin
│   ├── linker.ld                # Freestanding linker script (origin 0x00200000)
│   ├── mpconfigport.h           # MicroPython compiler configuration
│   ├── entry.c                  # C entry point, memory pool, 'aarambh' module
│   ├── libc.c / libc.h          # Freestanding C runtime (string, memory, formatting)
│   ├── auth.c / auth.h          # User/group auth, /etc/passwd parser, session state
│   ├── sha256.c / sha256.h      # Salted cryptographic SHA-256 hashing
│   ├── pci.c / pci.h            # PCI bus scanner and device configuration
│   ├── driver_rtl8139.c         # Realtek RTL8139 Fast Ethernet NIC driver
│   ├── driver_e1000.c           # Intel 82540EM Gigabit Ethernet NIC driver
│   ├── driver_wifi.c            # Wi-Fi detection and diagnostic framework
│   ├── net_driver.c / .h        # Unified network device abstraction layer
│   ├── net_stack.c / .h         # Freestanding TCP/IP, ARP, ICMP, DHCP, DNS stack
│   ├── net_config.c / .h        # Persistent /etc/network.conf parser and writer
│   ├── net_gui.c / .h           # Interactive text-mode 'network' configuration GUI
│   ├── http.c / http.h          # HTTP streaming client (wget, curl)
│   ├── git.c / git.h            # Native Smart HTTP Git clone implementation
│   ├── graphics.c / .h          # 1024x768 32-bit TrueColor engine & BMP coder
│   ├── mouse.c / .h             # PS/2 mouse driver, IRQ12 parser, arrow cursor
│   ├── wm.c / .h                # Reusable multi-window manager (Z-order, focus)
│   ├── theme.c / .h             # 4 Procedural wallpapers & desktop theme engine
│   ├── login.c / .h             # Graphical login & forced password change screen
│   ├── app_notepad.c / .h       # Notepad text editor application (AarFS I/O)
│   ├── app_paint.c / .h         # Paint drawing application (BMP save/load)
│   ├── app_filemgr.c / .h       # File Explorer directory navigation & app launcher
│   ├── app_snake.c / .h         # Windowed Snake arcade game
│   ├── app_settings.c / .h      # Control Center settings application
│   ├── sh_runner.c / .h         # Shell script runner (run / sh interpreter)
│   ├── deb_tool.c / .h          # Debian package manager (dpkg / deb tool)
│   ├── vfs.c / .h               # Virtual Filesystem layer (file descriptors 0..15, mounts)
│   ├── elf_loader.c / .h        # Native ELF32 binary inspector (headers, segments, sections)
│   ├── rpm_tool.c / .h          # Linux Red Hat RPM package inspector (Lead & Header tags)
│   ├── pe_tool.c / .h           # Windows PE32 / Wine compatibility inspector (IAT, DLLs)
│   ├── sysinfo.c / .h           # Unified system diagnostics (sysinfo, hwinfo, compat)
│   └── desktop.c / .h           # Desktop shell (Fedora top panel, XP taskbar, start menu)
│
├── micropython-master/          # Upstream MicroPython core headers and uzlib
│
└── tests/                       # Automated QEMU Regression Test Suite
    ├── qemu_harness.py          # Subprocess manager for QEMU with COM1/COM2 monitors
    ├── command_metadata.py      # Automated command registry parser
    ├── aarfs_tool.py            # AarFS file injector & package/binary format generator
    ├── test_command_registry.py # Shell command metadata and registry validation
    ├── test_aarfs_qemu.py       # Filesystem integrity and reboot persistence tests
    ├── test_editor_qemu.py      # Editor navigation, buffer, and save tests
    ├── test_help_calc_qemu.py   # Help index and calculator parser tests
    ├── test_calc_host_qemu.py   # Calculator host launcher handshake tests
    ├── test_users_permissions_qemu.py # User accounts, auth, permissions, sessions
    ├── test_native_python_qemu.py     # Native MicroPython execution tests
    ├── test_networking_qemu.py        # 17-scenario hardware, DHCP, ping, HTTP, Git test
    ├── test_desktop_complete_qemu.py  # 8-stage complete desktop acceptance test suite
    ├── test_script_dpkg.py      # Shell script runner & Debian package tool test
    ├── test_vfs_elf_qemu.py     # VFS operations & ELF32 binary inspection tests
    ├── test_rpm_pe_sysinfo_qemu.py    # RPM, Windows PE32 & system diagnostics tests
    ├── test_desktop_qemu.py     # Legacy Mode 13h baseline desktop test
    ├── test_python_bridge.py    # COM2 host protocol framing and unit tests
    └── test_python_bridge_qemu.py     # COM2 guest-host integration tests
```

---

## Build and Launch Instructions

### Prerequisites
1. **NASM** (v2.15 or later) - Assembler for bootloader and kernel.
2. **MinGW-w64 32-bit GCC** (i686-w64-mingw32) - Freestanding C compiler.
3. **Python 3.8+** - Virtual disk formatting and automated test execution.
4. **QEMU** (`qemu-system-i386`) - x86 PC emulation.

### 1. Build Native MicroPython & C Subsystem
From an MSYS2 / MinGW-w64 shell:
```bash
cd micropy32
make clean && make -j4
```
*Produces `micropy32.bin` (792 KiB freestanding binary).*

### 2. Assemble Kernel and Build Disk Image
In PowerShell (Windows):
```powershell
# Build kernel32.bin and create/update aarambh-v04-pm32-fs.img without launching QEMU:
powershell -ExecutionPolicy Bypass -File .\build-protected.ps1 -NoLaunch
```
*Checks loader limit (32,256 bytes) and writes `boot32.bin`, `kernel32.bin`, and `micropy32.bin` (at LBA 132000) into the disk image.*

### 3. Launching Aarambh OS in QEMU

**Standard Launch (Native Mode with Networking)**:
```powershell
powershell -ExecutionPolicy Bypass -File .\build-protected.ps1
```

**Launch with RTL8139 Fast Ethernet**:
```powershell
& "C:\Program Files\qemu\qemu-system-i386.exe" -drive "file=aarambh-v04-pm32-fs.img,format=raw,if=ide,index=0" -netdev user,id=net0 -device rtl8139,netdev=net0 -boot c
```

**Launch with Intel e1000 Gigabit Ethernet**:
```powershell
& "C:\Program Files\qemu\qemu-system-i386.exe" -drive "file=aarambh-v04-pm32-fs.img,format=raw,if=ide,index=0" -netdev user,id=net0 -device e1000,netdev=net0 -boot c
```

**Launch with COM2 Host Python Bridge**:
```powershell
# Window 1: Start host helper
py -3 host_python_bridge.py --port 40242

# Window 2: Start QEMU with COM2 connected
powershell -ExecutionPolicy Bypass -File .\build-protected.ps1 -PythonBridge -PythonBridgePort 40242
```

---

## Automated Testing and Verification

Aarambh OS includes an automated QEMU integration test harness in `tests/`. The test suite boots ephemeral copies of the virtual disk in QEMU, interacts with the shell via keyboard injection, inspects COM1 serial diagnostics, and verifies persistence across reboot cycles.

Run the test suites with Python 3:
```powershell
# Core Command Registry & Parser
py -3 tests\test_command_registry.py

# AarFS Filesystem CRUD & Reboot Persistence
py -3 tests\test_aarfs_qemu.py

# Interactive Editor Navigation & Save Boundaries
py -3 tests\test_editor_qemu.py

# Help System & Calculator Precedence
py -3 tests\test_help_calc_qemu.py

# User Management, Passwords & Permission Enforcement
py -3 tests\test_users_permissions_qemu.py

# Native MicroPython On-Metal Execution
py -3 tests\test_native_python_qemu.py

# Full Networking Suite (17 Scenarios: RTL8139, e1000, DHCP, Ping, Wget, Git)
py -3 tests\test_networking_qemu.py

# Complete Desktop Acceptance Suite (1024x768, Mouse, Windows, Login, Persistence, Reboot, Scripts, dpkg)
py -3 tests\test_desktop_complete_qemu.py

# Shell Script Runner & Debian Package Manager (run, sh, dpkg -I/-c/-i/-l)
py -3 tests\test_script_dpkg.py

# Virtual Filesystem & ELF32 Binary Inspection (vfs, elfinfo, elf)
py -3 tests\test_vfs_elf_qemu.py

# Red Hat RPM, Windows PE32 / Wine-Check & System Diagnostics (rpm, peinfo, sysinfo, hwinfo, compat)
py -3 tests\test_rpm_pe_sysinfo_qemu.py

# Legacy Mode 13h Baseline Desktop Test
py -3 tests\test_desktop_qemu.py

# COM2 Host Python Bridge Integration
py -3 tests\test_python_bridge.py
py -3 tests\test_python_bridge_qemu.py
py -3 tests\test_calc_host_qemu.py
```

### Verified Test Results Summary
- `test_rpm_pe_sysinfo_qemu.py`: **100% PASS** (8/8 stages). Verified RPM default inspection, file contents query (`-ql`), requirements query (`-qR`), Windows PE32 headers/sections/subsystem parsing, IAT resolution of KERNEL32/USER32 symbols, `wine-check` alias, full unified `sysinfo` telemetry, live PCI bus scan (`hwinfo`), and categorical capability matrix (`compat`).
- `test_vfs_elf_qemu.py`: **100% PASS** (3/3 stages). Verified VFS file operations, static freestanding ELF32 inspection, dynamic Linux ELF32 `PT_INTERP` detection, alias `elf`, and invalid file error containment.
- `test_desktop_complete_qemu.py`: **100% PASS** (8/8 stages). Verified 1024x768 TrueColor graphics switch, PS/2 mouse packet streaming and window dragging, login screen authentication, forced password change lifecycle, AarFS salted SHA-256 persistence in `/etc/passwd`, reboot survival, shell script execution with variable expansion, and Debian package installation and status database tracking.
- `test_script_dpkg.py`: **100% PASS** (5/5 tests). Verified comments, variables (`$APP`, `$USER`, `$?`), exit code, `dpkg -I`, `dpkg -c`, `dpkg -i` payload extraction, path traversal defense, and `dpkg -l`.
- `test_command_registry.py`: **100% PASS** (5/5 tests). Every shell command has validated help metadata, argument syntax, category, and real handler.
- `test_aarfs_qemu.py`: **100% PASS**. Verified directory creation, file write, file read, append, rename, copy, deletion, and reboot persistence.
- `test_editor_qemu.py`: **100% PASS**. Verified cursor navigation, horizontal scrolling, 16 KiB buffer limits, and AarFS persistence.
- `test_native_python_qemu.py`: **100% PASS** (6/6 tests). Verified Hello World, arithmetic evaluation, variables, exception containment, and `py native` dispatch.
- `test_users_permissions_qemu.py`: **100% PASS** (14/14 stages). Verified `root` vs unprivileged `aarambh` permissions, silent password input, salted SHA-256 verification, and `chmod`/`chown` enforcement.
- `test_networking_qemu.py`: **100% PASS** (17/17 scenarios). Verified RTL8139 and e1000 drivers, automatic DHCP configuration, ICMP ping, DNS resolution, `network` GUI, `wget` streaming, `curl`, and `git clone`.
- `test_desktop_qemu.py`: **100% PASS**. Verified Mode 13h switch, 2D graphics rendering, wallpaper selection, AarFS `/etc/desktop.conf` persistence, reboot survival, and clean terminal restoration.

---

## Security Boundaries and Technical Limitations

While Aarambh OS includes functional user authentication and permission checking, it is an educational hobby OS and possesses known architectural boundaries:

1. **Hardware Memory Isolation (Paging)**: The kernel operates in a flat 32-bit protected mode. Although GDT user selectors (`0x1B`, `0x23`) and IDT `int 0x80` syscall gates exist, hardware memory paging (`CR0.PG` / `CR3`) and Ring 3 TSS task isolation are not yet active. Physical RAM is currently shared across code paths.
2. **Scheduling Model**: Single-tasking cooperative execution. There is no preemptive, timer-driven multi-tasking scheduler.
3. **Filesystem Boundaries**: AarFS supports a fixed maximum of 2,048 directory entries and 16,384 bytes (16 KiB) per file.
4. **Binary Execution Layers**: ELF, RPM, and PE binaries are fully inspected natively. Loading freestanding binaries into memory is supported; execution of Linux glibc binaries and Windows Win32 binaries requires their respective syscall/API emulation layers (Roadmap Phases 4 & 5).
5. **HTTPS / TLS Limitations**: Insecure HTTPS is strictly rejected. True TLS 1.3 encryption and root CA certificate store loading remain future roadmap items.
6. **Wi-Fi Hardware Support**: The Intel Centrino Advanced-N 6205 driver framework is in place and detects PCI hardware, but operational Wi-Fi transmission requires loading binary microcode firmware blobs into the card.

---

## Roadmap: Implemented vs Future Work

| Subsystem | Implemented / Verified Today (v0.4) | Planned Architecture (Future) |
| :--- | :--- | :--- |
| **Kernel Core** | 32-bit Protected Mode, GDT, IDT, PIC, PIT, bump allocator, COM1/COM2 | Paging (CR3/CR0.PG), Ring 3 TSS task separation, preemptive scheduler |
| **Filesystem & VFS** | AarFS alternating superblocks, VFS file descriptors 0..15, mount table | Ext2/Ext4, FAT32 mount in PM32, page cache, storage block layer |
| **Binary Compatibility** | ELF32 inspector (`elfinfo`), RPM inspector (`rpm`), PE32 inspector (`peinfo`) | ELF32 process loader, Linux `int 0x80` syscall layer, Wine Win32 emulation |
| **Users & Security**| UIDs, GIDs, `/etc/passwd`, salted SHA-256, `su`, `chmod`, `chown` | Pluggable authentication modules (PAM), encrypted user home directories |
| **Python & Scripting**| Native 32-bit MicroPython at LBA 132000, `sh` script runner (`run`) | Python REPL mode in terminal, expanded standard library modules |
| **Networking** | RTL8139 & e1000 drivers, DHCP, DNS, ICMP ping, HTTP (`wget`/`curl`), Git | Intel 6205 Wi-Fi microcode loader, WPA2-PSK supplicant, TLS 1.3 engine |
| **Desktop GUI** | 1024x768 32-bit TrueColor BGA, multi-window manager, PS/2 mouse, 5 apps | Hardware acceleration (Mesa/DRI), compositing window manager, FreeType fonts |

---

## License and Attribution

- **Project Lead & Author**: Maintainer of the Aarambh OS Project.
- **MicroPython Core**: Copyright (c) 2013-2024 Damien P. George. MicroPython is licensed under the MIT License.
- **uzlib**: Embedded decompression library derived from uzlib / zlib.
- **License**: Aarambh OS source code and documentation are released under the [MIT License](LICENSE).
