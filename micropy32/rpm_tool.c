#include "rpm_tool.h"
#include "vfs.h"
#include <string.h>
#include <stdio.h>

extern const kernel_api_t *g_kapi;

static void rpm_print(const char *s) {
    if (g_kapi && g_kapi->putc && s) {
        while (*s) {
            if (*s == '\n') g_kapi->putc('\r');
            g_kapi->putc(*s++);
        }
    }
}

static inline uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           (uint32_t)p[3];
}

static inline uint16_t read_be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static const char *get_tag_string(const uint8_t *data_store, uint32_t data_len,
                                  const rpm_index_entry_t *indices, uint32_t nindex,
                                  uint32_t target_tag) {
    for (uint32_t i = 0; i < nindex; i++) {
        uint32_t tag = read_be32((const uint8_t *)&indices[i].tag);
        if (tag == target_tag) {
            uint32_t offset = read_be32((const uint8_t *)&indices[i].offset);
            if (offset < data_len) {
                return (const char *)(data_store + offset);
            }
        }
    }
    return NULL;
}

static void print_tag_string_array(const uint8_t *data_store, uint32_t data_len,
                                   const rpm_index_entry_t *indices, uint32_t nindex,
                                   uint32_t target_tag, const char *prefix) {
    for (uint32_t i = 0; i < nindex; i++) {
        uint32_t tag = read_be32((const uint8_t *)&indices[i].tag);
        if (tag == target_tag) {
            uint32_t offset = read_be32((const uint8_t *)&indices[i].offset);
            uint32_t count = read_be32((const uint8_t *)&indices[i].count);
            if (offset >= data_len) return;

            const char *curr = (const char *)(data_store + offset);
            for (uint32_t c = 0; c < count && (uint32_t)(curr - (const char *)data_store) < data_len; c++) {
                if (*curr) {
                    if (prefix) rpm_print(prefix);
                    rpm_print(curr);
                    rpm_print("\n");
                }
                curr += strlen(curr) + 1;
            }
            return;
        }
    }
}

int cmd_rpm(const char *args) {
    if (!args || !*args) {
        rpm_print("Aarambh Linux RPM Package Inspector\n");
        rpm_print("Usage:\n");
        rpm_print("  rpm <package.rpm>       Display full RPM package inspection report\n");
        rpm_print("  rpm -qi <package.rpm>   Query package metadata, version, & description\n");
        rpm_print("  rpm -ql <package.rpm>   List file contents of package\n");
        rpm_print("  rpm -qR <package.rpm>   List required dependencies & capabilities\n");
        rpm_print("  rpm -qp <package.rpm>   Query package brief information\n");
        return 0;
    }

    while (*args == ' ') args++;

    int mode_info = 0;
    int mode_list = 0;
    int mode_reqs = 0;

    char filename[64] = {0};

    if (strncmp(args, "-qi ", 4) == 0) {
        mode_info = 1;
        args += 4;
    } else if (strncmp(args, "-ql ", 4) == 0) {
        mode_list = 1;
        args += 4;
    } else if (strncmp(args, "-qR ", 4) == 0) {
        mode_reqs = 1;
        args += 4;
    } else if (strncmp(args, "-qp ", 4) == 0) {
        mode_info = 1;
        args += 4;
    } else if (strncmp(args, "-q ", 3) == 0) {
        mode_info = 1;
        args += 3;
    }

    while (*args == ' ') args++;
    strncpy(filename, args, sizeof(filename) - 1);
    char *sp = strchr(filename, ' ');
    if (sp) *sp = '\0';

    static uint8_t s_rpm_data[32768];
    size_t file_size = 0;
    int r = vfs_read_entire_file(filename, s_rpm_data, sizeof(s_rpm_data), &file_size);
    if (r != VFS_OK || file_size < sizeof(rpm_lead_t)) {
        rpm_print("rpm: error: cannot read '");
        rpm_print(filename);
        rpm_print("': file not found or inaccessible.\n");
        return 1;
    }

    // 1. Verify RPM Lead Magic
    const rpm_lead_t *lead = (const rpm_lead_t *)s_rpm_data;
    if (lead->magic[0] != RPM_LEAD_MAGIC0 || lead->magic[1] != RPM_LEAD_MAGIC1 ||
        lead->magic[2] != RPM_LEAD_MAGIC2 || lead->magic[3] != RPM_LEAD_MAGIC3) {
        rpm_print("rpm: error: '");
        rpm_print(filename);
        rpm_print("' is not a valid RPM package (Lead magic mismatch).\n");
        return 1;
    }

    // Lead architecture
    uint16_t lead_arch = read_be16((const uint8_t *)&lead->archnum);

    // 2. Locate RPM Headers
    // The Lead is 96 bytes. The first header starts at offset 96.
    uint32_t offset = 96;
    const rpm_header_intro_t *main_hdr = NULL;
    const rpm_index_entry_t *main_indices = NULL;
    const uint8_t *main_data_store = NULL;
    uint32_t main_nindex = 0;
    uint32_t main_datasize = 0;

    // Scan headers (up to 2: Signature Header followed by Main Package Header)
    while (offset + sizeof(rpm_header_intro_t) <= file_size) {
        const rpm_header_intro_t *hdr = (const rpm_header_intro_t *)(s_rpm_data + offset);
        if (hdr->magic[0] != RPM_HEADER_MAGIC0 ||
            hdr->magic[1] != RPM_HEADER_MAGIC1 ||
            hdr->magic[2] != RPM_HEADER_MAGIC2 ||
            hdr->version  != RPM_HEADER_MAGIC3) {
            break;
        }

        uint32_t nindex = read_be32((const uint8_t *)&hdr->nindex);
        uint32_t datasize = read_be32((const uint8_t *)&hdr->datasize);
        uint32_t hdr_table_sz = nindex * sizeof(rpm_index_entry_t);
        uint32_t indices_offset = offset + sizeof(rpm_header_intro_t);
        uint32_t data_offset = indices_offset + hdr_table_sz;

        if (data_offset + datasize > file_size) {
            break; // Truncated header
        }

        const rpm_index_entry_t *indices = (const rpm_index_entry_t *)(s_rpm_data + indices_offset);
        const uint8_t *data_store = s_rpm_data + data_offset;

        // Check if this header contains package metadata (RPMTAG_NAME = 1000)
        int has_name_tag = 0;
        for (uint32_t i = 0; i < nindex; i++) {
            if (read_be32((const uint8_t *)&indices[i].tag) == RPMTAG_NAME) {
                has_name_tag = 1;
                break;
            }
        }

        if (has_name_tag) {
            main_hdr = hdr;
            main_indices = indices;
            main_data_store = data_store;
            main_nindex = nindex;
            main_datasize = datasize;
            break; // Found main header
        }

        // Advance to next header with 8-byte alignment
        offset = data_offset + datasize;
        offset = (offset + 7) & ~7;
    }

    // If main header not found, fallback to first header if available
    if (!main_hdr && offset >= 96 && offset + sizeof(rpm_header_intro_t) <= file_size) {
        const rpm_header_intro_t *hdr = (const rpm_header_intro_t *)(s_rpm_data + 96);
        if (hdr->magic[0] == RPM_HEADER_MAGIC0 && hdr->magic[1] == RPM_HEADER_MAGIC1 &&
            hdr->magic[2] == RPM_HEADER_MAGIC2 && hdr->version  == RPM_HEADER_MAGIC3) {
            main_hdr = hdr;
            main_nindex = read_be32((const uint8_t *)&hdr->nindex);
            main_datasize = read_be32((const uint8_t *)&hdr->datasize);
            main_indices = (const rpm_index_entry_t *)(s_rpm_data + 96 + sizeof(rpm_header_intro_t));
            main_data_store = (const uint8_t *)main_indices + main_nindex * sizeof(rpm_index_entry_t);
        }
    }

    // Extract Metadata Tags
    const char *pkg_name = NULL;
    const char *pkg_ver = NULL;
    const char *pkg_rel = NULL;
    const char *pkg_arch = NULL;
    const char *pkg_summary = NULL;
    const char *pkg_license = NULL;
    const char *pkg_group = NULL;

    if (main_indices && main_data_store) {
        pkg_name = get_tag_string(main_data_store, main_datasize, main_indices, main_nindex, RPMTAG_NAME);
        pkg_ver = get_tag_string(main_data_store, main_datasize, main_indices, main_nindex, RPMTAG_VERSION);
        pkg_rel = get_tag_string(main_data_store, main_datasize, main_indices, main_nindex, RPMTAG_RELEASE);
        pkg_arch = get_tag_string(main_data_store, main_datasize, main_indices, main_nindex, RPMTAG_ARCH);
        pkg_summary = get_tag_string(main_data_store, main_datasize, main_indices, main_nindex, RPMTAG_SUMMARY);
        pkg_license = get_tag_string(main_data_store, main_datasize, main_indices, main_nindex, RPMTAG_LICENSE);
        pkg_group = get_tag_string(main_data_store, main_datasize, main_indices, main_nindex, RPMTAG_GROUP);
    }

    if (!pkg_name && lead->name[0]) {
        pkg_name = lead->name;
    }
    if (!pkg_name) pkg_name = "unknown";
    if (!pkg_ver) pkg_ver = "1.0";
    if (!pkg_rel) pkg_rel = "1";
    if (!pkg_arch) {
        if (lead_arch == 1) pkg_arch = "i386";
        else if (lead_arch == 2) pkg_arch = "alpha";
        else if (lead_arch == 3) pkg_arch = "sparc";
        else pkg_arch = "x86_64/other";
    }

    char line[128];

    // Handle File List Query (-ql)
    if (mode_list) {
        rpm_print("Files contained in RPM package '");
        rpm_print(filename);
        rpm_print("':\n");
        if (main_indices && main_data_store) {
            print_tag_string_array(main_data_store, main_datasize, main_indices, main_nindex, RPMTAG_BASENAMES, "  /");
            print_tag_string_array(main_data_store, main_datasize, main_indices, main_nindex, RPMTAG_FILENAMES, "  ");
        } else {
            rpm_print("  (Header indices unavailable)\n");
        }
        return 0;
    }

    // Handle Requirements Query (-qR)
    if (mode_reqs) {
        rpm_print("Dependencies & Requirements for '");
        rpm_print(filename);
        rpm_print("':\n");
        if (main_indices && main_data_store) {
            print_tag_string_array(main_data_store, main_datasize, main_indices, main_nindex, RPMTAG_REQUIRENAME, "  - ");
        } else {
            rpm_print("  (No requirements metadata found)\n");
        }
        return 0;
    }

    // Default or -qi / -qp: Print full package inspection report
    rpm_print("=================================================================\n");
    rpm_print("  RPM PACKAGE INSPECTION REPORT: ");
    rpm_print(filename);
    rpm_print("\n=================================================================\n");

    snprintf(line, sizeof(line), "  Package:      %s\n", pkg_name);
    rpm_print(line);
    snprintf(line, sizeof(line), "  Version:      %s-%s\n", pkg_ver, pkg_rel);
    rpm_print(line);
    snprintf(line, sizeof(line), "  Architecture: %s (Lead Arch ID: %u)\n", pkg_arch, lead_arch);
    rpm_print(line);
    if (pkg_license) {
        snprintf(line, sizeof(line), "  License:      %s\n", pkg_license);
        rpm_print(line);
    }
    if (pkg_group) {
        snprintf(line, sizeof(line), "  Group:        %s\n", pkg_group);
        rpm_print(line);
    }
    if (pkg_summary) {
        snprintf(line, sizeof(line), "  Summary:      %s\n", pkg_summary);
        rpm_print(line);
    }

    snprintf(line, sizeof(line), "  Archive Size: %u bytes\n", (unsigned int)file_size);
    rpm_print(line);
    snprintf(line, sizeof(line), "  Header Tags:  %u index records\n", main_nindex);
    rpm_print(line);

    // Compatibility Assessment
    rpm_print("\n  Aarambh Compatibility Assessment:\n");
    if (strcmp(pkg_arch, "x86_64") == 0 || strcmp(pkg_arch, "aarch64") == 0) {
        rpm_print("   [BLOCKED] 64-bit RPM package. Aarambh OS kernel is 32-bit protected mode.\n");
    } else if (strcmp(pkg_arch, "i386") == 0 || strcmp(pkg_arch, "i686") == 0 || strcmp(pkg_arch, "noarch") == 0) {
        rpm_print("   [ARCH OK] Architecture compatible (32-bit x86 / noarch).\n");
        rpm_print("   [RUNTIME] Requires Linux POSIX system call layer & glibc runtime (Roadmap Phase 4).\n");
        rpm_print("   [INSTALL] Cpio archive payload unpacker integration ready for Phase 4.\n");
    } else {
        snprintf(line, sizeof(line), "   [BLOCKED] Incompatible architecture '%s' for x86 CPU.\n", pkg_arch);
        rpm_print(line);
    }
    rpm_print("=================================================================\n");

    return 0;
}
