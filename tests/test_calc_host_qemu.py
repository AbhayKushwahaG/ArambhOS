"""QEMU test for the host-assisted `calc host` path using a stub launcher.

The stub speaks the real protocol and returns a success response, but it does
not open a Windows window. The real launcher is covered by unit tests; this
test proves the kernel only claims success after a valid helper response.
"""
import socket
import sys
import threading
from pathlib import Path

from qemu_harness import QemuSession

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from host_python_bridge import BridgeConfig, serve_client  # noqa: E402


def bridge_args(port):
    return [
        "-chardev", f"socket,id=pybridge,host=127.0.0.1,port={port},server=on,wait=off",
        "-device", "isa-serial,chardev=pybridge,iobase=0x2f8,irq=3",
    ]


def free_port():
    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    return port


def main():
    port = free_port()
    config = BridgeConfig(app_launcher=lambda app: b"stub calculator launched\r\n")
    stop = threading.Event()
    worker = threading.Thread(target=serve_client, args=("127.0.0.1", port, config),
                              kwargs={"stop_event": stop}, daemon=True)
    worker.start()
    try:
        with QemuSession(extra_args=bridge_args(port), settle=2.0) as vm:
            out = vm.send_line("calc host")
            assert "stub calculator launched" in out, out[-1200:]
            assert "Windows Calculator launch confirmed" in out
            assert config.last_app == b"calc"

            out = vm.send_line("calc")
            assert "Windows Calculator launch confirmed" in out
            assert "calc> " not in out

            # Built-in arithmetic still works alongside the host path.
            assert "60" in vm.send_line("calc (12 + 8) * 3")
    finally:
        stop.set()
        worker.join(timeout=3)

    print("PASS: calc host and bare calc confirm only after a valid helper launch response")


if __name__ == "__main__":
    main()
