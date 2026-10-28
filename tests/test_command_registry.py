"""Static checks that the command registry and help metadata stay complete."""
import re
import unittest
from pathlib import Path

from command_metadata import ROOT, load_handlers, load_registry

KERNEL = ROOT / "kernel"


class CommandRegistryTests(unittest.TestCase):
    def setUp(self):
        self.entries = load_registry()
        self.handlers = load_handlers()

    def test_every_registered_command_has_help_metadata_and_handler(self):
        self.assertGreaterEqual(len(self.entries), 20)
        names = set()
        aliases = set()
        categories = set()
        for entry in self.entries:
            self.assertRegex(entry["name"], r"^[a-z][a-z0-9-]*$")
            self.assertTrue(entry["syntax"], entry["name"])
            self.assertIn("Purpose:", entry["description"], entry["name"])
            self.assertIn("Result:", entry["example"], entry["name"])
            self.assertTrue(entry["errors"], entry["name"])
            self.assertTrue(entry["limits"], entry["name"])
            self.assertIn(entry["handler"], self.handlers,
                          f"missing handler {entry['handler']} for {entry['name']}")
            self.assertIn(entry["category"], range(1, 10))
            categories.add(entry["category"])
            self.assertNotIn(entry["name"], names)
            names.add(entry["name"])
            if entry["alias"]:
                self.assertNotIn(entry["alias"], aliases)
                self.assertNotIn(entry["alias"], names)
                aliases.add(entry["alias"])
        self.assertEqual(categories, set(range(1, 10)), "help categories 1..9 must all be used")

    def test_examples_start_with_the_command_name_or_alias(self):
        for entry in self.entries:
            first = entry["example"].splitlines()[0].split()[0]
            self.assertIn(first, {entry["name"], entry["alias"]}, entry["name"])

    def test_persistent_change_flags_match_the_real_handlers(self):
        modifying = {"mkdir", "touch", "scribe", "grow", "shift", "cp", "rm", "edit"}
        for entry in self.entries:
            expected = 1 if entry["name"] in modifying else 0
            self.assertEqual(entry["modifies"], expected, entry["name"])

    def test_shell_uses_the_registry_instead_of_a_parallel_dispatch(self):
        kernel = (KERNEL / "kernel32.asm").read_text(encoding="utf-8", errors="replace")
        self.assertIn("call command_dispatch", kernel)
        self.assertNotRegex(kernel, r"\bcmd_[a-z]+\b")
        registry = (KERNEL / "commands32.asm").read_text(encoding="utf-8", errors="replace")
        self.assertIn("[ebx+4]", registry, "aliases must participate in dispatch")
        self.assertIn("help_find_entry", registry)
        self.assertIn("command_unknown", registry)
        help_source = (KERNEL / "help32.asm").read_text(encoding="utf-8", errors="replace")
        self.assertIn("help_find_entry", help_source)
        self.assertIn("category_titles", help_source)
        self.assertIn("pager_putc", help_source)

    def test_no_help_text_lives_only_in_the_shell(self):
        kernel = (KERNEL / "kernel32.asm").read_text(encoding="utf-8", errors="replace")
        self.assertNotIn("AARAMBH PM32 COMMAND INDEX", kernel)
        self.assertNotIn("Usage: scribe", kernel)


if __name__ == "__main__":
    unittest.main(verbosity=2)
