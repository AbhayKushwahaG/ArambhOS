"""QEMU tests for the indexed help, pager navigation, and calculator."""
from command_metadata import load_registry
from qemu_harness import PROMPT, QemuSession


def main():
    entries = load_registry()
    with QemuSession() as vm:
        out = vm.send_line_paged("help")
        for text in ("AARAMBH PM32 HELP INDEX", "Getting started", "Troubleshooting",
                     "Type help <number>"):
            assert text in out, f"help index missing {text!r}\n{out[-2000:]}"

        # The pager's I key must return to the index without rebooting.
        mark = vm.mark()
        vm.type_text("help")
        vm.press("ret")
        vm.wait_for("-- More", mark, 10)
        vm.press("i")
        out = vm.page_to_prompt(mark)
        assert out.count("AARAMBH PM32 HELP INDEX") >= 2, "pager I did not reprint the index"

        for entry in entries:
            out = vm.send_line(f"help {entry['name']}")
            for text in ("Purpose:", "Example:", "Errors:", "Persistent changes:", "Limits:"):
                assert text in out, f"help {entry['name']} missing {text!r}\n{out[-1600:]}"

        assert "pwd" in vm.send_line("help where")
        assert "ls" in vm.send_line("help peek")
        assert "read <file>" in vm.send_line("help cat")
        assert "no such command" in vm.send_line("help nosuchcommand")

        categories = {
            "getting": "Getting started", "shell": "Shell and navigation",
            "files": "Files and directories", "editor": "Text editor",
            "python": "Python", "system": "System information",
            "screen": "Screen and session", "trouble": "Troubleshooting",
        }
        for keyword, title in categories.items():
            out = vm.send_line_paged(f"help {keyword}")
            assert title in out, f"help {keyword} missing category title {title!r}\n{out[-1600:]}"
        out = vm.send_line_paged("help 6")
        assert "Calculator" in out and "calc [expression|host|help]" in out

        assert "20" in vm.send_line("calc 12 + 8")
        assert "60" in vm.send_line("calc (12 + 8) * 3")
        assert "14" in vm.send_line("calc 2 + 3 * 4")
        assert "3" in vm.send_line("calc 7 % 4")
        assert "-3" in vm.send_line("calc -5 + 2")
        assert "14" in vm.send_line("calc 100 / 7")
        assert "division by zero" in vm.send_line("calc 7 / 0")
        assert "syntax error" in vm.send_line("calc 2 +")
        assert "overflow" in vm.send_line("calc 2147483647 + 1")
        assert "overflow" in vm.send_line("calc 999999999999")
        assert "host helper unavailable" in vm.send_line("calc host").lower()

        mark = vm.mark()
        vm.type_text("calc")
        vm.press("ret")
        vm.wait_for("calc> ", mark, 10)
        vm.type_text("9 * 9")
        vm.press("ret")
        vm.wait_for("81", mark, 10)
        vm.type_text("10 / 0")
        vm.press("ret")
        vm.wait_for("division by zero", mark, 10)
        vm.type_text("q")
        vm.press("ret")
        vm.wait_prompt(mark, 15)

    print("PASS: help index/categories/command pages, pager I, every registry command "
          "documented, calc precedence/arithmetic/errors/REPL/host-unavailable")


if __name__ == "__main__":
    main()
