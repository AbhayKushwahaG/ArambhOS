#include "pe_tool.h"
#include "vfs.h"
#include <string.h>
#include <stdio.h>

extern const kernel_api_t *g_kapi;

static void pe_print(const char *s) {
    if (g_kapi && g_kapi->putc && s) {
        while (*s) {
            if (*s == '\n') g_kapi->putc('\r');
            g_kapi->putc(*s++);
        }
    }
}

static uint32_t rva_to_offset(uint32_t rva, const image_section_header_t *sections, uint16_t num_sections, size_t file_size) {
    for (uint16_t i = 0; i < num_sections; i++) {
        uint32_t vaddr = sections[i].VirtualAddress;
        uint32_t vsz = sections[i].VirtualSize;
        if (vsz == 0) vsz = sections[i].SizeOfRawData;

        if (rva >= vaddr && rva < vaddr + vsz) {
            uint32_t diff = rva - vaddr;
            uint32_t raw_ptr = sections[i].PointerToRawData;
            if (raw_ptr + diff < file_size) {
                return raw_ptr + diff;
            }
        }
    }
    return 0;
}

int cmd_peinfo(const char *args) {
    if (!args || !*args) {
        pe_print("Aarambh Windows PE32 Binary Inspector & Wine Compatibility Tool\n");
        pe_print("Usage: peinfo <file.exe>   (Alias: wine-check <file.exe>)\n");
        return 0;
    }

    while (*args == ' ') args++;
    char filename[64];
    strncpy(filename, args, sizeof(filename) - 1);
    char *sp = strchr(filename, ' ');
    if (sp) *sp = '\0';

    static uint8_t s_pe_data[32768];
    size_t file_size = 0;
    int r = vfs_read_entire_file(filename, s_pe_data, sizeof(s_pe_data), &file_size);
    if (r != VFS_OK || file_size < sizeof(image_dos_header_t)) {
        pe_print("peinfo: error: cannot read '");
        pe_print(filename);
        pe_print("': file not found or inaccessible.\n");
        return 1;
    }

    // 1. Verify DOS Header
    const image_dos_header_t *dos_hdr = (const image_dos_header_t *)s_pe_data;
    if (dos_hdr->e_magic != IMAGE_DOS_SIGNATURE) {
        pe_print("peinfo: error: '");
        pe_print(filename);
        pe_print("' is not a valid MS-DOS / Windows executable ('MZ' magic missing).\n");
        return 1;
    }

    // 2. Check PE Header Offset
    int32_t pe_offset = dos_hdr->e_lfanew;
    if (pe_offset < 0 || (size_t)pe_offset + sizeof(uint32_t) + sizeof(image_file_header_t) > file_size) {
        pe_print("peinfo: error: '");
        pe_print(filename);
        pe_print("' is a legacy 16-bit DOS executable (no modern PE header).\n");
        return 1;
    }

    // 3. Verify PE Signature
    const uint32_t *nt_sig = (const uint32_t *)(s_pe_data + pe_offset);
    if (*nt_sig != IMAGE_NT_SIGNATURE) {
        pe_print("peinfo: error: '");
        pe_print(filename);
        pe_print("' missing NT PE signature ('PE\\0\\0').\n");
        return 1;
    }

    // 4. File Header (COFF)
    const image_file_header_t *file_hdr = (const image_file_header_t *)(s_pe_data + pe_offset + 4);
    uint16_t machine = file_hdr->Machine;
    uint16_t num_sections = file_hdr->NumberOfSections;
    uint16_t opt_hdr_sz = file_hdr->SizeOfOptionalHeader;
    uint16_t characteristics = file_hdr->Characteristics;

    pe_print("=================================================================\n");
    pe_print("  WINDOWS PE EXECUTABLE INSPECTION REPORT: ");
    pe_print(filename);
    pe_print("\n=================================================================\n");

    char line[128];

    // Machine Type
    if (machine == IMAGE_FILE_MACHINE_I386) {
        pe_print("  Machine:      i386 (Intel 32-bit x86 Standard)\n");
    } else if (machine == IMAGE_FILE_MACHINE_AMD64) {
        pe_print("  Machine:      x86-64 / AMD64 (64-bit Architecture)\n");
    } else {
        snprintf(line, sizeof(line), "  Machine:      Type 0x%04X\n", machine);
        pe_print(line);
    }

    // Binary Type
    if (characteristics & IMAGE_FILE_DLL) {
        pe_print("  Binary Type:  Dynamic Link Library (DLL)\n");
    } else if (characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) {
        pe_print("  Binary Type:  Standard Executable (EXE)\n");
    } else {
        pe_print("  Binary Type:  Object / Relocatable\n");
    }

    // 5. Optional Header
    uint32_t entry_point = 0;
    uint32_t image_base = 0;
    uint16_t subsystem = 0;
    uint32_t import_rva = 0;
    uint32_t import_sz = 0;

    if (opt_hdr_sz >= sizeof(image_optional_header32_t)) {
        const image_optional_header32_t *opt_hdr = (const image_optional_header32_t *)(s_pe_data + pe_offset + 4 + sizeof(image_file_header_t));
        if (opt_hdr->Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
            pe_print("  Format:       PE32 (32-bit Portable Executable)\n");
        } else if (opt_hdr->Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            pe_print("  Format:       PE32+ (64-bit Portable Executable)\n");
        }

        entry_point = opt_hdr->AddressOfEntryPoint;
        image_base = opt_hdr->ImageBase;
        subsystem = opt_hdr->Subsystem;

        snprintf(line, sizeof(line), "  Image Base:   0x%08X\n", (unsigned int)image_base);
        pe_print(line);
        snprintf(line, sizeof(line), "  Entry Point:  0x%08X (VA: 0x%08X)\n", (unsigned int)entry_point, (unsigned int)(image_base + entry_point));
        pe_print(line);

        if (subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI) {
            pe_print("  Subsystem:    Windows GUI (Win32 Windowed Application)\n");
        } else if (subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI) {
            pe_print("  Subsystem:    Windows CUI (Console Application)\n");
        } else if (subsystem == IMAGE_SUBSYSTEM_NATIVE) {
            pe_print("  Subsystem:    Native NT Driver / Subsystem\n");
        } else {
            snprintf(line, sizeof(line), "  Subsystem:    Code %u\n", subsystem);
            pe_print(line);
        }

        if (opt_hdr->NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_IMPORT) {
            import_rva = opt_hdr->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
            import_sz = opt_hdr->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;
        }
    }

    // 6. Section Table
    uint32_t sec_offset = pe_offset + 4 + sizeof(image_file_header_t) + opt_hdr_sz;
    const image_section_header_t *sections = (const image_section_header_t *)(s_pe_data + sec_offset);

    if (sec_offset + num_sections * sizeof(image_section_header_t) <= file_size) {
        pe_print("\n  Sections:\n");
        for (uint16_t i = 0; i < num_sections && i < 16; i++) {
            char sec_name[9] = {0};
            memcpy(sec_name, sections[i].Name, 8);
            snprintf(line, sizeof(line), "   [%u] %-8s  VAddr: 0x%08X  VSize: %5u B  RawSz: %5u B (off 0x%X)\n",
                     i, sec_name,
                     (unsigned int)sections[i].VirtualAddress,
                     (unsigned int)sections[i].VirtualSize,
                     (unsigned int)sections[i].SizeOfRawData,
                     (unsigned int)sections[i].PointerToRawData);
            pe_print(line);
        }
    }

    // 7. Import Directory (DLL dependencies & symbols)
    int has_kernel32 = 0;
    int has_user32 = 0;
    int has_gdi32 = 0;

    if (import_rva > 0 && import_sz > 0) {
        uint32_t imp_file_off = rva_to_offset(import_rva, sections, num_sections, file_size);
        if (imp_file_off > 0 && imp_file_off + sizeof(image_import_descriptor_t) <= file_size) {
            pe_print("\n  Imported Dynamic Link Libraries (Win32 DLLs):\n");
            const image_import_descriptor_t *imp_desc = (const image_import_descriptor_t *)(s_pe_data + imp_file_off);

            while (imp_desc->Name != 0 && (const uint8_t *)imp_desc + sizeof(image_import_descriptor_t) <= s_pe_data + file_size) {
                uint32_t name_off = rva_to_offset(imp_desc->Name, sections, num_sections, file_size);
                if (name_off > 0 && name_off < file_size) {
                    const char *dll_name = (const char *)(s_pe_data + name_off);
                    snprintf(line, sizeof(line), "   - %s\n", dll_name);
                    pe_print(line);

                    if (strstr(dll_name, "KERNEL32") || strstr(dll_name, "kernel32")) has_kernel32 = 1;
                    if (strstr(dll_name, "USER32") || strstr(dll_name, "user32")) has_user32 = 1;
                    if (strstr(dll_name, "GDI32") || strstr(dll_name, "gdi32")) has_gdi32 = 1;

                    // Read first few imported function names
                    uint32_t thunk_rva = imp_desc->OriginalFirstThunk ? imp_desc->OriginalFirstThunk : imp_desc->FirstThunk;
                    if (thunk_rva) {
                        uint32_t thunk_off = rva_to_offset(thunk_rva, sections, num_sections, file_size);
                        if (thunk_off && thunk_off + sizeof(uint32_t) <= file_size) {
                            const uint32_t *thunks = (const uint32_t *)(s_pe_data + thunk_off);
                            int sym_count = 0;
                            while (*thunks && sym_count < 4 && (const uint8_t *)thunks + sizeof(uint32_t) <= s_pe_data + file_size) {
                                if (!(*thunks & 0x80000000)) { // Not ordinal import
                                    uint32_t fn_off = rva_to_offset(*thunks, sections, num_sections, file_size);
                                    if (fn_off && fn_off + 2 < file_size) {
                                        const char *fn_name = (const char *)(s_pe_data + fn_off + 2);
                                        snprintf(line, sizeof(line), "       * %s\n", fn_name);
                                        pe_print(line);
                                    }
                                }
                                thunks++;
                                sym_count++;
                            }
                        }
                    }
                }
                imp_desc++;
            }
        }
    }

    // 8. Wine / Windows Compatibility Assessment
    pe_print("\n  AarOS Wine Compatibility Assessment:\n");
    if (machine != IMAGE_FILE_MACHINE_I386) {
        pe_print("   [BLOCKED] 64-bit PE32+ executable. Requires x86-64 CPU long-mode kernel.\n");
    } else {
        pe_print("   [ARCH OK] Architecture matches native 32-bit x86 Protected Mode.\n");
        if (subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI) {
            pe_print("   [CONSOLE] Console application: Target for Phase 5 Wine KERNEL32 console runtime.\n");
        } else if (subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI) {
            pe_print("   [GUI APP] Windowed GUI: Target for Phase 5 Wine USER32/GDI32 windowing bridge.\n");
        }
        if (has_kernel32) {
            pe_print("   [STATUS]  Requires KERNEL32.dll emulation: HeapAlloc, GetStdHandle, WriteFile.\n");
        }
        if (has_user32 || has_gdi32) {
            pe_print("   [STATUS]  Requires USER32/GDI32 emulation: CreateWindowEx, DefWindowProc, DispatchMessage.\n");
        }
        pe_print("   [ROADMAP] PE Loader & Import Address Table (IAT) thunking planned in Phase 5.\n");
    }
    pe_print("=================================================================\n");

    return 0;
}
