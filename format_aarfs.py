import struct, sys, zlib
from pathlib import Path
SECTOR = 512
SECTORS = 262144                 # 128 MiB disposable PM32 disk
TABLE_A, TABLE_B = 66, 322      # 2048 x 64-byte entries = 256 sectors
TABLE_SECTORS = 256
DATA_START, SLOT_SECTORS = 578, 32
MAGIC = b'AARFS32\0'

def make_super(generation=1, slot=0):
    b=bytearray(SECTOR)
    b[:8]=MAGIC
    struct.pack_into('<IIIIIIII', b, 8, 1, generation, slot, SECTORS,
                     zlib.crc32(bytes(TABLE_SECTORS*SECTOR)) & 0xffffffff,
                     TABLE_A, TABLE_B, DATA_START)
    struct.pack_into('<II', b, 40, 2048, SLOT_SECTORS)
    struct.pack_into('<I', b, 508, zlib.crc32(b[:508]) & 0xffffffff)
    return b

def check(path):
    if path.stat().st_size != SECTORS*SECTOR: raise SystemExit('Unexpected PM32 disk size.')
    with path.open('rb') as f:
        f.seek(64*SECTOR)
        s=f.read(SECTOR)
    if s[:8] != MAGIC or struct.unpack_from('<I',s,508)[0] != zlib.crc32(s[:508]) & 0xffffffff:
        raise SystemExit('AarFS superblock missing or corrupt; refusing to format existing disk.')
    print(f'Existing AarFS disk verified and preserved: {path}')

mode, name = sys.argv[1], Path(sys.argv[2])
if mode == '--check':
    check(name)
elif mode == '--create':
    with name.open('xb') as f:
        f.truncate(SECTORS*SECTOR)
        f.seek(64*SECTOR); f.write(make_super())
        f.seek(65*SECTOR); f.write(bytes(SECTOR))
        f.seek(TABLE_A*SECTOR); f.write(bytes(TABLE_SECTORS*SECTOR))
        f.seek(TABLE_B*SECTOR); f.write(bytes(TABLE_SECTORS*SECTOR))
    print(f'Created fresh AarFS virtual disk: {name} (128 MiB)')
else: raise SystemExit('Use --create for a new image or --check for an existing one.')
