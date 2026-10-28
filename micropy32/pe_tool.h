#ifndef AARAMBH_PE_TOOL_H
#define AARAMBH_PE_TOOL_H

#include <stddef.h>
#include <stdint.h>

#define IMAGE_DOS_SIGNATURE           0x5A4D      // "MZ"
#define IMAGE_NT_SIGNATURE            0x00004550  // "PE\0\0"

#define IMAGE_FILE_MACHINE_I386       0x014c
#define IMAGE_FILE_MACHINE_AMD64      0x8664

#define IMAGE_FILE_EXECUTABLE_IMAGE   0x0002
#define IMAGE_FILE_DLL                0x2000

#define IMAGE_NT_OPTIONAL_HDR32_MAGIC 0x10b
#define IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x20b

#define IMAGE_SUBSYSTEM_NATIVE        1
#define IMAGE_SUBSYSTEM_WINDOWS_GUI   2
#define IMAGE_SUBSYSTEM_WINDOWS_CUI   3
#define IMAGE_SUBSYSTEM_POSIX_CUI     7

#define IMAGE_DIRECTORY_ENTRY_EXPORT    0
#define IMAGE_DIRECTORY_ENTRY_IMPORT    1
#define IMAGE_DIRECTORY_ENTRY_RESOURCE  2
#define IMAGE_DIRECTORY_ENTRY_EXCEPTION 3
#define IMAGE_DIRECTORY_ENTRY_SECURITY  4
#define IMAGE_DIRECTORY_ENTRY_BASERELOC 5
#define IMAGE_DIRECTORY_ENTRY_DEBUG     6

typedef struct __attribute__((packed)) {
    uint16_t e_magic;       // Magic number ("MZ")
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    int32_t  e_lfanew;      // File offset of NT PE header
} image_dos_header_t;

typedef struct __attribute__((packed)) {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} image_file_header_t;

typedef struct __attribute__((packed)) {
    uint32_t VirtualAddress;
    uint32_t Size;
} image_data_directory_t;

typedef struct __attribute__((packed)) {
    uint16_t Magic;
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint32_t BaseOfData;
    uint32_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint32_t SizeOfStackReserve;
    uint32_t SizeOfStackCommit;
    uint32_t SizeOfHeapReserve;
    uint32_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    image_data_directory_t DataDirectory[16];
} image_optional_header32_t;

typedef struct __attribute__((packed)) {
    uint8_t  Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} image_section_header_t;

typedef struct __attribute__((packed)) {
    uint32_t OriginalFirstThunk; // RVA to INT
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;               // RVA to DLL name
    uint32_t FirstThunk;         // RVA to IAT
} image_import_descriptor_t;

int cmd_peinfo(const char *args);

#endif // AARAMBH_PE_TOOL_H
