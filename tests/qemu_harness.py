"""Shared QEMU/QMP harness for Aarambh PM32 regression tests.

Every test copies the disposable AarFS image to a temporary directory, boots
QEMU with only that copy attached, and drives the guest through the QMP
keyboard. No physical disk is touched.
"""
from __future__ import annotations

import json
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / "aarambh-v04-pm32-fs.img"
QEMU = Path(r"C:\Program Files\qemu\qemu-system-i386.exe")
PROMPT = "AR32:/ > "
MORE = "-- More:"

SPECIAL = {
    "enter": "ret", "esc": "esc", "space": "spc", "tab": "tab",
    "backspace": "backspace", "delete": "delete",
    "up": "up", "down": "down", "left": "left", "right": "right",
    "home": "home", "end": "end", "pgup": "pgup", "pgdn": "pgdn",
    "f1": "f1", "f2": "f2", "f3": "f3", "f4": "f4", "f5": "f5",
    "f6": "f6", "f7": "f7", "f8": "f8", "f9": "f9", "f10": "f10",
}

PLAIN = {
    " ": "spc", ".": "dot", "/": "slash", "-": "minus", "=": "equal",
    "'": "apostrophe", ",": "comma", ";": "semicolon", "\\": "backslash",
    "[": "bracket_left", "]": "bracket_right", "`": "grave_accent",
}

SHIFTED = {
    "!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6",
    "&": "7", "*": "8", "(": "9", ")": "0", "_": "minus", "+": "equal",
    "{": "bracket_left", "}": "bracket_right", "|": "backslash",
    ":": "semicolon", '"': "apostrophe", "<": "comma", ">": "dot",
    "?": "slash", "~": "grave_accent",
}


def char_key(ch: str):
    if ch.isalpha():
        return ch.lower(), ch.isupper()
    if ch.isdigit():
        return ch, False
    if ch in SHIFTED:
        return SHIFTED[ch], True
    if ch in PLAIN:
        return PLAIN[ch], False
    raise ValueError(f"No QEMU key mapping for {ch!r}")


class Qmp:
    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.buf = b""

    def recv(self):
        while b"\n" not in self.buf:
            block = self.sock.recv(4096)
            if not block:
                raise RuntimeError("QMP connection closed")
            self.buf += block
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line)

    def command(self, name, arguments=None):
        obj = {"execute": name}
        if arguments is not None:
            obj["arguments"] = arguments
        self.sock.sendall(json.dumps(obj).encode() + b"\r\n")
        while True:
            msg = self.recv()
            if "error" in msg:
                raise RuntimeError(msg["error"])
            if "return" in msg:
                return msg["return"]


class QemuSession:
    def __init__(self, *, extra_args=(), settle=2.5, image=None, network=None):
        if not IMAGE.exists():
            raise SystemExit("Build the PM32 image first: .\\build-protected.ps1 -NoLaunch")
        if not QEMU.exists():
            raise SystemExit(f"Missing QEMU executable: {QEMU}")
        self._td = tempfile.TemporaryDirectory(prefix="aasession-")
        base = Path(self._td.name)
        self.disk = base / "test.img"
        self.serial = base / "com1.log"
        shutil.copyfile(image or IMAGE, self.disk)
        probe = socket.socket()
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()
        args = [
            str(QEMU), "-drive", f"file={self.disk},format=raw,if=ide,index=0",
            "-boot", "c", "-display", "none", "-monitor", "none",
            "-serial", f"file:{self.serial}",
            "-qmp", f"tcp:127.0.0.1:{port},server=on,wait=off",
        ]
        if network and network != "none":
            args.extend(["-netdev", "user,id=net0", "-device", f"{network},netdev=net0"])
        elif network == "none":
            args.extend(["-nic", "none"])
        args.extend(list(extra_args))
        self.proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.qmp = None
        try:
            end = time.time() + 12
            while time.time() < end:
                try:
                    sock = socket.create_connection(("127.0.0.1", port), timeout=1)
                    break
                except OSError:
                    if self.proc.poll() is not None:
                        raise RuntimeError(f"QEMU exited with code {self.proc.returncode}")
                    time.sleep(0.1)
            else:
                raise RuntimeError("QMP did not become ready")
            sock.settimeout(30)
            self.qmp = Qmp(sock)
            self.qmp.recv()
            self.qmp.command("qmp_capabilities")
            time.sleep(settle)
        except Exception:
            self.close()
            raise

    def log(self) -> str:
        return self.serial.read_text(errors="replace") if self.serial.exists() else ""

    def mark(self) -> int:
        return len(self.log())

    def wait_for(self, text: str, mark: int = 0, timeout: float = 20.0) -> str:
        end = time.time() + timeout
        while time.time() < end:
            current = self.log()
            if text in current[mark:]:
                return current[mark:]
            time.sleep(0.05)
        raise AssertionError(f"Timed out waiting for {text!r}; tail:\n{self.log()[mark:][-2000:]}")

    def press(self, code: str, *, shift=False, ctrl=False, alt=False):
        mods = []
        if ctrl:
            mods.append("ctrl")
        if shift:
            mods.append("shift")
        if alt:
            mods.append("alt")
        for mod in mods:
            self.qmp.command("input-send-event", {"events": [
                {"type": "key", "data": {"down": True, "key": {"type": "qcode", "data": mod}}}]})
        self.qmp.command("input-send-event", {"events": [
            {"type": "key", "data": {"down": True, "key": {"type": "qcode", "data": code}}}]})
        self.qmp.command("input-send-event", {"events": [
            {"type": "key", "data": {"down": False, "key": {"type": "qcode", "data": code}}}]})
        for mod in reversed(mods):
            self.qmp.command("input-send-event", {"events": [
                {"type": "key", "data": {"down": False, "key": {"type": "qcode", "data": mod}}}]})
        time.sleep(0.03)

    def send_keys(self, keys):
        for item in keys:
            if isinstance(item, tuple):
                code = item[0]
                shift = item[1] if len(item) > 1 else False
                ctrl = item[2] if len(item) > 2 else False
                self.press(SPECIAL.get(code, code), shift=shift, ctrl=ctrl)
            elif item in SPECIAL:
                self.press(SPECIAL[item])
            elif len(item) == 1:
                code, shift = char_key(item)
                self.press(code, shift=shift)
            else:
                raise ValueError(f"Unsupported key token: {item!r}")

    def type_text(self, text: str):
        for ch in text:
            code, shift = char_key(ch)
            self.press(code, shift=shift)

    def wait_prompt(self, mark: int = 0, timeout: float = 30.0) -> str:
        end = time.time() + timeout
        while time.time() < end:
            current = self.log()
            tail = current[mark:]
            if ' > ' in tail and ('@AR32:' in tail or 'AR32:' in tail):
                return tail
            time.sleep(0.05)
        raise AssertionError(f'Timed out waiting for prompt; tail:\n{self.log()[mark:][-2000:]}')

    def send_line(self, line: str, timeout: float = 30.0) -> str:
        mark = self.mark()
        self.type_text(line)
        self.press("ret")
        return self.wait_prompt(mark, timeout)

    def page_to_prompt(self, mark: int, timeout: float = 90.0) -> str:
        mores = self.log()[:mark].count(MORE)
        end = time.time() + timeout
        while time.time() < end:
            current = self.log()
            if current.count(MORE) > mores:
                mores = current.count(MORE)
                self.press("spc")
            elif (' > ' in current[mark:] and ('@AR32:' in current[mark:] or 'AR32:' in current[mark:])) or current[mark:].rstrip().endswith(PROMPT.rstrip()):
                time.sleep(0.05)
                return current[mark:]
            time.sleep(0.05)
        raise AssertionError(f"Pager did not return to the shell; tail:\n{self.log()[mark:][-2500:]}")

    def send_line_paged(self, line: str, timeout: float = 90.0) -> str:
        mark = self.mark()
        self.type_text(line)
        self.press("ret")
        return self.page_to_prompt(mark, timeout)

    def send_hmp_sendkey(self, keys: str):
        """Use QEMU's own sendkey for shortcut combinations if needed."""
        self.qmp.command("human-monitor-command", {"command-line": f"sendkey {keys}"})

    def reboot(self, timeout: float = 30.0) -> str:
        mark = self.mark()
        self.type_text("reboot")
        self.press("ret")
        self.wait_for("Aarambh PM32: ready", mark, timeout)
        return self.wait_prompt(mark, timeout)

    def close(self):
        if getattr(self, "qmp", None):
            try:
                self.qmp.command("quit")
            except Exception:
                pass
        if getattr(self, "proc", None):
            self.proc.terminate()
            try:
                self.proc.wait(timeout=4)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        if getattr(self, "_td", None):
            self._td.cleanup()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
