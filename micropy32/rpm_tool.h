#ifndef AARAMBH_RPM_TOOL_H
#define AARAMBH_RPM_TOOL_H

#include <stddef.h>
#include <stdint.h>

// RPM Lead magic: 0xed, 0xab, 0xee, 0xdb
#define RPM_LEAD_MAGIC0 0xed
#define RPM_LEAD_MAGIC1 0xab
#define RPM_LEAD_MAGIC2 0xee
#define RPM_LEAD_MAGIC3 0xdb

// RPM Header magic: 0x8e, 0xad, 0xe8, 0x01
#define RPM_HEADER_MAGIC0 0x8e
#define RPM_HEADER_MAGIC1 0xad
#define RPM_HEADER_MAGIC2 0xe8
#define RPM_HEADER_MAGIC3 0x01

// RPM Tag constants
#define RPMTAG_NAME           1000
#define RPMTAG_VERSION        1001
#define RPMTAG_RELEASE        1002
#define RPMTAG_SUMMARY        1004
#define RPMTAG_DESCRIPTION    1005
#define RPMTAG_BUILDTIME      1006
#define RPMTAG_SIZE           1009
#define RPMTAG_DISTRIBUTION   1010
#define RPMTAG_VENDOR         1011
#define RPMTAG_LICENSE        1014
#define RPMTAG_GROUP          1016
#define RPMTAG_URL            1020
#define RPMTAG_OS             1021
#define RPMTAG_ARCH           1022
#define RPMTAG_FILENAMES      1027
#define RPMTAG_REQUIRENAME    1049
#define RPMTAG_REQUIREVERSION 1050
#define RPMTAG_BASENAMES      1117

#define RPM_TYPE_NULL         0
#define RPM_TYPE_CHAR         1
#define RPM_TYPE_INT8         2
#define RPM_TYPE_INT16        3
#define RPM_TYPE_INT32        4
#define RPM_TYPE_INT64        5
#define RPM_TYPE_STRING       6
#define RPM_TYPE_BIN          7
#define RPM_TYPE_STRING_ARRAY 8
#define RPM_TYPE_I18NSTRING   9

typedef struct __attribute__((packed)) {
    uint8_t magic[4];        // 0xed, 0xab, 0xee, 0xdb
    uint8_t major;           // 3
    uint8_t minor;           // 0
    uint16_t type;           // 0 = binary, 1 = source
    uint16_t archnum;        // 1 = i386, 2 = Alpha, 3 = SPARC, etc.
    char name[66];           // Package name null-padded
    uint16_t osnum;          // 1 = Linux
    uint16_t signature_type; // 5 = Header-style signature
    uint8_t reserved[16];    // Pad to 96 bytes
} rpm_lead_t;

typedef struct __attribute__((packed)) {
    uint8_t magic[3];        // 0x8e, 0xad, 0xe8
    uint8_t version;         // 0x01
    uint32_t reserved;
    uint32_t nindex;         // Number of index entries (big-endian)
    uint32_t datasize;       // Total bytes in data store (big-endian)
} rpm_header_intro_t;

typedef struct __attribute__((packed)) {
    uint32_t tag;            // Tag identifier (big-endian)
    uint32_t type;           // Data type (big-endian)
    uint32_t offset;         // Byte offset in data store (big-endian)
    uint32_t count;          // Count of items (big-endian)
} rpm_index_entry_t;

int cmd_rpm(const char *args);

#endif // AARAMBH_RPM_TOOL_H
