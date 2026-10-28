"""Automated QEMU test suite for AarOS VFS and ELF32 binary inspector."""
import os
import sys
import shutil
import time
from pathlib import Path

sys.path.insert(0, os.path.dirname(__file__))
from qemu_harness import QemuSession
from aarfs_tool import inject_aarfs_file, create_elf32_binary

def main():
    print("Testing Aarambh OS VFS and ELF32 Binary Inspection Layer...")
    test_img = Path("test_elf_vfs.img")
    shutil.copyfile("aarambh-v04-pm32-fs.img", test_img)

    # 1. Inject a static freestanding ELF32 binary
    static_elf = create_elf32_binary(entry_point=0x08048080, is_dynamic=False)
    inject_aarfs_file(test_img, "app_static.elf", static_elf)

    # 2. Inject a dynamic Linux ELF32 binary with interp
    dynamic_elf = create_elf32_binary(entry_point=0x00010000, is_dynamic=True, interp="/lib/ld-linux.so.2")
    inject_aarfs_file(test_img, "app_dyn.elf", dynamic_elf)

    try:
        with QemuSession(image=test_img) as vm:
            vm.wait_prompt(0)

            print("1. Testing 'elfinfo app_static.elf'...")
            out_static = vm.send_line("elfinfo app_static.elf")
            print("--- Output from static ELF inspection ---\n" + out_static)
            assert "ELF32 (32-bit x86 Standard)" in out_static, "Failed ELF32 detection"
            assert "Intel 80386 / i686" in out_static, "Failed architecture detection"
            assert "ET_EXEC" in out_static, "Failed ET_EXEC binary type"
            assert "0x08048080" in out_static, "Failed entry point address"
            assert "LOAD" in out_static, "Failed LOAD segment detection"
            assert ".text" in out_static, "Failed .text section detection"
            assert "[STATIC]  Statically Linked Freestanding x86 ELF Binary" in out_static, "Failed static assessment"
            print("   Static ELF32 inspection verified!")

            print("2. Testing 'elf app_dyn.elf' (using alias 'elf')...")
            out_dyn = vm.send_line("elf app_dyn.elf")
            print("--- Output from dynamic ELF inspection ---\n" + out_dyn)
            assert "ELF32 (32-bit x86 Standard)" in out_dyn, "Failed ELF32 detection"
            assert "ET_DYN" in out_dyn, "Failed ET_DYN detection"
            assert "INTERP Dynamic Linker Path: '/lib/ld-linux.so.2'" in out_dyn, "Failed dynamic linker path"
            assert "[DYNAMIC] Requires Linux Dynamic Linker" in out_dyn, "Failed dynamic assessment"
            assert "Requires POSIX system call layer & glibc runtime" in out_dyn, "Failed POSIX roadmap diagnostic"
            print("   Dynamic ELF32 inspection & alias verified!")

            print("3. Testing 'elfinfo' with invalid file...")
            out_err = vm.send_line("elfinfo missing.elf")
            print("--- Output from missing file inspection ---\n" + out_err)
            assert "cannot read 'missing.elf'" in out_err, "Failed missing file error handling"
            print("   Error handling verified!")

    finally:
        time.sleep(0.5)
        if test_img.exists():
            try:
                test_img.unlink()
            except OSError:
                pass

    print("ALL VFS & ELF32 TESTS PASSED 100%!")

if __name__ == "__main__":
    main()
