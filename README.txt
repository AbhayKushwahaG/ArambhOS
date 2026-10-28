AARAMBH OS v0.4 - README
========================

Aarambh is a hobby x86 BIOS operating system with two separate variants:

1. PM32: a 32-bit protected-mode kernel with its own persistent AarFS disk
   (aarambh-v04-pm32-fs.img). This README's editor, indexed help, calculator
   and host-assisted Python features live here.
2. Legacy: the original 16-bit real-mode kernel and FAT32 image built by
   build.ps1. It remains separate and is not modified by the PM32 build.

Safety
------
- Builds and tests use disposable virtual disk images only.
- The PM32 build writes only aarambh-v04-pm32-fs.img in this folder.
- The legacy build writes only aarambh-v04-fat32-rw.img.
- No physical USB drive or 2 TB HDD is formatted, partitioned or attached by
  these scripts. Do not point QEMU at a physical drive.

What works
----------
- BIOS boot of the PM32 kernel from a 128 MiB raw virtual disk.
- GDT/IDT, remapped PIC, PIT timer, PS/2 keyboard, VGA text, E820 memory map,
  COM1 diagnostics and a bounded bump allocator.
- AarFS: persistent files and nested directories with CRC-protected alternating
  superblocks and duplicate directory tables.
- Full-screen editor with real AarFS saves, dirty tracking, scrolling, cursor
  movement, F1 help, empty/long files and reboot persistence.
- Indexed, paged, colour help and a beginner guide generated from one command
  registry shared with the shell dispatcher.
- Built-in signed 32-bit integer calculator with parentheses and precedence.
- Native 32-bit MicroPython runtime: genuine on-metal script execution inside Aarambh OS
  ('python <file.py>' or 'py native <file.py>') without host or external OS assistance.
- Linux-inspired native user and group management ('useradd', 'userdel', 'usermod',
  'users', 'id', 'groupadd', 'groupdel', 'groups') with persistent /etc/passwd and /etc/group.
- Local authentication and session system with silent password input, salted SHA-256
  cryptographic password hashing, 'su', 'passwd', 'login', 'logout', and session-aware prompt.
- AarFS ownership and Unix-style permissions (UID, GID, mode bits) enforced on 'read',
  'cat', 'scribe', 'grow', 'rm', 'mv', and directory traversal, with 'chmod', 'chown', 'chgrp', 'stat'.
- Isolated user home directories (/root mode 0700, /home/<user> mode 0750) and 'cd ~' navigation.
- Kernel system call interface: GDT user selectors (0x1B, 0x23), IDT int 0x80 DPL=3 gate,
  and validated syscall dispatcher (SYS_GETUID, SYS_SETUID, SYS_GETGID, SYS_SETGID, SYS_WRITE).
- Native Python 'aarambh' module exposing getuid(), getgid(), getuser(), stat(), read(), and syscall().
- Genuine native networking subsystem with automatic PCI bus hardware detection for
  Intel Gigabit Ethernet (82579LM / e1000) and Realtek Fast Ethernet (RTL8139).
- Automatic Ethernet link detection and DHCP configuration (IP, netmask, gateway, DNS)
  at boot with bounded non-blocking timeouts.
- Complete in-kernel network stack: ARP cache, IPv4 routing, ICMP echo ping, UDP,
  DNS resolver, and RFC 793 compliant TCP protocol engine.
- Interactive 'network' console GUI (inspired by raspi-config) with 11 configuration
  menus, route viewing, ping diagnostics, static IP/DNS, and persistent /etc/network.conf.
- Honest Wi-Fi diagnostics detailing missing Intel Centrino Advanced-N 6205 microcode
  firmware blob without fabricating fake access points or connections.
- Real 'wget' command ('wget <URL>', 'wget -O <file> <URL>') streaming HTTP directly to AarFS.
- Real 'curl' command ('curl <URL>', 'curl -o <file> <URL>', 'curl -I <URL>') with console
  streaming, AarFS file persistence, and HEAD header inspection.
- Strict HTTPS rejection: mandates trusted root CA certificate store and rejects insecure
  connections without bypassing certificate validation.
- Native Git client ('git --version', 'git clone <url>') implementing Git Smart HTTP
  protocol, packfile inflate decompression via uzlib, tree traversal, and AarFS checkout.
- Native Python networking: 'aarambh' module exposing net_status(), gethostbyname(),
  ping(), http_get(), socket_connect(), socket_send(), socket_recv(), and socket_close().
- Host-assisted Python 3 through a loopback-only COM2 bridge (preserved for compatibility).
- Host-assisted Windows Calculator launch through the same bridge.

Build and launch
----------------
Requirements: NASM, Python 3, QEMU (qemu-system-i386.exe).

PM32 build only:

    powershell -NoProfile -ExecutionPolicy Bypass -File .\build-protected.ps1 -NoLaunch

If PowerShell execution policy already permits scripts:

    .\build-protected.ps1 -NoLaunch

The build assembles bootloader\boot.asm and kernel\kernel32.asm, then writes the
boot sector and kernel into aarambh-v04-pm32-fs.img. An existing valid AarFS
image is checked and preserved; it is never reformatted by a normal rebuild.
The loader may load up to 63 kernel sectors (32256 bytes); sector 64 is the
first AarFS metadata sector.

Launch PM32 with the normal disk:

    powershell -NoProfile -ExecutionPolicy Bypass -File .\build-protected.ps1

Launch PM32 with the Python/Calculator bridge on COM2:

    powershell -NoProfile -ExecutionPolicy Bypass -File .\build-protected.ps1 -PythonBridge -PythonBridgePort 40242
    py -3 host_python_bridge.py --port 40242

QEMU listens on 127.0.0.1:40242 for the COM2 chardev; the helper connects to
that listener and retries if QEMU is not up yet, so either start order works.
An absent helper cannot stall QEMU: commands report "helper unavailable" after
a short timeout. COM1 remains the kernel diagnostic log (pm32-debug.log when
launched with -PythonBridge).

Legacy 16-bit variant:

    .\build.ps1

It assembles kernel\kernel.asm into kernel.bin, uses the FAT32 image
aarambh-v04-fat32-rw.img, and launches QEMU. It is independent of the PM32
features described below.

Editor: `edit <file>`
---------------------
Opens an existing AarFS file in the current directory, or prepares a new file
that is created only when you save it.

Screen layout:
- rows 0..22: text (23 visible lines)
- row 23: message or the shortcut reminder
- row 24: status: EDIT <name>, size, line/column, READY/MODIFIED/SAVED

Keys:
- printable characters: insert at the cursor
- Enter: insert a newline
- Backspace: delete before cursor
- Delete: delete at cursor
- Left/Right: move one character
- Up/Down: move one line, preserving the column where possible
- Home/End: start/end of the current line
- PgUp/PgDn: move 20 lines
- Ctrl-S: save through the normal AarFS write/commit path
- Esc or Ctrl-Q: exit
- F1: editor keyboard help

Unsaved changes:
- The status shows MODIFIED and the message row shows the warning on exit.
- First Esc with unsaved changes warns; Ctrl-S saves; a second Esc discards.
- A newly named file with no save does not create a filesystem entry.

Real limits:
- 16384 bytes per file (AarFS limit). Inserting beyond it shows "Buffer full".
- CRLF is normalised to LF on load; tabs become one space; the Tab key inserts
  one space.
- Long lines scroll horizontally around the cursor.
- No undo, search, selection or clipboard yet.
- File commands operate in the current AarFS directory; use `cd` first.

Indexed help and guide
----------------------
    help
    help files
    help editor
    help python
    help calc
    help <command>
    guide

`help` prints a numbered category index (1..9). `help <number>` or
`help <category>` prints that category's syntax list. `help <command>` prints
one command page with purpose, example, expected result, errors, whether it
changes persistent data, and limits. All command documentation comes from
kernel\commands32.asm, the same registry used by the shell dispatcher.

Pager keys: Space = next page, I = return to the index, Q = quit to the shell.
Categories: Getting started, Shell and navigation, Files and directories, Text
editor, Python, Calculator, System information, Screen and session,
Troubleshooting.

Colours: headings cyan, syntax/commands yellow, body light grey, warnings
yellow, errors light red, success light green. The palette is readable on the
standard VGA text background.

Command reference
-----------------
Legend: <arg> is required, [arg] is optional. "Changes disk" means the command
can modify persistent AarFS data.

Getting started
- help [category|command]    Indexed command manual. No disk change.
- guide                      Beginner walkthrough. No disk change.
- identity                   Kernel name and mode. No disk change.

Shell and navigation
- say <text>                 Echo text. No disk change.
- cd <path>                  Change directory; supports /, ., .. Nested paths.
- pwd                        Print absolute path. Alias: where.

Files and directories (current directory only)
- ls                         List entries; directories end with /. Alias: peek.
- mkdir <name>               Create directory. Changes disk.
- touch <file>               Create empty file if absent. Alias: forge. Changes disk.
- read <file>                Print text. Alias: cat. No disk change.
- scribe <file> <text>       Replace with one line, creating if needed. Changes disk.
- grow <file> <text>         Append text. Changes disk.
- shift <old> <new>          Rename. Alias: mv. Changes disk.
- cp <src> <dst>             Copy a file. Changes disk.
- rm <name>                  Delete a file or empty directory. Alias: erase.
- df                         Logical capacity/used/free slot report.

Text editor
- edit <file>                Full-screen editor. Changes disk when saved.

Python
- python <file.py>           Run an AarFS script natively inside Aarambh using MicroPython.
- py <file.py>               Run script via Windows helper (or native fallback if helper unavailable).
- py native <file.py>        Explicitly run an AarFS script natively via in-guest MicroPython.
- py --version               Ask the helper for its real Python version.
- py help                    Python bridge usage.

Calculator
- calc                       Try the host helper; otherwise open calc>.
- calc <expression>          Built-in signed 32-bit integer calculation.
- calc host                  Launch Windows Calculator through the helper.
- calc help                  Usage.

System information
- mem                        BIOS conventional memory and bump-heap bytes left.
- ticks                      PIT ticks since boot (about 1 kHz).

Screen and session
- clear                      Clear VGA text. Alias: wipe.
- reboot                     Reboot the virtual machine.

Troubleshooting
- fault                      Deliberate breakpoint; halts until reboot.

Calculator
----------
The built-in engine is a bounded recursive-descent parser. It never calls
Python, the shell or machine code.

    calc 12 + 8                 -> 20
    calc (12 + 8) * 3           -> 60
    calc 2 + 3 * 4              -> 14
    calc 7 / 0                  -> calc: division by zero.
    calc 2 +                    -> calc: syntax error at column N.
    calc 2147483647 + 1         -> calc: integer overflow (signed 32-bit).

Operators: + - * / % and parentheses, with unary + and -. Signed 32-bit
integers only; no floating point, variables or functions. The `calc>` prompt
evaluates one expression per line; `q`, `quit` or `exit` returns to the shell.

Host behaviour: bare `calc` first asks the helper to open Windows Calculator.
Only if the helper is unavailable or refuses does it start the built-in
calculator. `calc host` never falls back; it reports the failure. The kernel
prints "launch confirmed" only after a valid status-0 response from the helper.

Python Execution: Native MicroPython & Host Bridge
--------------------------------------------------
Aarambh OS includes a genuine in-guest native Python runtime based on
freestanding 32-bit MicroPython (built in micropy32/). It executes Python
scripts directly inside Aarambh OS in 32-bit protected mode without Windows,
any host bridge, or external OS services.

Commands:
- python hello.py: Runs the script natively in Aarambh OS via MicroPython.
- py native hello.py: Explicitly runs the script natively via MicroPython.
- py hello.py: Attempts to contact the Windows helper over COM2; if the
  helper is unavailable, it automatically falls back to native MicroPython!

Native Runtime Architecture:
- Binary: Flat freestanding 32-bit binary (micropy32.bin) stored on the
  virtual disk starting at LBA sector 132,000 (past the AarFS 2048-slot area).
- Physical placement: Loaded by py_native_load into physical RAM at 0x00200000
  (2 MiB) on first run via ATA PIO.
- Memory: Uses an internal static 128 KiB GC heap and stdout directed through
  the Aarambh OS VGA text console and COM1 serial port.
- Supported features: Variables, arithmetic, control flow, functions, classes,
  built-in exceptions (e.g. ZeroDivisionError).
- Safety: Python exceptions are caught and reported cleanly without crashing
  or halting the kernel.

Host-assisted Python (Legacy Bridge)
------------------------------------
In addition to native MicroPython, the optional host bridge remains available:
scripts stored in AarFS can be sent over COM2 to host_python_bridge.py to run
under Windows CPython 3.

Typical session:
1. In Windows: `py -3 host_python_bridge.py --port 40242`
2. In another window: `powershell -NoProfile -ExecutionPolicy Bypass -File .\build-protected.ps1 -PythonBridge -PythonBridgePort 40242`
3. In Aarambh:
       touch hello.py
       scribe hello.py print('hello from Python')
       py hello.py

Expected output includes the real Python stdout and:
    Python exited with code 0.

Other commands:
    py --version        real host interpreter version
    py help             usage
    py <missing>.py     "script file not found" without contacting the helper
    py bad.txt          rejects non-.py names before reading

Transport and protocol
- COM2 at 0x2F8, IRQ3, 115200 8N1. QEMU listens on a loopback-only socket
  chardev (server=on, wait=off). The Windows helper is the TCP client and
  reconnects if QEMU or the link goes away. COM1 at 0x3F8 remains the debug log
  and is never used as the bridge.
- PING: "APNG" -> "APOK".
- Request: "APY1", version u8, request id u32, kind u8, name length u8,
  source length u32, name bytes, source bytes.
- Response: "APR1", version u8, request id u32, status u8, exit code i32,
  stdout length u32, stderr length u32, stdout bytes, stderr bytes.
- kind 1 = run script, 2 = Python version, 3 = launch allow-listed host app.
- Status: 0 OK, 1 nonzero exit, 2 timeout, 3 output limit, 4 rejected.

Limits and safety
- Script size: 16384 bytes (AarFS limit). Output: 8192 bytes combined.
- Helper execution timeout: 15 seconds by default (0.1..60 configurable).
  Kernel bridge deadlines: 0.5 s ping, 30 s run.
- Filenames: simple .py names, 1..39 characters, letters/digits/._- only.
- The helper validates magic, version, request id, name and lengths; rejects
  oversized or malformed frames; cleans its temporary directory; never uses
  eval/exec on protocol messages.
- Scripts run with the Windows account's permissions. This is not a sandbox.
- The helper never listens for network connections in normal use; it connects
  only to QEMU's loopback listener. QEMU binds 127.0.0.1, not all interfaces.
  Do not expose the port through a proxy or port forward. `--listen` is
  available only for tests/manual server mode.
- Host application launch is allow-listed to calc.exe only, with no shell.
- Unicode outside the VGA character set is converted to "?" by the helper so
  framing and the terminal stay intact.

AarFS filesystem notes (audited)
--------------------------------
- Layout: superblocks at LBA 64/65, duplicate 256-sector directory tables at
  66 and 322, data from LBA 578. 2048 entries x 16 KiB slots.
- Each file has a 32-sector fixed data slot and a double-buffered alternate
  slot; metadata commits write the inactive directory table first, then the
  other superblock. A failed table write remounts the previous metadata.
- Mount validates magic/version/geometry and both table and superblock CRC.
- ATA primary master PIO reads and writes retry up to 8 times with a bus reset;
  waits are generous, metadata writes are followed by cache flush.
- Names are 1..39 characters, reject "/" and "\", and reject "." and "..".
  File commands use the current directory; use `cd` for nested paths.
- Errors (invalid name, table full, 16 KiB limit, disk I/O, not a file,
  directory not empty) are propagated to the command prompt. Empty files,
  interrupted writes, reboot persistence and save-then-reopen are tested.
- There is no free-space bitmap, journal or fsck.
  `df` reports logical fixed-slot usage, not physical cluster free space.

User, Group, and Permission System
----------------------------------
A complete Linux-inspired native security subsystem integrated with the PM32
kernel, AarFS, shell, and native MicroPython runtime:

1. User and Group Management:
   - Unique numeric user ID (UID) and group ID (GID).
   - Superuser: root (UID 0, GID 0) with elevated administrative capabilities.
   - Standard unprivileged user: aarambh (UID 1000, GID 1000).
   - Dynamic user/group creation with duplicate name and ID collision validation.
   - Persistent user database stored in AarFS at /etc/passwd.
   - Persistent group database stored in AarFS at /etc/group.
   - Shell commands:
     - useradd [-u uid] [-g gid] [-d home] [-p pass] <name>: Create user account.
     - userdel <name>: Remove user account (superuser only).
     - usermod [-g gid] [-d home] <name>: Modify user record (superuser only).
     - users: List all registered user accounts with UID, GID, and home directory.
     - id [user]: Print UID, primary GID, and supplementary group IDs.
     - groupadd [-g gid] <name>: Create new group.
     - groupdel <name>: Remove existing group (superuser only).
     - groups [user]: Display group memberships for a user.

2. Login, Sessions, and Password Security:
   - Silent password input (no visual echoing to protect credentials from shoulder-surfing).
   - Salted SHA-256 cryptographic password hashing; plaintext passwords are never stored.
   - Salt generation and constant-time hash comparison prevent timing attacks.
   - Session tracking maintaining active UID, GID, supplementary GIDs, username, and home.
   - Dynamic shell prompt: username@AR32:<cwd> > .
   - Session switching:
     - su <user>: Switch user session (verifies password when called by unprivileged users;
       passwordless for root).
     - passwd [user]: Change account password (unprivileged users must verify current password).
     - login <user>: Clean login session transition.
     - logout: Return to default unprivileged session or root.

3. AarFS Ownership and Permission Enforcement:
   - File metadata layout backward-compatible upgrade (64-byte entry header):
     - Bytes 0..51: Standard AarFS metadata (status, type, parent_id, name, lba, size).
     - Bytes 52..53: Owner UID (little-endian 16-bit).
     - Bytes 54..55: Group GID (little-endian 16-bit).
     - Bytes 56..57: Permission mode bits (octal rwxrwxrwx, default 0644 files, 0755 dirs).
     - Bytes 58..63: Reserved.
   - Permission check logic:
     - UID 0 (root) bypasses normal read/write/execute checks.
     - Owner match: checked against owner permission bits ((mode >> 6) & 7).
     - Group match (primary or supplementary): checked against group bits ((mode >> 3) & 7).
     - Other: checked against world bits (mode & 7).
   - Complete enforcement on all filesystem access:
     - read / cat: Requires read permission (bit 4 / 04).
     - scribe / grow: Requires write permission (bit 2 / 02).
     - rm / erase / mv: Requires write permission on file and parent directory.
     - cd: Requires execute permission (bit 1 / 01) on every directory in the path.
     - Native script execution (python <script>): Requires read permission.
   - Commands:
     - chmod <octal_mode> <file>: Update permissions (owner or root only).
     - chown <user> <file>: Change file owner (root only).
     - chgrp <group> <file>: Change file group (owner or root).
     - stat <file>: Display detailed file metadata (size, UID, GID, octal mode, and rwx string).

4. Home Directories and Isolation:
   - Administrator home: /root (mode 0700; inaccessible to unprivileged users).
   - Standard user home: /home/<user> (mode 0750).
   - Navigation: 'cd ~' or bare 'cd' navigates directly to active user's session_home.

5. Kernel Security Boundaries & System Call Interface:
   - Global Descriptor Table (GDT): Configured with User Code selector (0x1B, DPL=3)
     and User Data selector (0x23, DPL=3) alongside kernel Ring 0 selectors (0x08, 0x10).
   - Interrupt Descriptor Table (IDT): Vector 128 (0x80) initialized as a DPL=3 user
     interrupt gate pointing to syscall_handler.
   - System calls supported via int 0x80:
     - SYS_GETUID (24): Returns caller's UID.
     - SYS_SETUID (23): Sets caller's UID (restricted to root).
     - SYS_GETGID (47): Returns caller's GID.
     - SYS_SETGID (46): Sets caller's GID (restricted to root).
     - SYS_WRITE  (4): Validated buffer and length console write.
   - Hardware separation status: The current kernel executes in flat 32-bit protected mode.
     Hardware memory isolation (paging via CR0.PG/CR3 and Ring 3 task execution switching via
     TSS) represents the subsequent architectural milestone.

6. Native MicroPython Runtime Integration:
   - Native 32-bit MicroPython engine embedded directly at LBA 132000.
   - Executes Python scripts natively on bare metal ('python <script.py>') with 0 Windows/host
     dependencies.
   - Scripts execute within caller's active user credentials and permission restrictions.
   - Built-in aarambh native Python module:
     - aarambh.getuid(): Returns active numeric UID.
     - aarambh.getgid(): Returns active numeric GID.
     - aarambh.getuser(): Returns active username string.
     - aarambh.stat(path): Returns file stat dictionary (st_mode, st_uid, st_gid, st_size).
     - aarambh.read(path): Reads AarFS file contents subject to kernel permission checks.
     - aarambh.syscall(num, arg1, arg2, arg3): Directly invokes kernel int 0x80 system call.

Known limitations / future work
-------------------------------
- No hardware memory paging (CR0.PG/CR3) or TSS task-switching for Ring 3.
  While user/group accounts, permissions, GDT user selectors, and the int 0x80 syscall
  gate are active, physical memory isolation between processes requires future paging.
- Cooperative single-tasking; no preemptive multi-tasking scheduler yet.
- Wi-Fi networking on Intel Centrino Advanced-N 6205 hardware requires future
  binary firmware microcode blob loading (PCI detection and honest diagnostic active).
- No USB storage, audio or graphical desktop mode.
- Help paging is forward-only with I/Q; there is no scroll-back pager.
- Editor has no undo/search/selection/clipboard; 16 KiB files only.
- File commands cannot take nested paths; `cd` first.
- Python bridge requires Windows, Python 3.8+, QEMU socket chardevs and a
  helper the user starts. It cannot work without the host helper.
- The `calc` Windows launch is Windows-specific by design; built-in arithmetic
  always works.

Tests
-----
Run from this folder with Python 3:

    py -3 tests\test_command_registry.py
    py -3 tests\test_python_bridge.py
    py -3 tests\test_aarfs_qemu.py
    py -3 tests\test_editor_qemu.py
    py -3 tests\test_help_calc_qemu.py
    py -3 tests\test_calc_host_qemu.py
    py -3 tests\test_python_bridge_qemu.py
    py -3 tests\test_native_python_qemu.py
    py -3 tests\test_users_permissions_qemu.py
    py -3 tests\test_networking_qemu.py

The QEMU tests copy aarambh-v04-pm32-fs.img into a temporary directory, boot
only that copy, drive the keyboard through QMP, and delete the copy. They
never attach a physical drive.

Results at the time of this update:
- test_command_registry.py: PASS (5 tests) - every registry command has syntax,
  purpose, example/result, errors, limits, a real handler and category; examples
  start with the command name/alias; shell dispatch uses the registry.
- test_python_bridge.py: PASS (8 tests) - real Python execution, exit codes,
  timeout, output cap, unicode mapping, unsafe names, malformed clients,
  host-app allowlist and rejection.
- test_aarfs_qemu.py: PASS - help/guide paging, files, directories, rename,
  copy, append, df, reboot persistence, errors.
- test_editor_qemu.py: PASS - create/save/reopen, arrows/Home/End/Delete/
  Backspace, dirty discard, F1, empty/long files, horizontal and vertical
  scrolling, invalid names, reboot persistence.
- test_help_calc_qemu.py: PASS - index, pager I, all 9 categories, a page for
  every registered command, arithmetic/precedence/errors, calc> REPL,
  host-unavailable path.
- test_calc_host_qemu.py: PASS - `calc host` and bare `calc` confirm only after
  a valid helper response (stub launcher, no window opened by the test).
- Manual real launch check: `calc host` through the real helper started a real
  Windows CalculatorApp instance; it was closed after verification.
- test_python_bridge_qemu.py: PASS - real CPython output over COM2, repeated
  runs, version, exceptions/exit codes, timeout, output limit, missing script,
  helper unavailable both with and without QEMU's COM2 listener (QEMU stays
  responsive), AarFS intact after bridge runs.
- test_native_python_qemu.py: PASS (6 tests) - Hello world string output, arithmetic
  expression evaluation, variables/strings, exception handling without kernel crash,
  'py native <script>' command, and automatic fallback when host helper is unavailable.
- test_users_permissions_qemu.py: PASS (14 test stages) - default boot session,
  groupadd validation & duplicates, useradd & silent password entry, passwd update,
  root mode 0600 file creation, su session switching, unprivileged command rejections,
  permission denials on read/cat/scribe/grow/rm/mv, native MicroPython permission denial,
  native MicroPython caller identity reflection & int 0x80 syscall verification,
  home directory isolation & cd ~ navigation, su root authentication, reboot persistence.
- test_networking_qemu.py: PASS (17 test scenarios) - boot without NIC detection,
  RTL8139 driver init & link UP, automatic DHCP configuration (IP/GW/DNS), ifconfig output,
  ICMP ping (gateway & 8.8.8.8), DNS resolution & hostname ping (google.com), interactive
  network GUI navigation & clean shell exit, static IP configuration & AarFS persistence,
  honest Wi-Fi scanning diagnostics (no fake APs), wget streaming download to AarFS,
  wget -O custom filename download, curl stdout streaming, curl -o file output,
  curl -I HEAD header inspection, strict HTTPS refusal without root CA certificates,
  native Git client version verification, and native Python aarambh networking integration.

Build verification:
- PM32 kernel assembles within the 32256-byte loader limit and boots.
- The legacy 16-bit kernel (kernel.asm) still assembles separately and remains
  under the same loader limit. Its FAT32 image is untouched by PM32 builds.

Troubleshooting
---------------
- "py: Python helper unavailable" -> start host_python_bridge.py with the same
  --port used by -PythonBridgePort, and confirm QEMU was launched with
  -PythonBridge.
- "AarFS unavailable" -> use the disposable PM32 image; run
  format_aarfs.py --check aarambh-v04-pm32-fs.img to inspect its superblock.
- "Unknown command" -> type help, or help <command>.
- Help stops at "-- More" -> Space continues, I returns to the index, Q quits.
- Editor says "Buffer full" -> the 16384-byte AarFS limit is reached; split the
  file or remove text.
- PowerShell says running scripts is disabled -> use the
  `powershell -NoProfile -ExecutionPolicy Bypass -File ...` form above.
- QEMU cannot write the image -> close other QEMU instances and avoid OneDrive
  syncing the .img while it is open.
