#include "elf_loader.h"
#include "vfs.h"
#include <string.h>
#include <stdio.h>

extern const kernel_api_t *g_kapi;

static void elf_print(const char *s) {
    if (g_kapi && g_kapi->putc && s) {
        while (*s) {
            if (*s == '\n') g_kapi->putc('\r');
            g_kapi->putc(*s++);
        }
    }
}

int cmd_elfinfo(const char *args) {
    if (!args || !*args) {
        elf_print("Aarambh ELF Binary Inspector & Loader Engine\n");
        elf_print("Usage: elfinfo <file.elf>   (Alias: elf <file.elf>)\n");
        return 0;
    }

    while (*args == ' ') args++;
    char filename[64];
    strncpy(filename, args, sizeof(filename) - 1);
    char *sp = strchr(filename, ' ');
    if (sp) *sp = '\0';

    static char s_elf_data[16384];
    size_t file_size = 0;
    int r = vfs_read_entire_file(filename, s_elf_data, sizeof(s_elf_data), &file_size);
    if (r != VFS_OK || file_size < sizeof(Elf32_Ehdr)) {
        elf_print("elfinfo: error: cannot read '");
        elf_print(filename);
        elf_print("': file not found or inaccessible.\n");
        return 1;
    }

    // 1. Verify ELF Identification Magic
    if (s_elf_data[0] != ELFMAG0 || s_elf_data[1] != ELFMAG1 ||
        s_elf_data[2] != ELFMAG2 || s_elf_data[3] != ELFMAG3) {
        elf_print("elfinfo: error: '");
        elf_print(filename);
        elf_print("' is not a valid ELF binary (magic mismatch).\n");
        return 1;
    }

    Elf32_Ehdr *eh = (Elf32_Ehdr *)s_elf_data;

    elf_print("=================================================================\n");
    elf_print("  ELF BINARY INSPECTION REPORT: ");
    elf_print(filename);
    elf_print("\n=================================================================\n");

    // Bitness / Class
    char line[128];
    if (eh->e_ident[4] == ELFCLASS32) {
        elf_print("  Format:       ELF32 (32-bit x86 Standard)\n");
    } else if (eh->e_ident[4] == ELFCLASS64) {
        elf_print("  Format:       ELF64 (64-bit Architecture - Long Mode required)\n");
    } else {
        elf_print("  Format:       Unknown ELF Class\n");
    }

    // Endianness
    elf_print(eh->e_ident[5] == ELFDATA2LSB ? "  Endianness:   Little-Endian (x86 LSB)\n" : "  Endianness:   Big-Endian\n");

    // Machine Architecture
    if (eh->e_machine == EM_386) {
        elf_print("  Architecture: Intel 80386 / i686 (x86 Compatible)\n");
    } else if (eh->e_machine == EM_X86_64) {
        elf_print("  Architecture: AMD x86-64 / EM64T\n");
    } else {
        snprintf(line, sizeof(line), "  Architecture: Machine Type 0x%04X\n", eh->e_machine);
        elf_print(line);
    }

    // Binary Type
    if (eh->e_type == ET_EXEC) {
        elf_print("  Binary Type:  ET_EXEC (Position-Dependent Executable)\n");
    } else if (eh->e_type == ET_DYN) {
        elf_print("  Binary Type:  ET_DYN (Position-Independent Shared Object / PIE)\n");
    } else if (eh->e_type == ET_REL) {
        elf_print("  Binary Type:  ET_REL (Relocatable Object File)\n");
    } else {
        snprintf(line, sizeof(line), "  Binary Type:  0x%04X\n", eh->e_type);
        elf_print(line);
    }

    // Entry Point Virtual Address
    snprintf(line, sizeof(line), "  Entry Point:  0x%08X\n", (unsigned int)eh->e_entry);
    elf_print(line);

    // Headers count
    snprintf(line, sizeof(line), "  Program Hdrs: %d entries (offset 0x%X)\n", eh->e_phnum, (unsigned int)eh->e_phoff);
    elf_print(line);
    snprintf(line, sizeof(line), "  Section Hdrs: %d entries (offset 0x%X)\n", eh->e_shnum, (unsigned int)eh->e_shoff);
    elf_print(line);

    // 2. Scan Program Headers (Segments)
    int has_interp = 0;
    char interp_path[64] = "";
    uint32_t total_mem = 0;

    if (eh->e_phoff + eh->e_phnum * sizeof(Elf32_Phdr) <= file_size) {
        elf_print("\n  Segments (Program Headers):\n");
        for (int i = 0; i < eh->e_phnum; i++) {
            Elf32_Phdr *ph = (Elf32_Phdr *)(s_elf_data + eh->e_phoff + i * sizeof(Elf32_Phdr));
            char flags[4] = "---";
            if (ph->p_flags & PF_R) flags[0] = 'R';
            if (ph->p_flags & PF_W) flags[1] = 'W';
            if (ph->p_flags & PF_X) flags[2] = 'X';

            if (ph->p_type == PT_LOAD) {
                total_mem += ph->p_memsz;
                snprintf(line, sizeof(line), "   [%d] LOAD  VAddr: 0x%08X  FileSz: %5u B  MemSz: %5u B  [%s]\n",
                         i, (unsigned int)ph->p_vaddr, (unsigned int)ph->p_filesz, (unsigned int)ph->p_memsz, flags);
                elf_print(line);
            } else if (ph->p_type == PT_INTERP) {
                has_interp = 1;
                if (ph->p_offset + ph->p_filesz <= file_size && ph->p_filesz < sizeof(interp_path)) {
                    memcpy(interp_path, s_elf_data + ph->p_offset, ph->p_filesz);
                    interp_path[ph->p_filesz] = '\0';
                }
                snprintf(line, sizeof(line), "   [%d] INTERP Dynamic Linker Path: '%s'\n", i, interp_path);
                elf_print(line);
            } else if (ph->p_type == PT_DYNAMIC) {
                snprintf(line, sizeof(line), "   [%d] DYNAMIC Dynamic Linking Information\n", i);
                elf_print(line);
            }
        }
    }

    // 3. Scan Section Headers (Names)
    if (eh->e_shoff + eh->e_shnum * sizeof(Elf32_Shdr) <= file_size && eh->e_shstrndx < eh->e_shnum) {
        Elf32_Shdr *shstr = (Elf32_Shdr *)(s_elf_data + eh->e_shoff + eh->e_shstrndx * sizeof(Elf32_Shdr));
        const char *strtab = (shstr->sh_offset + shstr->sh_size <= file_size) ? (s_elf_data + shstr->sh_offset) : NULL;

        if (strtab) {
            elf_print("\n  Key Sections:\n");
            for (int i = 0; i < eh->e_shnum; i++) {
                Elf32_Shdr *sh = (Elf32_Shdr *)(s_elf_data + eh->e_shoff + i * sizeof(Elf32_Shdr));
                const char *sname = strtab + sh->sh_name;
                if (sh->sh_size > 0 && sname[0] == '.') {
                    snprintf(line, sizeof(line), "   %-16s  Addr: 0x%08X  Size: %5u B\n",
                             sname, (unsigned int)sh->sh_addr, (unsigned int)sh->sh_size);
                    elf_print(line);
                }
            }
        }
    }

    // 4. Honest Compatibility Diagnostics
    elf_print("\n  Compatibility Assessment:\n");
    if (eh->e_machine != EM_386) {
        elf_print("   [BLOCKED] Non-x86 Architecture. Cannot execute on 32-bit x86 CPU.\n");
    } else if (eh->e_ident[4] != ELFCLASS32) {
        elf_print("   [BLOCKED] 64-bit ELF binary. Aarambh OS kernel is 32-bit protected mode.\n");
    } else if (has_interp) {
        snprintf(line, sizeof(line), "   [DYNAMIC] Requires Linux Dynamic Linker: %s\n", interp_path);
        elf_print(line);
        elf_print("   [STATUS]  Requires POSIX system call layer & glibc runtime (Roadmap Phase 4).\n");
    } else {
        elf_print("   [STATIC]  Statically Linked Freestanding x86 ELF Binary.\n");
        elf_print("   [STATUS]  Directly loadable via Aarambh Process Execution Layer.\n");
    }
    elf_print("=================================================================\n");

    return 0;
}
