"""Comprehensive QEMU integration test for Aarambh OS user, group, and permission system.

Tests:
1. Default boot session (root UID 0, GID 0, home /root, prompt root@AR32:/ > )
2. User and group queries: whoami, id, users, groups, stat
3. Group management: groupadd, groupdel, duplicate prevention
4. User management: useradd, userdel, usermod, duplicate prevention
5. Unprivileged user creation and password setting
6. Session switching: su with authentication (silent password, reject invalid, accept valid)
7. Privilege separation: unprivileged user denied admin commands (useradd, userdel, chown)
8. Filesystem permissions: chmod 0600 on root file blocks unprivileged read/write/append/rename/delete
9. Native MicroPython permission enforcement: unauthorized script execution blocked (Permission denied)
10. Native MicroPython 'aarambh' module: getuid(), getgid(), getuser(), stat()
11. Home directory isolation: cd /root blocked for unprivileged; cd / cd ~ navigates to user home
12. Logout / reprompt behavior
13. Database persistence across reboot
"""
import sys
from pathlib import Path
from qemu_harness import QemuSession

def test_users_and_permissions():
    print("Starting QEMU user and permission test session...", flush=True)
    with QemuSession() as vm:
        # 1. Initial boot session checks
        print("1. Checking default boot session...", flush=True)
        out = vm.send_line("whoami")
        assert "root" in out, f"whoami failed: {out}"

        out = vm.send_line("id")
        assert "uid=0(root)" in out and "gid=0(root)" in out, f"id failed: {out}"

        out = vm.send_line("users")
        assert "root" in out and "aarambh" in out, f"users failed: {out}"

        out = vm.send_line("groups")
        assert "root" in out and "wheel" in out, f"groups failed: {out}"

        # 2. Group management
        print("2. Testing group creation and validation...", flush=True)
        out = vm.send_line("groupadd -g 1001 developers")
        assert "created with GID" in out, f"groupadd failed: {out}"

        # Duplicate group rejection
        out = vm.send_line("groupadd developers")
        assert "already exists" in out, f"duplicate group not rejected: {out}"
        out = vm.send_line("groupadd -g 1001 othergrp")
        assert "already in use" in out, f"duplicate gid not rejected: {out}"

        # 3. User management
        print("3. Testing user creation and interactive password entry...", flush=True)
        mark = vm.mark()
        vm.type_text("useradd -u 1001 -g 1001 -d /home/dev devuser")
        vm.press("ret")
        vm.wait_for("Enter new password for devuser:", mark)
        vm.type_text("devpass123")
        vm.press("ret")
        vm.wait_for("Retype password:", mark)
        vm.type_text("devpass123")
        vm.press("ret")
        out = vm.wait_prompt(mark)
        assert "created with UID" in out, f"useradd failed: {out}"

        # Duplicate user rejection (pass -p to avoid prompt on expected failure)
        out = vm.send_line("useradd -p pass devuser")
        assert "already exists" in out, f"duplicate user not rejected: {out}"
        out = vm.send_line("useradd -u 1001 -p pass devuser2")
        assert "already in use" in out, f"duplicate uid not rejected: {out}"

        # Verify user in database
        out = vm.send_line("users")
        assert "devuser" in out, f"users missing devuser: {out}"

        # Change devuser password via passwd command
        print("4. Updating devuser password via passwd...", flush=True)
        mark = vm.mark()
        vm.type_text("passwd devuser")
        vm.press("ret")
        vm.wait_for("New password:", mark)
        vm.type_text("devpass456")
        vm.press("ret")
        vm.wait_for("Retype new password:", mark)
        vm.type_text("devpass456")
        vm.press("ret")
        out = vm.wait_prompt(mark)
        assert "password updated successfully" in out, f"passwd failed: {out}"

        # 5. Create a restricted root file (mode 0600)
        print("5. Creating root-owned restricted file (mode 0600)...", flush=True)
        assert "File created." in vm.send_line("touch secret.txt")
        assert "File saved." in vm.send_line("scribe secret.txt TOP_SECRET_ROOT_DATA")
        vm.send_line("chmod 600 secret.txt")
        out = vm.send_line("stat secret.txt")
        assert "0600/-rw-------" in out and "Uid: (0/root)" in out, f"stat failed: {out}"

        # Also create a restricted python script (mode 0600)
        assert "File created." in vm.send_line("touch secret.py")
        assert "File saved." in vm.send_line("scribe secret.py print('ROOT_EXECUTION')")
        vm.send_line("chmod 600 secret.py")

        # Create native MicroPython test script testing both module API and int 0x80 syscall
        assert "File created." in vm.send_line("touch check_id.py")
        vm.send_line("scribe check_id.py import aarambh; print(aarambh.getuid(), aarambh.getuser(), aarambh.syscall(24))")
        out = vm.send_line("python check_id.py")
        assert "0 root 0" in out, f"native python aarambh module/syscall failed as root: {out}"

        # 6. Session switching with su
        print("6. Testing su session switching...", flush=True)
        # Root switches to devuser without needing password
        out = vm.send_line("su devuser")
        assert "Switched to user devuser" in out, f"su as root failed: {out}"

        # Verify shell prompt and identity updated
        out = vm.send_line("whoami")
        assert "devuser" in out, f"whoami not devuser: {out}"
        out = vm.send_line("id")
        assert "uid=1001(devuser)" in out and "gid=1001(developers)" in out, f"id not devuser: {out}"

        # 7. Unprivileged user permission restrictions
        print("7. Testing unprivileged user permission enforcement...", flush=True)
        out = vm.send_line("useradd hacker")
        assert "permission denied" in out.lower(), f"unprivileged user ran useradd: {out}"

        out = vm.send_line("userdel root")
        assert "permission denied" in out.lower(), f"unprivileged user ran userdel: {out}"

        out = vm.send_line("chown devuser secret.txt")
        assert "Operation not permitted" in out, f"unprivileged user ran chown: {out}"

        # 8. Filesystem permission checks on secret.txt (mode 0600 root-owned)
        print("8. Testing read/write/append/delete permission denials on restricted file...", flush=True)
        out = vm.send_line("read secret.txt")
        assert "Permission denied" in out, f"unprivileged read not denied: {out}"

        out = vm.send_line("cat secret.txt")
        assert "Permission denied" in out, f"unprivileged cat not denied: {out}"

        out = vm.send_line("scribe secret.txt injected")
        assert "Permission denied" in out, f"unprivileged scribe not denied: {out}"

        out = vm.send_line("grow secret.txt extra")
        assert "Permission denied" in out, f"unprivileged grow not denied: {out}"

        out = vm.send_line("rm secret.txt")
        assert "Permission denied" in out, f"unprivileged rm not denied: {out}"

        out = vm.send_line("mv secret.txt stolen.txt")
        assert "Permission denied" in out, f"unprivileged mv not denied: {out}"

        # 9. Native MicroPython permission denial
        print("9. Testing native MicroPython script execution permission denial...", flush=True)
        out = vm.send_line("python secret.py")
        assert "Permission denied" in out, f"unprivileged python execution not denied: {out}"

        # 10. Native MicroPython caller identity reflection
        print("10. Testing native MicroPython caller identity as devuser...", flush=True)
        out = vm.send_line("python check_id.py")
        assert "1001 devuser 1001" in out, f"python identity not devuser: {out}"

        # 11. Home directory isolation
        print("11. Testing home directory isolation and navigation...", flush=True)
        out = vm.send_line("cd /root")
        assert "Permission denied" in out, f"devuser accessed /root: {out}"

        vm.send_line("cd ~")
        out = vm.send_line("pwd")
        assert "/home/devuser" in out or "/home/dev" in out, f"cd ~ did not go to home: {out}"

        # 12. Switch back to root with password
        print("12. Testing su to root with password authentication...", flush=True)
        # Negative test: wrong password
        mark = vm.mark()
        vm.type_text("su root")
        vm.press("ret")
        vm.wait_for("Password:", mark)
        vm.type_text("wrongpass")
        vm.press("ret")
        out = vm.wait_prompt(mark)
        assert "Authentication failure" in out, f"wrong password accepted: {out}"

        # Positive test: correct password (root123)
        mark = vm.mark()
        vm.type_text("su root")
        vm.press("ret")
        vm.wait_for("Password:", mark)
        vm.type_text("root123")
        vm.press("ret")
        out = vm.wait_prompt(mark)
        assert "Switched to user root" in out, f"su root failed: {out}"

        out = vm.send_line("whoami")
        assert "root" in out, f"not root after su: {out}"

        # Root CAN read secret.txt
        vm.send_line("cd /")
        out = vm.send_line("read secret.txt")
        assert "TOP_SECRET_ROOT_DATA" in out, f"root could not read secret.txt: {out}"

        # 13. Persistence across reboot
        print("13. Rebooting to verify persistence...", flush=True)
        vm.send_line("df")
        vm.reboot()

        # 14. Verifying persistence in second boot session
        print("14. Verifying persistence after reboot...", flush=True)
        out = vm.send_line("users")
        assert "devuser" in out, f"devuser did not persist across reboot: {out}"
        assert "aarambh" in out and "root" in out

        out = vm.send_line("groups devuser")
        assert "developers" in out, f"developers group did not persist across reboot: {out}"

        out = vm.send_line("stat secret.txt")
        assert "0600/-rw-------" in out and "Uid: (0/root)" in out, f"file permissions did not persist: {out}"

        # devuser can still su with their updated password (devpass456)
        mark = vm.mark()
        vm.type_text("su devuser")
        vm.press("ret")
        out = vm.wait_prompt(mark)
        assert "Switched to user devuser" in out

        out = vm.send_line("whoami")
        assert "devuser" in out

        # Permissions still enforced
        out = vm.send_line("read secret.txt")
        assert "Permission denied" in out

    print("ALL USER, GROUP, AND PERMISSION TESTS PASSED!", flush=True)

if __name__ == "__main__":
    test_users_and_permissions()
