"""AarFS image inspection and file/package injection tools for tests."""
import struct
import zlib
from pathlib import Path

SECTOR = 512
MAGIC = b'AARFS32\0'

def inject_aarfs_file(img_path: Path, filename: str, data: bytes | str, parent_id: int = 0, mode: int = 0o755) -> int:
    """Inject a file directly into an AarFS 128 MiB image."""
    if isinstance(data, str):
        data = data.encode('utf-8')
    if len(data) > 16384:
        raise ValueError(f"File size {len(data)} exceeds 16 KiB AarFS slot limit")

    with open(img_path, 'r+b') as f:
        f.seek(64 * SECTOR)
        sb = bytearray(f.read(SECTOR))
        if sb[:8] != MAGIC:
            raise ValueError(f"Not an AarFS image: {sb[:8]}")

        magic, ver, gen, active_slot, sectors, crc_t, tbl_a, tbl_b, data_start, max_ent, slot_sec = struct.unpack_from('<8sIIIIIIIIII', sb, 0)
        tbl_sec = tbl_a if active_slot == 0 else tbl_b
        f.seek(tbl_sec * SECTOR)
        tbl = bytearray(f.read(max_ent * 64))

        # Check if file with same name already exists in parent_id
        target_id = -1
        for i in range(1, max_ent):
            off = (i - 1) * 64
            if tbl[off] != 0:
                p_id = struct.unpack_from('<I', tbl, off + 4)[0]
                n = tbl[off + 8: off + 48].split(b'\0')[0].decode('latin1')
                if p_id == parent_id and n.lower() == filename.lower():
                    target_id = i
                    break

        if target_id == -1:
            # Find first free entry
            for i in range(1, max_ent):
                off = (i - 1) * 64
                if tbl[off] == 0:
                    target_id = i
                    break
        if target_id <= 0:
            raise RuntimeError("No free AarFS entries available")

        # Prepare entry
        e_off = (target_id - 1) * 64
        name_bytes = filename.encode('latin1')[:39] + b'\0' * (40 - len(filename[:39]))
        struct.pack_into('<BBBBI40sIHHI', tbl, e_off,
                         1, 1, 0, 0, parent_id, name_bytes, len(data), 0, 0, mode)

        # Write file data into slot 0 extent
        slot_lba = data_start + ((target_id - 1) * 2 + 0) * slot_sec
        f.seek(slot_lba * SECTOR)
        f.write(data)
        # Pad extent to 512-byte boundary
        rem = len(data) % SECTOR
        if rem > 0:
            f.write(b'\0' * (SECTOR - rem))

        # Write updated tables
        tbl_crc = zlib.crc32(tbl) & 0xffffffff
        f.seek(tbl_a * SECTOR)
        f.write(tbl)
        f.seek(tbl_b * SECTOR)
        f.write(tbl)

        # Update superblock
        gen += 1
        struct.pack_into('<II', sb, 12, gen, active_slot)
        struct.pack_into('<I', sb, 24, tbl_crc)
        sb_crc = zlib.crc32(sb[:508]) & 0xffffffff
        struct.pack_into('<I', sb, 508, sb_crc)
        f.seek(64 * SECTOR)
        f.write(sb)

    return target_id

def create_deb_archive(pkg_name: str, version: str, arch: str, desc: str, payload_data: bytes | str) -> bytes:
    """Create a standard Debian binary package (.deb) archive format."""
    if isinstance(payload_data, str):
        payload_data = payload_data.encode('utf-8')

    deb_bin_content = b"2.0\n"
    control_content = f"Package: {pkg_name}\nVersion: {version}\nArchitecture: {arch}\nMaintainer: Gentix <gentix@aarambh.org>\nDescription: {desc}\n".encode('utf-8')

    def make_ar_header(name: str, size: int) -> bytes:
        # AR header: 16 name, 12 date, 6 uid, 6 gid, 8 mode, 10 size, 2 fmag (`\n)
        h = bytearray(60)
        h[0:16] = f"{name:<16}".encode('latin1')
        h[16:28] = b"0           "
        h[28:34] = b"0     "
        h[34:40] = b"0     "
        h[40:48] = b"100644  "
        h[48:58] = f"{size:<10}".encode('latin1')
        h[58:60] = b"`\n"
        return bytes(h)

    out = bytearray(b"!<arch>\n")

    # Member 1: debian-binary
    out.extend(make_ar_header("debian-binary", len(deb_bin_content)))
    out.extend(deb_bin_content)
    if len(out) % 2 != 0:
        out.append(0x0A)

    # Member 2: control
    out.extend(make_ar_header("control", len(control_content)))
    out.extend(control_content)
    if len(out) % 2 != 0:
        out.append(0x0A)

    # Member 3: data
    out.extend(make_ar_header("data", len(payload_data)))
    out.extend(payload_data)
    if len(out) % 2 != 0:
        out.append(0x0A)

    return bytes(out)


def create_elf32_binary(entry_point: int = 0x08048080, is_dynamic: bool = False, interp: str = "/lib/ld-linux.so.2") -> bytes:
    """Create a minimal valid 32-bit x86 ELF executable binary."""
    # Sections: .text and .shstrtab
    text_data = b"\x31\xc0\x40\xcd\x80"  # xor eax, eax; inc eax; int 0x80 (exit 1)
    interp_bytes = interp.encode('ascii') + b"\0" if is_dynamic else b""

    # Offsets layout:
    # 0x00: ELF Header (52 bytes)
    # 0x34: Program Headers (1 or 2 entries, 32 bytes each)
    num_ph = 2 if is_dynamic else 1
    ph_size = num_ph * 32
    body_offset = 52 + ph_size

    interp_offset = body_offset
    text_offset = body_offset + len(interp_bytes)
    # Align text_offset to 16 bytes
    text_offset = (text_offset + 15) & ~15
    text_vaddr = entry_point

    shstrtab = b"\0.text\0.shstrtab\0"
    if is_dynamic:
        shstrtab += b".interp\0"

    shstrtab_offset = text_offset + len(text_data)
    shstrtab_offset = (shstrtab_offset + 15) & ~15

    sh_offset = shstrtab_offset + len(shstrtab)
    sh_offset = (sh_offset + 15) & ~15
    num_sh = 4 if is_dynamic else 3

    out = bytearray(sh_offset + num_sh * 40)

    # ELF Header
    out[0:4] = b"\x7fELF"
    out[4] = 1  # ELFCLASS32
    out[5] = 1  # ELFDATA2LSB
    out[6] = 1  # EV_CURRENT
    out[7] = 0  # ELFOSABI_NONE
    e_type = 3 if is_dynamic else 2  # ET_DYN vs ET_EXEC
    e_machine = 3  # EM_386
    struct.pack_into('<HHIIIIIHHHHHH', out, 16,
                     e_type, e_machine, 1, entry_point,
                     52, sh_offset, 0, 52, 32, num_ph, 40, num_sh, 2)

    # Program Header 0: PT_LOAD (.text)
    # Type 1, Offset, VAddr, PAddr, FileSz, MemSz, Flags 0x5 (R-X), Align 0x1000
    struct.pack_into('<IIIIIIII', out, 52,
                     1, text_offset, text_vaddr, text_vaddr,
                     len(text_data), len(text_data), 0x5, 0x1000)

    # Program Header 1 (Optional): PT_INTERP
    if is_dynamic:
        struct.pack_into('<IIIIIIII', out, 52 + 32,
                         3, interp_offset, 0x08047f00, 0x08047f00,
                         len(interp_bytes), len(interp_bytes), 0x4, 1)
        out[interp_offset : interp_offset + len(interp_bytes)] = interp_bytes

    # Text data
    out[text_offset : text_offset + len(text_data)] = text_data

    # Shstrtab data
    out[shstrtab_offset : shstrtab_offset + len(shstrtab)] = shstrtab

    # Section Headers
    # 0: NULL
    # 1: .text (sh_name=1)
    struct.pack_into('<IIIIIIIIII', out, sh_offset + 40,
                     1, 1, 0x6, text_vaddr, text_offset, len(text_data), 0, 0, 16, 0)
    # 2: .shstrtab (sh_name=7)
    struct.pack_into('<IIIIIIIIII', out, sh_offset + 80,
                     7, 3, 0, 0, shstrtab_offset, len(shstrtab), 0, 0, 1, 0)

    return bytes(out)


def create_rpm_package(pkg_name: str = "hello-rpm", version: str = "2.10", release: str = "1",
                       arch: str = "i386", summary: str = "AarOS Hello RPM Package",
                       reqs: list[str] = None, files: list[str] = None) -> bytes:
    """Create a minimal valid RPM binary package file."""
    if reqs is None:
        reqs = ["libc.so.6"]
    if files is None:
        files = ["/usr/bin/hello", "/etc/hello.conf"]

    # 1. Lead (96 bytes)
    lead = bytearray(96)
    lead[0:4] = bytes([0xed, 0xab, 0xee, 0xdb])
    lead[4] = 3  # Major version 3
    lead[5] = 0  # Minor version 0
    struct.pack_into('>H', lead, 6, 0)   # Type: 0 = binary
    arch_id = 1 if arch in ("i386", "i686", "noarch") else 2
    struct.pack_into('>H', lead, 8, arch_id)  # Arch: 1 = i386
    name_bytes = pkg_name.encode('latin1')[:65]
    lead[10 : 10 + len(name_bytes)] = name_bytes
    struct.pack_into('>H', lead, 76, 1)  # OS: 1 = Linux
    struct.pack_into('>H', lead, 78, 5)  # Signature type: 5

    # 2. Build Header Data Store and Index Table
    data_store = bytearray()
    index_entries = []

    def add_string(tag: int, s: str):
        nonlocal data_store, index_entries
        off = len(data_store)
        sb = s.encode('utf-8') + b'\0'
        data_store.extend(sb)
        index_entries.append((tag, 6, off, 1))  # Type 6 = RPM_TYPE_STRING

    def add_string_array(tag: int, arr: list[str]):
        nonlocal data_store, index_entries
        off = len(data_store)
        for item in arr:
            data_store.extend(item.encode('utf-8') + b'\0')
        index_entries.append((tag, 8, off, len(arr)))  # Type 8 = RPM_TYPE_STRING_ARRAY

    add_string(1000, pkg_name)    # RPMTAG_NAME
    add_string(1001, version)     # RPMTAG_VERSION
    add_string(1002, release)     # RPMTAG_RELEASE
    add_string(1004, summary)     # RPMTAG_SUMMARY
    add_string(1014, "GPLv2+")    # RPMTAG_LICENSE
    add_string(1016, "Utilities") # RPMTAG_GROUP
    add_string(1022, arch)        # RPMTAG_ARCH
    add_string_array(1049, reqs)  # RPMTAG_REQUIRENAME
    add_string_array(1117, files) # RPMTAG_BASENAMES

    nindex = len(index_entries)
    datasize = len(data_store)

    header_intro = bytearray(16)
    header_intro[0:3] = bytes([0x8e, 0xad, 0xe8])
    header_intro[3] = 0x01
    struct.pack_into('>I', header_intro, 8, nindex)
    struct.pack_into('>I', header_intro, 12, datasize)

    index_table = bytearray(nindex * 16)
    for i, (tag, ttype, off, count) in enumerate(index_entries):
        struct.pack_into('>IIII', index_table, i * 16, tag, ttype, off, count)

    return bytes(lead) + bytes(header_intro) + bytes(index_table) + bytes(data_store)


def create_pe32_executable(subsystem: int = 3, is_dll: bool = False,
                           dlls: list[tuple[str, list[str]]] = None) -> bytes:
    """Create a minimal valid 32-bit Windows PE executable."""
    if dlls is None:
        dlls = [("KERNEL32.dll", ["ExitProcess", "GetStdHandle", "WriteFile"])]

    # Layout:
    # 0x000..0x040: DOS Header (64 bytes)
    # 0x040..0x080: DOS Stub
    # 0x080..0x084: PE Signature (4 bytes)
    # 0x084..0x098: File Header (20 bytes)
    # 0x098..0x178: Optional Header (224 bytes)
    # 0x178..0x1C8: Section Headers (2 * 40 = 80 bytes)
    # 0x1C8..0x200: Header Padding
    # 0x200..0x400: .text section (code)
    # 0x400..0x800: .rdata section (imports)

    pe_offset = 0x80
    out = bytearray(0x800)

    # 1. DOS Header
    struct.pack_into('<H', out, 0, 0x5A4D)  # "MZ"
    struct.pack_into('<I', out, 0x3C, pe_offset)  # e_lfanew

    # 2. PE Signature
    out[pe_offset : pe_offset + 4] = b"PE\0\0"

    # 3. File Header
    # Machine: 0x014c (i386), NumSections: 2, Characteristics: 0x0102 or 0x2102
    num_sections = 2
    chars = 0x2102 if is_dll else 0x0102
    struct.pack_into('<HHIIIHH', out, pe_offset + 4,
                     0x014c, num_sections, 0x67000000, 0, 0, 224, chars)

    # 4. Optional Header (PE32)
    opt_offset = pe_offset + 24
    struct.pack_into('<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII', out, opt_offset,
                     0x010b, 14, 0,
                     0x200, 0x400, 0,
                     0x1000, 0x1000, 0x2000, 0x00400000, 0x1000, 0x200,
                     4, 0, 0, 0, 4, 0,
                     0, 0x3000, 0x200, 0,
                     subsystem, 0,
                     0x100000, 0x1000, 0x100000, 0x1000, 0, 16)

    # Data Directory 1: Import Table (RVA 0x2000, Size 0x200)
    import_rva = 0x2000
    struct.pack_into('<II', out, opt_offset + 96 + 1 * 8, import_rva, 0x200)

    # 5. Section Headers (at opt_offset + 224)
    sec_offset = opt_offset + 224
    # .text: VSize 0x1000, VAddr 0x1000, RawSize 0x200, RawOff 0x200
    struct.pack_into('<8sIIIIIIHHI', out, sec_offset,
                     b".text\0\0\0", 0x1000, 0x1000, 0x200, 0x200, 0, 0, 0, 0, 0x60000020)
    # .rdata: VSize 0x1000, VAddr 0x2000, RawSize 0x400, RawOff 0x400
    struct.pack_into('<8sIIIIIIHHI', out, sec_offset + 40,
                     b".rdata\0\0", 0x1000, 0x2000, 0x400, 0x400, 0, 0, 0, 0, 0x40000040)

    # 6. Fill .text (offset 0x200)
    out[0x200 : 0x204] = b"\x31\xc0\xc3\x90"  # xor eax, eax; ret; nop

    # 7. Fill .rdata (offset 0x400, VAddr 0x2000)
    # Layout within .rdata:
    # 0x00..0x40: Import Descriptors (20 bytes per DLL + 20 byte null terminator)
    # 0x40..0x80: Import Lookup Table (INT)
    # 0x80..0xC0: Import Address Table (IAT)
    # 0xC0..0x200: Strings (DLL names + hint/name entries)
    rdata_off = 0x400
    rdata_vaddr = 0x2000

    num_dlls = len(dlls)
    int_off = rdata_off + (num_dlls + 1) * 20
    iat_off = int_off + 64
    str_off = iat_off + 64

    for i, (dll_name, fn_names) in enumerate(dlls):
        dll_name_rva = rdata_vaddr + (str_off - rdata_off)
        dll_b = dll_name.encode('ascii') + b'\0'
        out[str_off : str_off + len(dll_b)] = dll_b
        str_off += len(dll_b)
        if str_off % 2 != 0:
            str_off += 1

        curr_int_rva = rdata_vaddr + (int_off - rdata_off)
        curr_iat_rva = rdata_vaddr + (iat_off - rdata_off)

        # Import descriptor
        desc_pos = rdata_off + i * 20
        struct.pack_into('<IIIII', out, desc_pos,
                         curr_int_rva, 0, 0, dll_name_rva, curr_iat_rva)

        for fn_name in fn_names:
            fn_rva = rdata_vaddr + (str_off - rdata_off)
            # Hint/Name entry: uint16 Hint, then ASCII string
            hint_name = struct.pack('<H', 0) + fn_name.encode('ascii') + b'\0'
            out[str_off : str_off + len(hint_name)] = hint_name
            str_off += len(hint_name)
            if str_off % 2 != 0:
                str_off += 1

            # Put pointer in INT and IAT
            struct.pack_into('<I', out, int_off, fn_rva)
            struct.pack_into('<I', out, iat_off, fn_rva)
            int_off += 4
            iat_off += 4

        # Null terminate thunks
        struct.pack_into('<I', out, int_off, 0)
        struct.pack_into('<I', out, iat_off, 0)
        int_off += 4
        iat_off += 4

    return bytes(out)

