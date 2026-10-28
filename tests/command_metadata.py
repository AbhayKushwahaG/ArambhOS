"""Parse the kernel command registry so tests and docs stay in sync.

The registry in kernel/commands32.asm is the single source of truth for shell
dispatch and help pages. This module reads it without executing the kernel.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMMANDS_ASM = ROOT / "kernel" / "commands32.asm"

_CMD_RE = re.compile(r"^\s*CMD\s+(.+)$")
_DB_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s+db\s+(.+)$")
_TOKEN_RE = re.compile(r"'([^']*)'|\"([^\"]*)\"|0x([0-9A-Fa-f]+)|(\d+)")


def _parse_db(expr: str) -> str:
    out = []
    for match in _TOKEN_RE.finditer(expr):
        if match.group(1) is not None:
            out.append(match.group(1))
        elif match.group(2) is not None:
            out.append(match.group(2))
        elif match.group(3) is not None:
            value = int(match.group(3), 16)
            if value == 0:
                break
            out.append(chr(value))
        elif match.group(4) is not None:
            value = int(match.group(4))
            if value == 0:
                break
            out.append(chr(value))
    return "".join(out)


def load_strings(path: Path = COMMANDS_ASM) -> dict[str, str]:
    strings = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = _DB_RE.match(line)
        if match:
            strings.setdefault(match.group(1), _parse_db(match.group(2)))
    return strings


def load_registry(path: Path = COMMANDS_ASM) -> list[dict]:
    strings = load_strings(path)
    entries = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = _CMD_RE.match(line)
        if not match or line.lstrip().startswith("%macro"):
            continue
        args = [arg.strip() for arg in match.group(1).split(",")]
        if len(args) != 10:
            raise AssertionError(f"CMD needs 10 arguments: {line.strip()}")
        name_label, alias_label, handler, cat, modifies, syntax, desc, example, errors, limits = args
        entry = {
            "name_label": name_label,
            "alias_label": alias_label,
            "handler": handler,
            "category": int(cat),
            "modifies": int(modifies),
            "syntax_label": syntax,
            "description_label": desc,
            "example_label": example,
            "errors_label": errors,
            "limits_label": limits,
        }
        for field in ("syntax", "description", "example", "errors", "limits"):
            label = entry[f"{field}_label"]
            if label not in strings:
                raise AssertionError(f"{field} label {label} has no string")
            entry[field] = strings[label]
        entry["name"] = strings.get(name_label, "")
        entry["alias"] = strings.get(alias_label, "") if alias_label != "0" else ""
        entries.append(entry)
    return entries


def load_handlers() -> set[str]:
    handlers = set()
    for path in (ROOT / "kernel").glob("*.asm"):
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = re.match(r"^([A-Za-z_][A-Za-z0-9_]*):", line)
            if match:
                handlers.add(match.group(1))
    return handlers


if __name__ == "__main__":
    for item in load_registry():
        print(f"{item['name']:8} cat={item['category']} modifies={item['modifies']} "
              f"alias={item['alias'] or '-':7} handler={item['handler']}")
