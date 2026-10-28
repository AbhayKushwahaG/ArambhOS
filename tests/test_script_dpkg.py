"""Test shell script runner (run/sh) and Debian package tool (dpkg/deb)."""
import sys
import os
import shutil
from pathlib import Path

sys.path.insert(0, os.path.dirname(__file__))
from qemu_harness import QemuSession
from aarfs_tool import inject_aarfs_file, create_deb_archive

def main():
    print("Testing Shell Script Runner and Debian Package Manager...")
    test_img = Path("test_sh_deb.img")
    shutil.copyfile("aarambh-v04-pm32-fs.img", test_img)

    # 1. Prepare test script
    script_text = (
        "# Shell script test\n"
        "APP=Calculator\n"
        "echo Starting $APP runner\n"
        "echo Executing as user: $USER\n"
        "whoami\n"
        "echo Last command status: $?\n"
        "exit 0\n"
    )
    inject_aarfs_file(test_img, "test.sh", script_text)

    # 2. Prepare test debian package
    deb_data = create_deb_archive(
        pkg_name="hello-app",
        version="1.2.3",
        arch="i386",
        desc="Aarambh Hello Test Utility",
        payload_data="Hello World binary payload for Aarambh OS\n"
    )
    inject_aarfs_file(test_img, "hello.deb", deb_data)

    try:
        with QemuSession(image=test_img) as vm:
            vm.wait_prompt(0)
            print("1. Testing 'run test.sh'...")
            out = vm.send_line("run test.sh")
            print("--- Output from 'run test.sh' ---\n" + out)
            assert "Starting Calculator runner" in out, "Failed variable expansion $APP"
            assert "Executing as user: root" in out, "Failed variable expansion $USER"
            assert "root" in out, "Failed whoami execution"
            assert "Last command status: 0" in out, "Failed status $? expansion"
            print("   Shell script execution and variable expansion verified!")

            print("2. Testing 'dpkg -I hello.deb' (Info inspection)...")
            out_info = vm.send_line("dpkg -I hello.deb")
            print("--- Output from 'dpkg -I' ---\n" + out_info)
            assert "Package: hello-app" in out_info
            assert "Version: 1.2.3" in out_info
            assert "Architecture: i386" in out_info
            print("   dpkg -I verified!")

            print("3. Testing 'dpkg -c hello.deb' (Archive content listing)...")
            out_contents = vm.send_line("dpkg -c hello.deb")
            print("--- Output from 'dpkg -c' ---\n" + out_contents)
            assert "debian-binary" in out_contents
            assert "control" in out_contents
            assert "data" in out_contents
            print("   dpkg -c verified!")

            print("4. Testing 'dpkg -i hello.deb' (Package installation)...")
            out_inst = vm.send_line("dpkg -i hello.deb")
            print("--- Output from 'dpkg -i' ---\n" + out_inst)
            assert "Installing 'hello.deb' into AarFS" in out_inst
            assert "status database updated successfully" in out_inst
            print("   dpkg -i verified!")

            print("5. Testing 'dpkg -l' (List installed packages)...")
            out_list = vm.send_line("dpkg -l")
            print("--- Output from 'dpkg -l' ---\n" + out_list)
            assert "hello-app" in out_list
            assert "1.2.3" in out_list
            assert "i386" in out_list
            print("   dpkg -l verified!")

    finally:
        if test_img.exists():
            test_img.unlink()

    print("ALL SCRIPT & DPKG TESTS PASSED 100%!")

if __name__ == "__main__":
    main()
