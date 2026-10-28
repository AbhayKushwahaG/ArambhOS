"""QEMU tests for the Aarambh editor: editing, saving, scrolling, persistence."""
from qemu_harness import QemuSession


def cat(vm, name):
    return vm.send_line(f"cat {name}")


def main():
    with QemuSession() as vm:
        vm.send_line("mkdir work")
        vm.send_line("cd work")
        vm.send_line("mkdir sub")

        # Create and save a new file.
        mark = vm.mark()
        vm.type_text("edit hello.txt")
        vm.press("ret")
        vm.wait_for("EDIT hello.txt", mark)
        vm.type_text("first line")
        vm.press("ret")
        vm.type_text("second line")
        out = vm.wait_for("22B", mark)
        assert "MODIFIED" in out
        mark = vm.mark()
        vm.send_keys([("s", False, True)])
        out = vm.wait_for("SAVED", mark)
        assert "Saved to AarFS." in out and "SAVED" in out, out[-1200:]
        mark = vm.mark()
        vm.press("esc")
        vm.wait_prompt(mark)
        assert "edit: closed hello.txt (saved)." in vm.log()[mark:]
        out = cat(vm, "hello.txt")
        assert "first line" in out and "second line" in out

        # Reopen, use End, move down, Home, insert, save, and verify neighbours.
        mark = vm.mark()
        vm.type_text("edit hello.txt")
        vm.press("ret")
        vm.wait_for("EDIT hello.txt", mark)
        vm.press("end")
        vm.type_text("!")
        vm.send_keys([("s", False, True)])
        vm.wait_for("Saved to AarFS.", mark)
        vm.press("esc")
        vm.wait_prompt(mark)
        out = cat(vm, "hello.txt")
        assert "first line!" in out and "second line" in out

        mark = vm.mark()
        vm.type_text("edit hello.txt")
        vm.press("ret")
        vm.wait_for("EDIT hello.txt", mark)
        vm.press("down")
        vm.press("home")
        vm.type_text("X")
        vm.send_keys([("s", False, True)])
        vm.wait_for("Saved to AarFS.", mark)
        vm.press("esc")
        vm.wait_prompt(mark)
        out = cat(vm, "hello.txt")
        assert "first line!" in out and "Xsecond line" in out

        # Delete and Backspace must not disturb the rest of the file.
        mark = vm.mark()
        vm.type_text("edit hello.txt")
        vm.press("ret")
        vm.wait_for("EDIT hello.txt", mark)
        vm.press("home")
        vm.press("delete")
        vm.type_text("F")
        vm.press("end")
        vm.press("backspace")
        vm.send_keys([("s", False, True)])
        vm.wait_for("Saved to AarFS.", mark)
        vm.press("esc")
        vm.wait_prompt(mark)
        out = cat(vm, "hello.txt")
        assert "First line" in out and "Xsecond line" in out
        assert "irst line!" not in out

        # Dirty warning, then discard with a second Esc.
        mark = vm.mark()
        vm.type_text("edit hello.txt")
        vm.press("ret")
        vm.wait_for("EDIT hello.txt", mark)
        vm.type_text("zzz")
        vm.press("esc")
        out = vm.wait_for("Unsaved changes", mark)
        assert "Ctrl-S save" in out
        vm.press("esc")
        vm.wait_prompt(mark)
        out = cat(vm, "hello.txt")
        assert "zzz" not in out and "First line" in out

        # F1 help overlay, then return and exit.
        mark = vm.mark()
        vm.type_text("edit hello.txt")
        vm.press("ret")
        vm.wait_for("EDIT hello.txt", mark)
        vm.press("f1")
        out = vm.wait_for("AARAMBH EDITOR - KEYBOARD HELP", mark)
        assert "Ctrl-S" in out and "F1" in out
        vm.press("esc")
        vm.wait_for("[EDIT]", mark)
        vm.press("esc")
        vm.wait_prompt(mark)

        # Empty files: open, save as new, and print without error.
        vm.send_line("touch empty.txt")
        mark = vm.mark()
        vm.type_text("edit empty.txt")
        vm.press("ret")
        vm.wait_for("0B", mark)
        vm.press("esc")
        vm.wait_prompt(mark)
        out = cat(vm, "empty.txt")
        assert "File not found" not in out
        mark = vm.mark()
        vm.type_text("edit newempty.txt")
        vm.press("ret")
        vm.wait_for("EDIT newempty.txt", mark)
        vm.send_keys([("s", False, True)])
        vm.wait_for("Saved to AarFS.", mark)
        vm.press("esc")
        vm.wait_prompt(mark)
        assert "newempty.txt" in vm.send_line("ls")

        # Long line: horizontal scrolling and the 16 KiB boundary are real.
        vm.send_line("scribe long.txt " + "a" * 100)
        mark = vm.mark()
        vm.type_text("edit long.txt")
        vm.press("ret")
        vm.wait_for("EDIT long.txt", mark)
        vm.press("end")
        vm.wait_for("Col 101", mark)
        vm.type_text(".")
        vm.send_keys([("s", False, True)])
        out = vm.wait_for("SAVED", mark)
        assert "101B" in out and "Saved to AarFS." in out
        vm.press("esc")
        vm.wait_prompt(mark)
        out = cat(vm, "long.txt")
        assert out.count("a") >= 100 and "." in out

        # Vertical scrolling: 24 newlines create 25 lines.
        mark = vm.mark()
        vm.type_text("edit scroll.txt")
        vm.press("ret")
        vm.wait_for("EDIT scroll.txt", mark)
        for _ in range(24):
            vm.press("ret")
        out = vm.wait_for("Ln 25", mark, 30)
        assert "Col 1" in out
        vm.send_keys([("s", False, True)])
        out = vm.wait_for("SAVED", mark, 30)
        assert "24B" in out and "Saved to AarFS." in out
        vm.press("esc")
        vm.wait_prompt(mark)
        mark = vm.mark()
        vm.type_text("edit scroll.txt")
        vm.press("ret")
        vm.wait_for("24B", mark)
        vm.press("pgdn")
        vm.press("esc")
        vm.wait_prompt(mark)

        # Invalid names, directories, and missing arguments are reported.
        assert "Usage: edit <file>" in vm.send_line("edit")
        assert "invalid filename" in vm.send_line("edit a/b")
        assert "directory" in vm.send_line("edit sub")
        assert "invalid filename" in vm.send_line("edit " + "a" * 40 + ".txt")

        # Editor contents survive a reboot.
        vm.reboot()
        vm.send_line("cd work")
        out = cat(vm, "hello.txt")
        assert "First line" in out and "Xsecond line" in out
        out = cat(vm, "long.txt")
        assert out.count("a") >= 100 and "." in out

    print("PASS: editor create/edit/save/reopen, arrows/Home/End/Delete/Backspace, "
          "dirty-discard, F1, empty and long files, vertical/horizontal scrolling, "
          "invalid names, and reboot persistence")


if __name__ == "__main__":
    main()
