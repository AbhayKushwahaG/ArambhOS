"""QEMU end-to-end test: AarFS scripts execute through the Windows helper."""
import socket
import sys
import threading
import time
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
    config = BridgeConfig(execution_timeout=2.0, max_script_size=16384, max_output_size=8192)
    stop = threading.Event()
    worker = threading.Thread(target=serve_client, args=("127.0.0.1", port, config),
                              kwargs={"stop_event": stop}, daemon=True)
    worker.start()
    try:
        with QemuSession(extra_args=bridge_args(port), settle=2.0) as vm:
            assert "py <file.py>" in vm.send_line("py help")
            vm.send_line("touch hello.py")
            vm.send_line("scribe hello.py import this")
            time.sleep(1.0)  # allow the COM2 socket chardev to settle
            out = vm.send_line("py hello.py")
            assert "The Zen of Python, by Tim Peters" in out, out[-1500:]
            assert "Python exited with code 0." in out
            assert "The Zen of Python, by Tim Peters" in vm.send_line("py hello.py")
            assert "Python 3." in vm.send_line("py --version")

            vm.send_line("touch empty.py")
            assert "Python exited with code 0." in vm.send_line("py empty.py")

            vm.send_line("touch failed.py")
            vm.send_line("scribe failed.py raise ValueError('bad')")
            out = vm.send_line("py failed.py")
            assert "Traceback" in out and "Python exited with code 1." in out

            vm.send_line("touch exit7.py")
            vm.send_line("scribe exit7.py raise SystemExit(7)")
            out = vm.send_line("py exit7.py")
            assert "Python exited with code 7." in out

            vm.send_line("touch slow.py")
            vm.send_line("scribe slow.py import time; time.sleep(6)")
            out = vm.send_line("py slow.py", timeout=20)
            assert "exceeded the helper execution timeout" in out, out[-1500:]

            vm.send_line("touch big.py")
            vm.send_line("scribe big.py print('x'*100000)")
            out = vm.send_line("py big.py", timeout=20)
            assert "output exceeded the configured limit" in out, out[-1500:]

            assert "script file not found" in vm.send_line("py absent.py")

            # AarFS still works after repeated bridge runs.
            assert "hello.py" in vm.send_line("ls")
            assert "import this" in vm.send_line("cat hello.py")

        # Without a COM2 chardev the bridge must fail fast and clearly.
        with QemuSession() as vm:
            out = vm.send_line("py --version")
            assert "helper unavailable" in out.lower(), out[-1000:]
            assert "Aarambh OS PM32" in vm.send_line("identity")

        # With QEMU's COM2 listener configured but no helper connected, QEMU must
        # stay responsive and the kernel must still report a clear error.
        with QemuSession(extra_args=bridge_args(free_port()), settle=2.0) as vm:
            out = vm.send_line("py --version", timeout=15)
            assert "helper unavailable" in out.lower(), out[-1000:]
            assert "Aarambh OS PM32" in vm.send_line("identity")
    finally:
        stop.set()
        worker.join(timeout=3)

    print("PASS: real Python output, version, repeated runs, exceptions/exit codes, "
          "timeout, output limit, missing script, helper unavailable with and without "
          "the COM2 listener, AarFS intact")


if __name__ == "__main__":
    main()
