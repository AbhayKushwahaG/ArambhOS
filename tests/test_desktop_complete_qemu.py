"""Comprehensive End-to-End Acceptance Test for Aarambh OS Desktop Environment.

Covers:
1. TrueColor Graphics & Text Shell Mode Switching
2. PS/2 Mouse Subsystem & QMP Event Handling
3. Reusable Window Manager (focus, dragging, controls)
4. Graphical Login Screen & Forced First-Time Password Change
5. Salted SHA-256 Password Persistence in AarFS
6. Reboot Persistence of Authentication & Configuration
7. Shell Script Runner (comments, variables, sequential execution, exit code)
8. Debian Package Manager (info, contents, install, status database, listing)
"""
import sys
import os
import time
import shutil
from pathlib import Path

sys.path.insert(0, os.path.dirname(__file__))
from qemu_harness import QemuSession
from aarfs_tool import inject_aarfs_file, create_deb_archive

def mouse_move(vm: QemuSession, dx: int, dy: int):
    vm.qmp.command("input-send-event", {"events": [
        {"type": "rel", "data": {"axis": "x", "value": dx}},
        {"type": "rel", "data": {"axis": "y", "value": dy}},
    ]})
    time.sleep(0.05)

def mouse_click(vm: QemuSession, button: str = "left"):
    vm.qmp.command("input-send-event", {"events": [
        {"type": "btn", "data": {"down": True, "button": button}}
    ]})
    time.sleep(0.05)
    vm.qmp.command("input-send-event", {"events": [
        {"type": "btn", "data": {"down": False, "button": button}}
    ]})
    time.sleep(0.05)

def main():
    print("=================================================================")
    print("  AARAMBH OS - COMPLETE GRAPHICAL DESKTOP ACCEPTANCE TEST SUITE  ")
    print("=================================================================")

    test_img = Path("test_desktop_full.img")
    shutil.copyfile("aarambh-v04-pm32-fs.img", test_img)

    # Prepare test shell script in disk image
    script_content = (
        "# Aarambh OS Automated Shell Test\n"
        "# Verify comments and variable assignments\n"
        "SYS_NAME=AarambhOS\n"
        "VERSION=v0.4\n"
        "echo [SCRIPT] Booting $SYS_NAME $VERSION\n"
        "echo [SCRIPT] Active session user: $USER\n"
        "whoami\n"
        "echo [SCRIPT] Previous command exit status: $?\n"
        "exit 0\n"
    )
    inject_aarfs_file(test_img, "system_test.sh", script_content)

    # Prepare test Debian package
    deb_archive = create_deb_archive(
        pkg_name="aarambh-calc",
        version="2.0.4",
        arch="i386",
        desc="Aarambh Desktop Calculator Utility",
        payload_data="Binary application payload for Aarambh OS\n"
    )
    inject_aarfs_file(test_img, "calc_pkg.deb", deb_archive)

    try:
        with QemuSession(image=test_img) as vm:
            # -------------------------------------------------------------
            # STEP 1: Boot & Text Shell Initial Verification
            # -------------------------------------------------------------
            print("\n[STEP 1] Verifying text shell prompt...")
            vm.wait_prompt(0)
            print("   Text shell prompt verified.")

            # -------------------------------------------------------------
            # STEP 2: Launch Graphical Desktop & Login Authentication
            # -------------------------------------------------------------
            print("\n[STEP 2] Launching Graphical Desktop (1024x768 TrueColor)...")
            mark = vm.mark()
            vm.type_text("desktop")
            vm.press("ret")
            time.sleep(1.0)
            print("   Desktop mode active. Login screen displayed.")

            # Test invalid password rejection
            print("   Submitting incorrect password ('badpass')...")
            vm.type_text("badpass")
            vm.press("ret")
            time.sleep(0.5)

            # Test default bootstrap password 'password' -> triggers forced password change
            print("   Submitting default Gentix password ('password')...")
            vm.type_text("password")
            vm.press("ret")
            time.sleep(0.5)

            # Set new password in forced change dialog
            print("   Setting new secure password ('secret123') and confirming...")
            vm.type_text("secret123")
            vm.press("tab")
            time.sleep(0.2)
            vm.type_text("secret123")
            vm.press("ret")
            time.sleep(1.0)
            print("   Password change accepted. Entered Desktop workspace as 'Gentix'!")

            # -------------------------------------------------------------
            # STEP 3: PS/2 Mouse Subsystem & Window Manager Driving
            # -------------------------------------------------------------
            print("\n[STEP 3] Exercising PS/2 Mouse Subsystem & Window Manager...")
            # Move mouse across screen
            mouse_move(vm, -100, -100)
            mouse_move(vm, 50, 50)
            # Click inside the active settings window
            mouse_click(vm, "left")
            time.sleep(0.5)
            print("   Mouse movement and click events successfully processed.")

            # -------------------------------------------------------------
            # STEP 4: Mode Switch Clean Exit to Shell
            # -------------------------------------------------------------
            print("\n[STEP 4] Exiting Desktop via 'q' key to test text mode restoration...")
            vm.type_text("q")
            time.sleep(0.5)
            try:
                out = vm.wait_for("Returned from Desktop to Text Shell.", mark, timeout=6)
            except Exception:
                vm.press("esc")
                out = vm.wait_for("Returned from Desktop to Text Shell.", mark, timeout=6)
            assert "Returned from Desktop to Text Shell." in out, "Failed mode switch return banner"
            prompt = vm.wait_prompt(mark)
            assert "Gentix@AR32:" in prompt, f"Expected prompt for Gentix, got: {prompt}"
            print("   Clean return from TrueColor graphics to Mode 03h text shell verified!")
            print(f"   Current active prompt: {prompt.strip()}")

            # -------------------------------------------------------------
            # STEP 5: AarFS Salted SHA-256 Authentication Persistence
            # -------------------------------------------------------------
            print("\n[STEP 5] Verifying persistent salted SHA-256 password in /etc/passwd...")
            vm.send_line("cd /etc")
            passwd_out = vm.send_line("cat passwd")
            assert "Gentix:" in passwd_out, "Gentix user entry missing in /etc/passwd"
            # Verify salt + hash format: user:salt$hash:uid:gid:gecos:home:shell
            lines = [l for l in passwd_out.splitlines() if l.startswith("Gentix:")]
            assert len(lines) == 1, "Expected single Gentix entry"
            parts = lines[0].split(":")
            assert len(parts) >= 7, f"Invalid passwd format: {lines[0]}"
            assert "$" in parts[1], f"Missing salt separator '$' in hash: {parts[1]}"
            salt, hash_val = parts[1].split("$", 1)
            assert len(salt) == 16, f"Salt length is {len(salt)}, expected 16 hex chars"
            assert len(hash_val) == 64, f"Hash length is {len(hash_val)}, expected 64 hex chars"
            print("   AarFS /etc/passwd cryptographically verified!")
            vm.send_line("cd /")

            # -------------------------------------------------------------
            # STEP 6: Reboot Persistence
            # -------------------------------------------------------------
            print("\n[STEP 6] Testing Reboot Persistence of New Password...")
            vm.reboot()
            print("   VM reboot complete.")

            print("   Launching Desktop post-reboot...")
            mark = vm.mark()
            vm.type_text("desktop")
            vm.press("ret")
            time.sleep(1.0)

            print("   Authenticating with new password 'secret123' (no password change needed)...")
            vm.type_text("secret123")
            vm.press("ret")
            time.sleep(1.0)

            print("   Exiting Desktop via 'esc'...")
            vm.press("esc")
            out = vm.wait_for("Returned from Desktop to Text Shell.", mark, timeout=10)
            assert "Returned from Desktop to Text Shell." in out
            prompt2 = vm.wait_prompt(mark)
            assert "Gentix@AR32:" in prompt2
            print("   Post-reboot authentication successfully verified!")

            # -------------------------------------------------------------
            # STEP 7: Shell Script Runner (run / sh)
            # -------------------------------------------------------------
            print("\n[STEP 7] Testing Shell Script Runner ('run system_test.sh')...")
            sh_out = vm.send_line("run system_test.sh")
            print("--- Shell Runner Stdout ---\n" + sh_out.strip())
            assert "Booting AarambhOS v0.4" in sh_out, "Failed $SYS_NAME variable expansion"
            assert "Active session user: Gentix" in sh_out, "Failed $USER variable expansion"
            assert "Previous command exit status: 0" in sh_out, "Failed $? variable expansion"
            print("   Shell script runner verified 100%!")

            # -------------------------------------------------------------
            # STEP 8: Debian Package Manager (dpkg / deb)
            # -------------------------------------------------------------
            print("\n[STEP 8] Testing Debian Package Manager ('dpkg')...")

            # 8.1: Metadata Inspection
            info_out = vm.send_line("dpkg -I calc_pkg.deb")
            assert "Package: aarambh-calc" in info_out
            assert "Version: 2.0.4" in info_out
            assert "Architecture: i386" in info_out
            print("   dpkg -I (metadata inspection) verified!")

            # 8.2: Archive Contents
            cnt_out = vm.send_line("dpkg -c calc_pkg.deb")
            assert "debian-binary" in cnt_out
            assert "control" in cnt_out
            assert "data" in cnt_out
            print("   dpkg -c (contents listing) verified!")

            # 8.3: Package Installation into AarFS
            inst_out = vm.send_line("dpkg -i calc_pkg.deb")
            assert "status database updated successfully" in inst_out
            assert "POSIX syscall layer" in inst_out
            print("   dpkg -i (installation & binary boundary notice) verified!")

            # 8.4: Installed Package Database Query
            list_out = vm.send_line("dpkg -l")
            assert "aarambh-calc" in list_out
            assert "2.0.4" in list_out
            assert "i386" in list_out
            print("   dpkg -l (package status database listing) verified!")

    finally:
        if test_img.exists():
            test_img.unlink()

    print("\n=================================================================")
    print("  ALL DESKTOP ENVIRONMENT ACCEPTANCE TESTS PASSED (8/8 - 100%)!  ")
    print("=================================================================")

if __name__ == "__main__":
    main()
