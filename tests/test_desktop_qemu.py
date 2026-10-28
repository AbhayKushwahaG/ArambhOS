"""Automated QEMU test for Aarambh OS Graphical Desktop and Wallpaper Customization."""
import sys
import os
import time

sys.path.insert(0, os.path.dirname(__file__))
from qemu_harness import QemuSession

def main():
    print("=================================================================")
    print("  AARAMBH OS - GRAPHICAL DESKTOP & WALLPAPER TEST")
    print("=================================================================")

    with QemuSession() as vm:
        print("1. Verifying initial text shell prompt...")
        vm.wait_prompt(0)
        print("   Prompt ready.")

        print("2. Launching graphical desktop via 'desktop' command...")
        mark = vm.mark()
        vm.type_text("desktop")
        vm.press("ret")

        # Give desktop 1 second to switch video mode and render initial frame
        time.sleep(1.0)

        print("3. Changing wallpaper to '2' (GNOME Adwaita) and Applying...")
        # Press '2' to select Adwaita
        vm.type_text("2")
        time.sleep(0.5)

        # Press Enter to Apply and save to /etc/desktop.conf
        vm.press("ret")
        time.sleep(0.5)

        print("4. Exiting desktop back to terminal via 'q'...")
        vm.type_text("q")

        # Wait for terminal return banner
        out = vm.wait_for("Returned from Desktop to Text Shell.", mark, timeout=10)
        assert "Returned from Desktop to Text Shell." in out
        vm.wait_prompt(mark)
        print("   Clean return from Mode 13h to Mode 03h text shell verified!")

        print("5. Verifying persistent configuration in /etc/desktop.conf...")
        vm.send_line("cd /etc")
        out = vm.send_line("cat desktop.conf")
        print("   /etc/desktop.conf contents:\n" + out)
        assert "wallpaper=1" in out, "Failed to find wallpaper=1 in /etc/desktop.conf"
        assert "theme=xp_luna" in out, "Failed to find theme=xp_luna in /etc/desktop.conf"
        print("   Wallpaper persistence verified in AarFS!")

        print("6. Testing reboot persistence...")
        vm.reboot()
        print("   Reboot complete. Shell prompt active.")

        # Check /etc/desktop.conf still exists
        vm.send_line("cd /etc")
        out = vm.send_line("cat desktop.conf")
        assert "wallpaper=1" in out
        print("   Configuration verified intact across reboot!")

        print("7. Launching desktop after reboot and exiting via Esc...")
        mark = vm.mark()
        vm.type_text("desktop")
        vm.press("ret")
        time.sleep(1.0)

        # Press Esc to exit
        vm.press("esc")
        out = vm.wait_for("Returned from Desktop to Text Shell.", mark, timeout=10)
        assert "Returned from Desktop to Text Shell." in out
        vm.wait_prompt(mark)
        print("   Second launch and Esc exit verified!")

        vm.send_line("cd /")
        out = vm.send_line("pwd")
        assert "/" in out
        print("   Shell fully responsive.")

    print("=================================================================")
    print("  ALL GRAPHICAL DESKTOP & WALLPAPER TESTS PASSED 100%!")
    print("=================================================================")

if __name__ == "__main__":
    main()
