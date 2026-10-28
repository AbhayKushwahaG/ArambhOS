#include "sysinfo.h"
#include "auth.h"
#include "pci.h"
#include "net_stack.h"
#include "net_driver.h"
#include <string.h>
#include <stdio.h>

extern const kernel_api_t *g_kapi;

static void sys_print(const char *s) {
    if (g_kapi && g_kapi->putc && s) {
        while (*s) {
            if (*s == '\n') g_kapi->putc('\r');
            g_kapi->putc(*s++);
        }
    }
}

int cmd_sysinfo(const char *args) {
    (void)args;
    char line[128];

    sys_print("=================================================================\n");
    sys_print("  AARAMBH OS - UNIFIED SYSTEM SPECIFICATIONS & STATUS\n");
    sys_print("=================================================================\n");

    // 1. Operating System & Kernel
    sys_print("  Operating System:    Aarambh OS v0.4 (Unified x86 Desktop & Core)\n");
    sys_print("  Kernel Architecture: Monolithic 32-bit Protected Mode (Ring 0)\n");
    sys_print("  Memory Model:        Flat 4 GB Segmentation (GDT Base 0x00000000)\n");
    sys_print("  Interrupts / Timers: PIC 8259A Remapped (0x20..0x2F), PIT 8254 @ 1000 Hz\n");
    if (g_kapi && g_kapi->timer_ticks) {
        snprintf(line, sizeof(line), "  System Uptime:       %u ms (%u ticks)\n",
                 (unsigned int)*g_kapi->timer_ticks, (unsigned int)*g_kapi->timer_ticks);
        sys_print(line);
    }

    // 2. Physical Memory & Memory Map
    sys_print("\n  Memory Layout:\n");
    sys_print("   - Low Conventional:  0x00000000 - 0x0009FFFF (IVT, BDA, Bootloader)\n");
    sys_print("   - Protected Kernel:  0x00010000 - 0x00017FFF (kernel32.bin, 32 KB max)\n");
    sys_print("   - C Subsystem & VM:  0x00200000 - 0x004D0000 (micropy32.bin, ~2.8 MB)\n");
    sys_print("   - MicroPython Heap:  0x004D0000 - 0x004F0000 (128 KB GC Heap)\n");
    sys_print("   - VESA Backbuffer:   0x00800000 - 0x00B00000 (3 MB, 1024x768x32bpp)\n");
    sys_print("   - Total System RAM:  128 MB (QEMU default / E820 detected)\n");

    // 3. Storage & VFS
    sys_print("\n  Storage & Virtual Filesystem:\n");
    sys_print("   - Primary Drive:     ATA/IDE Master 0 (LBA28 PIO Mode)\n");
    sys_print("   - Filesystem:        AarFS v0.4 (Partition LBA 2048, 128 MB)\n");
    sys_print("   - VFS Layer:         Active (POSIX File Descriptors 0..15)\n");
    sys_print("   - Standard Mounts:   '/' -> AarFS; '/dev' -> Devices; '/proc' -> Kernel\n");

    // 4. Display Controller
    pci_device_t disp_dev;
    sys_print("\n  Graphics & Display:\n");
    if (pci_find_display_device(&disp_dev) == 0) {
        snprintf(line, sizeof(line), "   - Controller:        Bochs/QEMU BGA (PCI %02X:%02X.%d, Dev 0x%04X:0x%04X)\n",
                 disp_dev.bus, disp_dev.slot, disp_dev.func, disp_dev.vendor_id, disp_dev.device_id);
        sys_print(line);
        snprintf(line, sizeof(line), "   - Framebuffer MMIO:  BAR0 = 0x%08X\n", (unsigned int)disp_dev.bar[0]);
        sys_print(line);
    } else {
        sys_print("   - Controller:        Standard VGA / VESA Compatible\n");
    }
    sys_print("   - Video Mode:        1024x768 TrueColor (32-bit BGRX Framebuffer)\n");
    sys_print("   - Desktop Interface: Window Manager (WinXP / Fedora GNOME Hybrid)\n");

    // 5. Network Stack & Controller
    sys_print("\n  Networking Stack:\n");
    if (g_active_net_driver) {
        snprintf(line, sizeof(line), "   - Active Adapter:    %s\n", g_active_net_driver->name);
        sys_print(line);
        snprintf(line, sizeof(line), "   - MAC Address:       %02X:%02X:%02X:%02X:%02X:%02X\n",
                 g_active_net_driver->mac[0], g_active_net_driver->mac[1], g_active_net_driver->mac[2],
                 g_active_net_driver->mac[3], g_active_net_driver->mac[4], g_active_net_driver->mac[5]);
        sys_print(line);
        snprintf(line, sizeof(line), "   - Link Status:       %s\n",
                 g_active_net_driver->get_link_status() ? "Up (Connected)" : "Down (Disconnected)");
        sys_print(line);

        char ip_buf[20];
        inet_ntoa(g_net_config.ip, ip_buf);
        snprintf(line, sizeof(line), "   - IPv4 Address:      %s %s\n", ip_buf, g_net_config.is_dhcp ? "(DHCP)" : "(Static)");
        sys_print(line);

        inet_ntoa(g_net_config.netmask, ip_buf);
        snprintf(line, sizeof(line), "   - Subnet Mask:       %s\n", ip_buf);
        sys_print(line);

        inet_ntoa(g_net_config.gateway, ip_buf);
        snprintf(line, sizeof(line), "   - Gateway:           %s\n", ip_buf);
        sys_print(line);

        inet_ntoa(g_net_config.dns, ip_buf);
        snprintf(line, sizeof(line), "   - DNS Resolver:      %s\n", ip_buf);
        sys_print(line);
    } else {
        sys_print("   - Network Controller: None detected or uninitialized\n");
    }

    // 6. Execution Engines & Compatibility Tools
    sys_print("\n  Supported Software Environments:\n");
    sys_print("   - Freestanding C / ASM: Native 32-bit Ring 0 flat execution\n");
    sys_print("   - MicroPython:          Native embedded runtime (Python 3.4 core)\n");
    sys_print("   - POSIX Shell Scripts:  Native '.sh' execution engine ('run')\n");
    sys_print("   - Debian Linux:         Package inspection & installation ('dpkg', 'deb')\n");
    sys_print("   - Red Hat Linux:        Package inspection & query ('rpm')\n");
    sys_print("   - Standard Linux ELF:   ELF32 binary inspection & loader ('elfinfo')\n");
    sys_print("   - Windows Executables:  PE32 binary inspector & Wine bridge ('peinfo')\n");
    sys_print("=================================================================\n");

    return 0;
}

int cmd_hwinfo(const char *args) {
    (void)args;
    char line[128];

    sys_print("=================================================================\n");
    sys_print("  AARAMBH OS - HARDWARE ENUMERATION & PCI BUS SCAN\n");
    sys_print("=================================================================\n");

    pci_device_t pci_list[32];
    int count = pci_scan(pci_list, 32);

    snprintf(line, sizeof(line), "  Found %d PCI device(s) on host bus:\n\n", count);
    sys_print(line);

    for (int i = 0; i < count; i++) {
        pci_device_t *d = &pci_list[i];
        const char *desc = "Unknown Device";

        if (d->vendor_id == 0x8086 && d->device_id == 0x100E) desc = "Intel 82540EM Gigabit Ethernet (e1000)";
        else if (d->vendor_id == 0x10EC && d->device_id == 0x8139) desc = "Realtek RTL8139 Fast Ethernet";
        else if (d->vendor_id == 0x1234 && d->device_id == 0x1111) desc = "Bochs/QEMU BGA Display Adapter";
        else if (d->vendor_id == 0x8086 && d->device_id == 0x1237) desc = "Intel 440FX PCI Host Bridge";
        else if (d->vendor_id == 0x8086 && d->device_id == 0x7000) desc = "Intel PIIX3 PCI-to-ISA Bridge";
        else if (d->vendor_id == 0x8086 && d->device_id == 0x7010) desc = "Intel PIIX3 IDE Controller";
        else if (d->vendor_id == 0x8086 && d->device_id == 0x7113) desc = "Intel PIIX4 ACPI / Power Management";

        snprintf(line, sizeof(line), "  [%02d] %02X:%02X.%d  ID: %04X:%04X  Class: %02X.%02X  IRQ: %2u  %s\n",
                 i, d->bus, d->slot, d->func, d->vendor_id, d->device_id, d->class_code, d->subclass, d->irq, desc);
        sys_print(line);

        for (int b = 0; b < 6; b++) {
            if (d->bar[b]) {
                snprintf(line, sizeof(line), "       BAR%d: %s 0x%08X\n",
                         b, d->bar_type[b] ? "I/O " : "MMIO", (unsigned int)d->bar[b]);
                sys_print(line);
            }
        }
    }

    sys_print("=================================================================\n");
    return 0;
}

int cmd_compat(const char *args) {
    (void)args;

    sys_print("=================================================================\n");
    sys_print("  AARAMBH OS - SOFTWARE COMPATIBILITY MATRIX & ARCHITECTURE\n");
    sys_print("=================================================================\n");

    sys_print("  Category                  Technology / Subsystem           Status\n");
    sys_print("  ---------------------------------------------------------------\n");
    sys_print("  Core Kernel               32-bit Protected Mode (Ring 0)   [FUNCTIONAL]\n");
    sys_print("  Memory Model              Flat 4GB Segments, PIC/PIT       [FUNCTIONAL]\n");
    sys_print("  Filesystem                AarFS v0.4 (128 MB Disk Image)   [FUNCTIONAL]\n");
    sys_print("  Filesystem                VFS POSIX File Descriptors 0..15 [FUNCTIONAL]\n");
    sys_print("  User Authentication       Multi-user, Passwd, SHA-256      [FUNCTIONAL]\n");
    sys_print("  Scripting Engine          Native MicroPython 3.4 Runtime   [FUNCTIONAL]\n");
    sys_print("  Shell Scripting           POSIX-style .sh Batch Scripts    [FUNCTIONAL]\n");
    sys_print("  Debian Packages           DEB Inspector & AarFS Installer  [FUNCTIONAL]\n");
    sys_print("  Red Hat Packages          RPM Lead & Header Tag Inspector  [FUNCTIONAL]\n");
    sys_print("  Linux Binaries            ELF32 Header & Segment Inspector [FUNCTIONAL]\n");
    sys_print("  Windows Binaries          PE32 Header, Sections & IAT      [FUNCTIONAL]\n");
    sys_print("  Networking Stack          Ethernet, ARP, IPv4, ICMP, UDP   [FUNCTIONAL]\n");
    sys_print("  Internet Protocols        DHCP Client, DNS Resolver        [FUNCTIONAL]\n");
    sys_print("  Network Tools             Ping, Ifconfig, HTTP GET, Wget   [FUNCTIONAL]\n");
    sys_print("  Version Control           Git Clone & AarFS Commit         [FUNCTIONAL]\n");
    sys_print("  Graphical Desktop         VESA/BGA TrueColor Window Mgr    [FUNCTIONAL]\n");
    sys_print("  ---------------------------------------------------------------\n");
    sys_print("  Linux Emulation Layer     Ring 3 User Mode & Paging        [ROADMAP Phase 3]\n");
    sys_print("  Linux System Calls        POSIX int 0x80 ABI Emulator      [ROADMAP Phase 4]\n");
    sys_print("  Self-Hosting Compiler     Native C Compiler (TCC/AarCC)    [ROADMAP Phase 4]\n");
    sys_print("  Windows Wine Emulation    Win32 PE Loader & KERNEL32 IAT   [ROADMAP Phase 5]\n");
    sys_print("  Security / Encryption     TLS 1.3 / HTTPS Network Security [ROADMAP Phase 6]\n");
    sys_print("  Web Browser Engine        Lightweight HTML5/CSS/JS Engine  [ROADMAP Phase 7]\n");
    sys_print("  Gaming & 3D Acceleration  OpenGL / Mesa 3D Pipeline        [ROADMAP Phase 8]\n");
    sys_print("  Java Virtual Machine      JVM Runtime for Minecraft Target [ROADMAP Phase 9]\n");
    sys_print("=================================================================\n");

    return 0;
}
