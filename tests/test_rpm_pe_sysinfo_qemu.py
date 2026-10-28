"""Automated QEMU test suite for RPM package manager, Windows PE32/Wine inspector, and sysinfo."""
import os
import sys
import shutil
import time
from pathlib import Path

sys.path.insert(0, os.path.dirname(__file__))
from qemu_harness import QemuSession
from aarfs_tool import inject_aarfs_file, create_rpm_package, create_pe32_executable

def main():
    print("Testing Aarambh OS RPM, Windows PE32, and System Diagnostics Layer...")
    test_img = Path("test_rpm_pe.img")
    shutil.copyfile("aarambh-v04-pm32-fs.img", test_img)

    # 1. Inject sample RPM package
    rpm_bytes = create_rpm_package(
        pkg_name="aarambh-core",
        version="2.4",
        release="1",
        arch="i386",
        summary="Aarambh Core System Utilities",
        reqs=["libc.so.6", "libm.so.6"],
        files=["/bin/coretools", "/etc/core.cfg"]
    )
    inject_aarfs_file(test_img, "core.rpm", rpm_bytes)

    # 2. Inject sample Windows PE32 executable
    pe_bytes = create_pe32_executable(
        subsystem=3,  # Windows CUI
        is_dll=False,
        dlls=[
            ("KERNEL32.dll", ["ExitProcess", "GetStdHandle", "WriteFile"]),
            ("USER32.dll", ["MessageBoxA"])
        ]
    )
    inject_aarfs_file(test_img, "tool.exe", pe_bytes)

    try:
        with QemuSession(image=test_img) as vm:
            vm.wait_prompt(0)

            # --- RPM Tests ---
            print("1. Testing 'rpm core.rpm'...")
            out_rpm = vm.send_line("rpm core.rpm")
            print("--- Output from 'rpm core.rpm' ---\n" + out_rpm)
            assert "Package:      aarambh-core" in out_rpm, "Failed RPM package name"
            assert "Version:      2.4-1" in out_rpm, "Failed RPM version"
            assert "Architecture: i386" in out_rpm, "Failed RPM arch"
            assert "Summary:      Aarambh Core System Utilities" in out_rpm, "Failed RPM summary"
            assert "[ARCH OK] Architecture compatible (32-bit x86 / noarch)" in out_rpm, "Failed RPM arch assessment"
            assert "Requires Linux POSIX system call layer & glibc runtime" in out_rpm, "Failed RPM runtime diagnostic"
            print("   RPM default inspection verified!")

            print("2. Testing 'rpm -ql core.rpm' (File list)...")
            out_ql = vm.send_line("rpm -ql core.rpm")
            print("--- Output from 'rpm -ql' ---\n" + out_ql)
            assert "/bin/coretools" in out_ql, "Failed RPM file list"
            assert "/etc/core.cfg" in out_ql, "Failed RPM file list"
            print("   RPM -ql verified!")

            print("3. Testing 'rpm -qR core.rpm' (Requirements list)...")
            out_qr = vm.send_line("rpm -qR core.rpm")
            print("--- Output from 'rpm -qR' ---\n" + out_qr)
            assert "libc.so.6" in out_qr, "Failed RPM requirement libc.so.6"
            assert "libm.so.6" in out_qr, "Failed RPM requirement libm.so.6"
            print("   RPM -qR verified!")

            # --- Windows PE32 Tests ---
            print("4. Testing 'peinfo tool.exe'...")
            out_pe = vm.send_line("peinfo tool.exe")
            print("--- Output from 'peinfo tool.exe' ---\n" + out_pe)
            assert "Machine:      i386 (Intel 32-bit x86 Standard)" in out_pe, "Failed PE machine"
            assert "Format:       PE32 (32-bit Portable Executable)" in out_pe, "Failed PE format"
            assert "Subsystem:    Windows CUI (Console Application)" in out_pe, "Failed PE subsystem"
            assert ".text" in out_pe, "Failed PE .text section"
            assert ".rdata" in out_pe, "Failed PE .rdata section"
            assert "KERNEL32.dll" in out_pe, "Failed imported KERNEL32.dll"
            assert "USER32.dll" in out_pe, "Failed imported USER32.dll"
            assert "ExitProcess" in out_pe, "Failed imported function ExitProcess"
            assert "MessageBoxA" in out_pe, "Failed imported function MessageBoxA"
            assert "[ARCH OK] Architecture matches native 32-bit x86 Protected Mode" in out_pe, "Failed Wine arch assessment"
            assert "Target for Phase 5 Wine KERNEL32 console runtime" in out_pe, "Failed Wine console roadmap"
            print("   Windows PE32 inspection verified!")

            print("5. Testing 'wine-check tool.exe' (Alias)...")
            out_alias = vm.send_line("wine-check tool.exe")
            assert "WINDOWS PE EXECUTABLE INSPECTION REPORT" in out_alias, "Failed wine-check alias"
            print("   wine-check alias verified!")

            # --- Diagnostics Tests ---
            print("6. Testing 'sysinfo'...")
            out_sys = vm.send_line("sysinfo")
            print("--- Output from 'sysinfo' ---\n" + out_sys)
            assert "Aarambh OS v0.4" in out_sys, "Failed sysinfo OS name"
            assert "Monolithic 32-bit Protected Mode (Ring 0)" in out_sys, "Failed kernel arch"
            assert "Flat 4 GB Segmentation" in out_sys, "Failed memory model"
            assert "AarFS v0.4" in out_sys, "Failed filesystem name"
            assert "POSIX File Descriptors 0..15" in out_sys, "Failed VFS in sysinfo"
            print("   sysinfo verified!")

            print("7. Testing 'hwinfo'...")
            out_hw = vm.send_line("hwinfo")
            print("--- Output from 'hwinfo' ---\n" + out_hw)
            assert "PCI device(s) on host bus" in out_hw, "Failed PCI device count"
            assert "BAR0" in out_hw, "Failed BAR enumeration"
            print("   hwinfo verified!")

            print("8. Testing 'compat'...")
            out_compat = vm.send_line("compat")
            print("--- Output from 'compat' ---\n" + out_compat)
            assert "[FUNCTIONAL]" in out_compat, "Failed functional tag"
            assert "[ROADMAP Phase" in out_compat, "Failed roadmap tag"
            assert "Linux System Calls" in out_compat, "Failed Linux syscall item"
            assert "Windows Wine Emulation" in out_compat, "Failed Wine item"
            assert "Web Browser Engine" in out_compat, "Failed browser item"
            assert "Java Virtual Machine" in out_compat, "Failed JVM item"
            print("   compat verified!")

    finally:
        time.sleep(0.5)
        if test_img.exists():
            try:
                test_img.unlink()
            except OSError:
                pass

    print("ALL RPM, PE32, AND SYSINFO TESTS PASSED 100%!")

if __name__ == "__main__":
    main()
