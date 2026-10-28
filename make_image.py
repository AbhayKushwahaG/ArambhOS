#!/usr/bin/env python3
"""Create an empty 128 MiB BIOS boot disk on first build; preserve valid data later.
The Aarambh kernel is stored before the FAT32 partition at LBA 2048.
"""
import math, struct, sys
from pathlib import Path

SECTOR = 512
TOTAL_SECTORS = 128 * 1024 * 1024 // SECTOR
PART_START = 2048
PART_SECTORS = TOTAL_SECTORS - PART_START
RESERVED = 32
NFATS = 2
SPC = 1
# Iterate FAT size until cluster count and FAT allocation agree.
fatsz = 1
for _ in range(20):
    clusters = (PART_SECTORS - RESERVED - NFATS * fatsz) // SPC
    new_fatsz = math.ceil((clusters + 2) * 4 / SECTOR)
    if new_fatsz == fatsz:
        break
    fatsz = new_fatsz
clusters = (PART_SECTORS - RESERVED - NFATS * fatsz) // SPC
if clusters < 65525:
    raise SystemExit(f'Volume too small for FAT32: {clusters} clusters')

out = Path(sys.argv[1] if len(sys.argv) > 1 else 'aarambh-v04.img')
if out.exists():
    if out.stat().st_size != TOTAL_SECTORS * SECTOR:
        raise SystemExit(f"Refusing to replace existing disk with unexpected size: {out}")
    with out.open('rb') as existing:
        mbr_existing = existing.read(SECTOR)
        existing.seek(PART_START * SECTOR)
        bpb_existing = existing.read(SECTOR)
    valid = (
        mbr_existing[510:512] == b'\x55\xaa'
        and struct.unpack_from('<I', mbr_existing, 454)[0] == PART_START
        and bpb_existing[510:512] == b'\x55\xaa'
        and struct.unpack_from('<H', bpb_existing, 11)[0] == SECTOR
        and bpb_existing[82:90] == b'FAT32   '
    )
    if not valid:
        raise SystemExit(f"Refusing to overwrite an existing non-Aarambh or invalid disk image: {out}")
    print(f"Preserving existing FAT32 data in {out}")
    raise SystemExit(0)
with out.open('wb') as f:
    f.truncate(TOTAL_SECTORS * SECTOR)

mbr = bytearray(SECTOR)
# Partition 1: active, FAT32 LBA type 0x0C. CHS fields are saturated.
o = 446
mbr[o] = 0x80
mbr[o+1:o+4] = b'\xfe\xff\xff'
mbr[o+4] = 0x0C
mbr[o+5:o+8] = b'\xfe\xff\xff'
struct.pack_into('<II', mbr, o+8, PART_START, PART_SECTORS)
mbr[510:512] = b'\x55\xaa'

bpb = bytearray(SECTOR)
bpb[0:3] = b'\xeb\x58\x90'
bpb[3:11] = b'MSDOS5.0'
struct.pack_into('<H', bpb, 11, SECTOR)
bpb[13] = SPC
struct.pack_into('<H', bpb, 14, RESERVED)
bpb[16] = NFATS
struct.pack_into('<H', bpb, 17, 0)
struct.pack_into('<H', bpb, 19, 0)
bpb[21] = 0xF8
struct.pack_into('<H', bpb, 22, 0)
struct.pack_into('<H', bpb, 24, 63)
struct.pack_into('<H', bpb, 26, 255)
struct.pack_into('<I', bpb, 28, PART_START)
struct.pack_into('<I', bpb, 32, PART_SECTORS)
struct.pack_into('<I', bpb, 36, fatsz)
struct.pack_into('<H', bpb, 40, 0)
struct.pack_into('<H', bpb, 42, 0)
struct.pack_into('<I', bpb, 44, 2)  # root cluster
struct.pack_into('<H', bpb, 48, 1)  # FSInfo sector
struct.pack_into('<H', bpb, 50, 6)  # backup boot sector
bpb[64] = 0x80
bpb[66] = 0x29
struct.pack_into('<I', bpb, 67, 0xAABBCC04)
bpb[71:82] = b'AARAMBH    '
bpb[82:90] = b'FAT32   '
bpb[510:512] = b'\x55\xaa'

fsinfo = bytearray(SECTOR)
struct.pack_into('<I', fsinfo, 0, 0x41615252)
struct.pack_into('<I', fsinfo, 484, 0x61417272)
struct.pack_into('<I', fsinfo, 488, clusters - 1)
struct.pack_into('<I', fsinfo, 492, 3)
struct.pack_into('<I', fsinfo, 508, 0xAA550000)

fat = bytearray(SECTOR * fatsz)
struct.pack_into('<III', fat, 0, 0x0FFFFFF8, 0xFFFFFFFF, 0x0FFFFFFF)
# Root directory cluster 2 is allocated and end-of-chain.
struct.pack_into('<I', fat, 12, 0x0FFFFFFF)
with out.open('r+b') as f:
    f.seek(0); f.write(mbr)
    f.seek(SECTOR); f.write(bytes(SECTOR * 63))  # kernel is inserted by build.ps1
    f.seek(PART_START * SECTOR); f.write(bpb)
    f.seek((PART_START + 1) * SECTOR); f.write(fsinfo)
    f.seek((PART_START + 6) * SECTOR); f.write(bpb)
    f.seek((PART_START + 7) * SECTOR); f.write(fsinfo)
    fat1 = PART_START + RESERVED
    f.seek(fat1 * SECTOR); f.write(fat)
    f.seek((fat1 + fatsz) * SECTOR); f.write(fat)
    root_lba = PART_START + RESERVED + NFATS * fatsz
    f.seek(root_lba * SECTOR); f.write(bytes(SECTOR))
print(f'Created {out} ({TOTAL_SECTORS * SECTOR // (1024*1024)} MiB)')
print(f'FAT32 partition: LBA {PART_START}, {PART_SECTORS} sectors; {clusters} clusters; FAT size {fatsz} sectors')
