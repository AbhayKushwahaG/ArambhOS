# Aarambh OS — Changelog & Feature Status

All notable changes to the Aarambh OS project are documented in this file.

---

## [0.4.3] - 2026-10-28 (Core Architecture, VFS, Multi-OS Binary Inspection & Unified Diagnostics)

### Added
- **Virtual Filesystem (VFS) Layer (`micropy32/vfs.c`, `micropy32/vfs.h`)**:
  - Abstract file descriptor table supporting 16 open files (descriptors 0..15).
  - Pre-allocated POSIX standard streams: `fd 0` (`/dev/stdin`), `fd 1` (`/dev/stdout`), `fd 2` (`/dev/stderr`).
  - Standardized VFS functions: `vfs_init`, `vfs_open`, `vfs_read`, `vfs_write`, `vfs_close`, `vfs_stat`, `vfs_read_entire_file`, `vfs_write_entire_file`.
  - Mount table supporting 4 active filesystem mounts; mounts root (`/`) to AarFS v0.4 through kernel API dispatch, ready for future `/dev`, `/proc`, and FAT32 removable media mounts.
  - Path normalization engine (`vfs_normalize_path`) handling relative paths, duplicate slash collapsing (`//`), and root directory resolution.
- **Native ELF32 Binary Inspection & Loader Foundation (`micropy32/elf_loader.c`, `micropy32/elf_loader.h`)**:
  - Full parser and structural validator for Standard 32-bit x86 ELF executables (`Elf32_Ehdr`, `Elf32_Phdr`, `Elf32_Shdr`).
  - Validates ELF magic (`0x7F 'E' 'L' 'F'`), Class (ELFCLASS32 vs ELFCLASS64), Endianness (ELFDATA2LSB), Machine (`EM_386`), Type (`ET_EXEC`, `ET_DYN`, `ET_REL`), and entry point virtual address.
  - Program Header segment scanner identifying `PT_LOAD` segments, memory footprints, and `PT_INTERP` dynamic linker paths (e.g. `/lib/ld-linux.so.2`).
  - Section Header scanner resolving section names (`.text`, `.rodata`, `.data`, `.bss`, `.shstrtab`).
  - Shell commands: `elfinfo <file.elf>` and alias `elf <file.elf>`.
  - Honest compatibility assessment distinguishing between statically linked freestanding x86 binaries (directly loadable) vs dynamically linked binaries requiring Linux glibc & POSIX syscall emulation.
- **Linux RPM Package Inspector (`micropy32/rpm_tool.c`, `micropy32/rpm_tool.h`)**:
  - Native parser for Red Hat RPM packages (`.rpm`).
  - Validates RPM Lead structure (`0xedabeedb`, major/minor version, binary package type, arch ID, OS ID).
  - Validates RPM Header structure (`0x8eade801`, index records, data store) with big-endian unaligned byte reading.
  - Extracts and formats metadata tags: Name (`1000`), Version (`1001`), Release (`1002`), Summary (`1004`), License (`1014`), Group (`1016`), Architecture (`1022`), Dependencies/Requirements (`1049`), and Archive File Lists (`1117`/`1027`).
  - Shell commands: `rpm <file.rpm>`, `rpm -qi <file.rpm>`, `rpm -ql <file.rpm>`, `rpm -qR <file.rpm>`, `rpm -qp <file.rpm>`.
  - Architecture compatibility diagnostics identifying x86 32-bit/noarch compatibility vs 64-bit blocks.
- **Windows PE32 Executable & Wine Compatibility Inspector (`micropy32/pe_tool.c`, `micropy32/pe_tool.h`)**:
  - Native parser for Microsoft Windows PE32 executable files (`.exe`, `.dll`).
  - Validates MS-DOS MZ header (`0x5A4D`) and extracts NT header offset `e_lfanew`.
  - Validates NT PE signature (`PE\0\0`), COFF Machine (`IMAGE_FILE_MACHINE_I386` = `0x014C`), Characteristics (Executable vs DLL), and Optional Header PE32 magic (`0x10B`).
  - Discovers ImageBase (e.g. `0x00400000`), AddressOfEntryPoint, Subsystem (Windows GUI vs Windows CUI Console vs Native), and Section Table (`.text`, `.rdata`, `.data`, `.rsrc`).
  - Parses Import Directory Table (Directory 1) using RVA-to-file-offset translation; enumerates imported DLLs (`KERNEL32.dll`, `USER32.dll`, `GDI32.dll`) and parses Import Name Tables to resolve imported Win32 API functions (`ExitProcess`, `GetStdHandle`, `WriteFile`, `MessageBoxA`).
  - Shell commands: `peinfo <file.exe>` and alias `wine-check <file.exe>`.
  - Provides Wine-style architectural feasibility analysis for Win32 console vs GUI execution.
- **Unified System Diagnostics & Compatibility Command (`micropy32/sysinfo.c`, `micropy32/sysinfo.h`)**:
  - `sysinfo`: Comprehensive system specifications report covering CPU mode, PIT timer & uptime, physical RAM layout (IVT, Kernel, C Subsystem, MicroPython GC Heap, VESA Backbuffer), storage/VFS status, display adapter & video mode, active network adapter & IP/DHCP/DNS, and supported execution runtimes.
  - `hwinfo`: Low-level hardware inventory via live PCI bus scanning (`pci_scan()`), enumerating PCI device IDs, vendor IDs, class codes, IRQs, and MMIO/IO Base Address Registers (BARs).
  - `compat`: Honest software compatibility matrix displaying categorized technology statuses (`[FUNCTIONAL]` vs multi-phase `[ROADMAP]` targets).
- **Automated Verification Suites**:
  - `tests/test_vfs_elf_qemu.py`: Validates VFS file operations, static freestanding ELF32 parsing, dynamic Linux ELF32 interp parsing, and error containment.
  - `tests/test_rpm_pe_sysinfo_qemu.py`: Validates RPM metadata/files/requirements parsing, Windows PE32 headers/sections/IAT resolution, `sysinfo`, `hwinfo`, and `compat` commands.
  - Total automated test suites: **16/16 suites passing 100%**.

---

## [0.4.2] - 2026-10-28 (Complete Desktop Environment, Native Applications, Login, Customization, Script & Package Support)

### Added
- **High-Resolution TrueColor Graphics Engine (`micropy32/graphics.c`, `micropy32/graphics.h`)**:
  - Upgraded from 320x200 proof-of-concept to full high-resolution **1024x768 32-bit TrueColor** (16.7 million colors) via Bochs/QEMU BGA (I/O ports `0x01CE`/`0x01CF`, PCI BAR0 linear framebuffer at `0xFD000000`).
  - Double-buffered backbuffer allocated safely at physical RAM `0x00800000` (8 MiB mark), completely eliminating memory collisions with the kernel and MicroPython binary.
  - Reusable graphics primitives: `gfx_put_pixel`, `gfx_fill_rect`, `gfx_draw_rect`, `gfx_draw_line` (Bresenham), `gfx_draw_rect_3d`, `gfx_gradient_v`, `gfx_gradient_h`, `gfx_draw_text`, `gfx_draw_text_scaled`, `gfx_draw_icon`.
  - Windows BMP image encoder (`gfx_save_bmp`) and decoder (`gfx_load_bmp`) supporting direct export/import with AarFS.
  - Hardware clipping rectangle stack (`gfx_set_clip`, `gfx_reset_clip`) preventing window contents from painting outside their bounds.
- **Interrupt-Driven PS/2 Mouse Subsystem (`kernel/kernel32.asm`, `micropy32/mouse.c`, `micropy32/mouse.h`)**:
  - Unmasked IRQ2 (cascade) and IRQ12 on 8259 Slave PIC (`0xEF`).
  - Registered low-level `irq_mouse` interrupt vector 44, streaming packets into a 64-byte kernel ring buffer with port `0x64` AUX status check.
  - 3-byte packet synchronization (bit 3 check), relative coordinate decoding, boundary clamping, and single/double-click/hold detection.
  - Crisp 12x18 arrow pointer with black border, pure white interior, and drop shadow.
- **Reusable Multi-Window Manager (`micropy32/wm.c`, `micropy32/wm.h`)**:
  - Complete multi-window manager supporting up to 16 concurrent windows with Z-ordering, active focus, title-bar dragging with clamping, and window controls (Minimize `[-]`, Maximize `[+]`, Close `[X]`).
  - Active vs inactive title bar styling: Windows XP Luna Blue gradient for focused windows vs muted slate gray for inactive windows.
  - Per-window callbacks: `on_paint`, `on_click`, `on_mouse_move`, `on_key`, `on_close`.
- **Graphical Login Screen (`micropy32/login.c`, `micropy32/login.h`)**:
  - Centered login dialog box with drop shadow, Aarambh OS branding, and user account selector (`Gentix` or `root`).
  - Masked password entry (`*`), blinking cursor, and error reporting.
  - Pre-configured bootstrap user `Gentix` with initial default password `password`.
  - Forced first-time password change dialog requiring new password and confirmation, validated and persisted into AarFS `/etc/passwd` with 16-character salt and SHA-256 hash.
- **Five Core Native Applications**:
  - **Notepad (`micropy32/app_notepad.c`, `app_notepad.h`)**: Multi-line visual text editor with AarFS Open/Save/Save-As support, text navigation, character/line counters, and status bar.
  - **Paint (`micropy32/app_paint.c`, `app_paint.h`)**: 480x320 drawing canvas with Pen, Brush, Eraser, Line, and Rectangle tools, brush size selection (1, 2, 4, 8px), 16-color palette picker, and BMP format save/load to AarFS.
  - **File Explorer (`micropy32/app_filemgr.c`, `app_filemgr.h`)**: Interactive AarFS filesystem navigation with directory tree, file icons, size, permissions, and launch associations (opens `.txt`/`.sh` in Notepad, `.bmp` in Paint).
  - **Snake Game (`micropy32/app_snake.c`, `app_snake.h`)**: Windowed arcade game with 20x20 grid, real-time score tracking, food generation, self-collision detection, and both keyboard arrow and on-screen mouse D-Pad controls.
  - **Settings App (`micropy32/app_settings.c`, `app_settings.h`)**: Control center featuring 4 categories: Appearance (wallpaper/theme selection with live thumbnail preview), Display resolution, User Accounts (password modification), and System Diagnostics.
- **Desktop Shell & Customization Engine (`micropy32/desktop.c`, `micropy32/theme.c`)**:
  - **Fedora GNOME Top Panel**: 28px dark slate panel with "Activities" button, live digital clock (`HH:MM:SS`), active window title, and user/network status pills.
  - **Windows XP Luna Taskbar**: 36px bottom taskbar with "Start" button, quick-launch icons, open window taskbar buttons, and system tray clock.
  - **Windows XP Start Menu**: Two-column popup menu with user profile banner, application launcher list, places shortcuts, Log Off, and Exit to Shell.
  - **Desktop Icons**: Vector icons on desktop (My Computer, File Explorer, Notepad, Paint, Snake Game, Settings, Terminal) with double-click execution.
  - **4 Procedural Wallpapers**: `XP Bliss` (azure sky + sun radiance + rolling green hills), `GNOME Adwaita` (indigo/slate polygonal gradient), `Classic Teal` (Windows 95/XP teal with dotted grid), `Charcoal Dark`.
  - Persistent `/etc/desktop.conf` configuration in AarFS across reboots.
- **Shell Script Runner (`micropy32/sh_runner.c`, `micropy32/sh_runner.h`)**:
  - New commands: `run <script.sh>` and `sh <script.sh>`.
  - Supports comments (`#`), variable assignments (`VAR=val`), variable expansion (`$VAR`, `$?`, `$USER`, `$HOME`), sequential execution, and line-numbered error reporting.
- **Debian Package Manager (`micropy32/deb_tool.c`, `micropy32/deb_tool.h`)**:
  - New commands: `dpkg` and `deb`.
  - Features `-I` (inspect package metadata and control information), `-c` (list archive contents), `-i` (install package payload into AarFS, enforce path traversal defense, and update `/var/lib/dpkg/status`), `-l` (list installed packages in Debian status format), and `-r` (remove package).
  - Transparently documents binary compatibility boundaries regarding native Linux ELF execution versus Aarambh runtime scripts.
- **Automated Acceptance Test Suite (`tests/test_desktop_complete_qemu.py`)**:
  - 8-stage comprehensive test verifying mode switching, PS/2 mouse packet processing, login lifecycle, password forced change, AarFS salted SHA-256 persistence, reboot survival, shell script execution, and Debian package management.

---

## [0.4.1] - 2026-10-27 (Graphical Desktop & Customization Foundation)

### Added
- **Real-Mode Video Mode Switch Trampoline (`kernel/kernel32.asm`)**:
  - Configured GDT descriptors for 16-bit protected mode code (`0x28`) and data (`0x30`).
  - Added `kernel_set_video_mode(uint16_t mode)` allowing clean, BIOS-level switching between VGA Mode 13h (320x200 256-color graphics at `0x000A0000`) and VGA Mode 03h (80x25 text mode at `0x000B8000`).
  - Preserved terminal console: exiting the desktop restores the text buffer and returns directly to the shell prompt.
- **Freestanding 2D Graphics Engine (`micropy32/graphics.c`, `micropy32/graphics.h`)**:
  - Double-buffered rendering via 64,000-byte backbuffer with fast 32-bit dword blitting (`gfx_flip()`).
  - Reusable graphics primitives: `gfx_put_pixel`, `gfx_draw_line` (Bresenham), `gfx_draw_rect`, `gfx_fill_rect`, `gfx_draw_rect_3d` (sunken/raised bevels), `gfx_gradient_h`, `gfx_gradient_v`.
  - Built-in 8x8 bitmap font covering ASCII 32..126 (`micropy32/font8x8.h`) for transparent and opaque text rendering.
  - Programmable VGA DAC 256-color palette (`gfx_init_palette()`): includes Windows XP Luna Blues, Bliss sky/grass greens, GNOME slate, Classic Teal, and 3D UI bevel grays.
  - Built-in 16x16 application icons for Terminal, Settings, Files, and Editor.
- **PS/2 Mouse Subsystem (`micropy32/mouse.c`, `micropy32/mouse.h`)**:
  - Intel 8042 controller auxiliary device initialization and data stream enable.
  - Non-blocking 3-byte packet polling and coordinate updates with boundary clamping.
  - Classic 11x16 arrow cursor rendering over backbuffer.
- **Desktop Environment & Windows XP Window Manager (`micropy32/desktop.c`, `micropy32/desktop.h`)**:
  - **Fedora GNOME-inspired Top Panel**: Dark slate panel (y=0..13) with "Aarambh" pill button, live digital clock (`HH:MM:SS` from PIT timer ticks), and network/user indicators.
  - **Windows XP-Style Window**: Classic Luna Blue title bar gradient with drop-shadow title text, 2-pixel 3D frame, and XP control buttons (Minimize `[-]`, Maximize `[+]`, Close `[X]`).
  - **Wallpaper Customization Engine**:
    - 4 selectable procedural wallpapers: `XP Bliss` (procedural sky gradient + rolling green grass hills), `GNOME Adwaita` (indigo/slate gradient), `Classic Teal` (Windows 95/XP teal), and `Charcoal Dark` (minimalist dark theme).
    - Live wallpaper preview thumbnail box (52x30 pixels) rendering inside the window.
    - Persistent configuration: saves active wallpaper and theme to `/etc/desktop.conf` on AarFS with reboot persistence.
  - Interactive window dragging by title bar, mouse clicking on wallpaper items/buttons, and keyboard shortcuts (`1..4` select, `W` cycle, `Enter` apply, `Esc`/`Q` exit to terminal).
- **New Shell Commands**:
  - `desktop` (Alias: `gui`): Launches the graphical desktop environment.
- **Automated Verification**:
  - Added `tests/test_desktop_qemu.py` verifying mode switch, wallpaper selection, AarFS persistence in `/etc/desktop.conf`, reboot survival, and clean terminal restoration.

---

## [0.4.0] - 2026-10-26 (Networking, Git & User Management)

### Added
- Realtek RTL8139 Fast Ethernet and Intel 82540EM (e1000) Gigabit Ethernet drivers.
- In-kernel TCP/IP stack: ARP cache, IPv4 routing, ICMP ping, UDP, DHCP client, DNS resolver, and RFC 793 TCP engine.
- HTTP client (`wget` and `curl`) with streaming to AarFS files and terminal output.
- Native Git Smart HTTP client (`git clone`) with packfile inflation via embedded `uzlib`.
- Multi-user authentication subsystem with unique numeric UIDs/GIDs, `/etc/passwd`, `/etc/group`, silent password entry, and salted SHA-256 password hashing.
- AarFS Unix-style file permissions (`chmod`, `chown`, `chgrp`, `stat`) and user home directory isolation (`/root`, `/home/<user>`).
- Native 32-bit MicroPython runtime at LBA 132000 (`python <script.py>`) with `aarambh` system call and networking module.

---

# Feature Status Matrix

| Subsystem | Feature | Status | Notes |
| :--- | :--- | :--- | :--- |
| **Boot & CPU** | BIOS MBR Bootloader | **Implemented** | 512 bytes, loads 63 kernel sectors via INT 13h extensions |
| | 32-bit Protected Mode | **Implemented** | Flat 4 GiB GDT, IDT with 8259 PIC remap, PIT timer, PS/2 keyboard |
| | Real-Mode Transition | **Implemented** | 16-bit GDT descriptors & trampoline for BIOS INT 10h calls |
| | Paging (CR0.PG/CR3) | *Planned* | Required for future Ring 3 hardware process memory isolation |
| **Graphics** | VGA Mode 13h (320x200x256) | **Implemented** | Standard framebuffer at `0x000A0000`, 256 programmable DAC colors |
| | Double Buffering | **Implemented** | 64,000-byte backbuffer with fast 32-bit dword blitting (`gfx_flip`) |
| | 2D Drawing Primitives | **Implemented** | Pixels, lines, rects, 3D bevels, horizontal & vertical gradients |
| | Bitmap Typography | **Implemented** | 8x8 font (ASCII 32..126) with opaque and transparent rendering |
| | VESA VBE (High-Res) | *Planned* | 640x480 / 800x600 32-bit linear framebuffer planned for desktop |
| **Input** | PS/2 Keyboard Driver | **Implemented** | IRQ1 ring buffer, extended key mapping, non-blocking check |
| | PS/2 Mouse Driver | **Implemented** | Intel 8042 auxiliary device init, 3-byte packet stream polling |
| | Mouse Cursor | **Implemented** | 11x16 classic arrow cursor rendered over backbuffer |
| **Desktop / GUI** | Fedora GNOME Top Panel | **Implemented** | 14px dark slate panel, Activities pill, live clock, status pills |
| | Windows XP Window Style | **Implemented** | Luna Blue gradient title bar, 3D border, `[-]`, `[+]`, `[X]` buttons |
| | Window Dragging & Focus | **Implemented** | Dragging by titlebar, bounds clamping to desktop area |
| | Wallpaper Customization | **Implemented** | 4 procedural wallpapers (Bliss, Adwaita, Teal, Dark) |
| | Settings & Preview | **Implemented** | Live wallpaper preview thumbnail box, Apply button |
| | Persistent Wallpaper | **Implemented** | Saved to `/etc/desktop.conf` in AarFS, survives reboot |
| **Filesystem** | AarFS v1.0 Storage Engine| **Implemented** | Alternating superblocks, double-buffered tables, 16 KiB slots |
| | File Permissions | **Implemented** | POSIX-style mode bits (0644, 0755, 0600), owner UID & group GID |
| | Data Persistence | **Implemented** | Verified 100% persistent across QEMU reboot cycles |
| **Terminal Shell**| Dual-Dispatch Shell | **Implemented** | 25 assembly built-in commands + 23 C subsystem commands |
| | Command Registry & Help | **Implemented** | Indexed, paged help generated directly from command registry |
| **Networking** | RTL8139 / e1000 Drivers | **Implemented** | Automatic PCI detection, packet TX/RX ring buffers |
| | TCP/IP Stack & DHCP | **Implemented** | ARP, IPv4, ICMP, UDP, DHCP, DNS, RFC 793 TCP state machine |
| | Internet Clients | **Implemented** | `wget`, `curl`, and Smart HTTP `git clone` with `uzlib` inflate |
