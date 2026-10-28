"""End-to-end AarFS and shell regression test on a disposable PM32 disk copy."""
from qemu_harness import QemuSession


def main():
    with QemuSession() as vm:
        out = vm.send_line_paged("help")
        for text in ("AARAMBH PM32 HELP INDEX", "Getting started", "Files and directories"):
            assert text in out, f"help index missing {text!r}\n{out[-2000:]}"

        out = vm.send_line_paged("guide")
        assert "AARAMBH OS GUIDE" in out, f"guide missing title\n{out[-2000:]}"

        out = vm.send_line_paged("help files")
        for text in ("mkdir <name>", "read <file>", "rm <name>"):
            assert text in out, f"help files missing {text!r}\n{out[-2000:]}"

        out = vm.send_line("help read")
        for text in ("read <file>", "Purpose:", "Example:", "Persistent changes: no", "Limits:"):
            assert text in out, f"help read missing {text!r}\n{out[-2000:]}"

        out = vm.send_line("py help")
        assert "py <file.py>" in out

        assert "hello test" in vm.send_line("say hello test")
        assert "Unknown command" in vm.send_line("unknowncmd")
        assert "Did you mean 'help'" in vm.send_line("hel")
        assert "cd: directory not found." in vm.send_line("cd missing")

        assert "Directory created." in vm.send_line("mkdir work")
        vm.send_line("cd work")
        assert "/work" in vm.send_line("pwd")

        assert "File created." in vm.send_line("touch hello.py")
        assert "File saved." in vm.send_line("scribe hello.py hello aarambh")
        assert "hello aarambh" in vm.send_line("cat hello.py")
        assert "File appended." in vm.send_line("grow hello.py os")
        assert "hello aarambhos" in vm.send_line("read hello.py")
        assert "File copied." in vm.send_line("cp hello.py copy.py")
        assert "Entry renamed." in vm.send_line("shift copy.py renamed.py")

        out = vm.send_line("ls")
        assert "renamed.py" in out and "hello.py" in out
        vm.send_line("cd ..")
        assert "/" in vm.send_line("pwd")
        assert "/" in vm.send_line("where")
        vm.send_line("cd work")
        assert "hello aarambhos" in vm.send_line("cat renamed.py")
        assert "Logical capacity:" in vm.send_line("df")

        vm.reboot()
        vm.send_line("cd work")
        assert "hello aarambhos" in vm.send_line("read hello.py")

        assert "Entry deleted." in vm.send_line("erase renamed.py")
        assert "Entry deleted." in vm.send_line("rm hello.py")
        out = vm.send_line("ls")
        assert "hello.py" not in out and "renamed.py" not in out
        vm.send_line("cd ..")
        assert "Entry deleted." in vm.send_line("rm work")
        assert "File not found." in vm.send_line("cat absent.txt")
        assert "Aarambh OS PM32" in vm.send_line("identity")

    print("PASS: help/guide paging, command pages, directories, file write/read/append/"
          "copy/rename/delete, df, reboot persistence, errors, identity")


if __name__ == "__main__":
    main()
