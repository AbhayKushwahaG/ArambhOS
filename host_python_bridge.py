"""Loopback-only Python execution bridge for Aarambh's PM32 QEMU serial port.

This executes user-provided scripts with the Windows account running this helper.
It is not a sandbox. Keep it bound to 127.0.0.1 and run it only for a trusted VM.
"""
from __future__ import annotations

import argparse
import os
import re
import socket
import socketserver
import struct
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

REQ_MAGIC = b"APY1"
RESP_MAGIC = b"APR1"
PING_MAGIC = b"APNG"
PONG_MAGIC = b"APOK"
VERSION = 1
RUN_SCRIPT = 1
PY_VERSION = 2
LAUNCH_APP = 3
ALLOWED_APPS = {b"calc": "calc.exe"}
STATUS_OK = 0
STATUS_EXIT_ERROR = 1
STATUS_TIMEOUT = 2
STATUS_OUTPUT_LIMIT = 3
STATUS_REJECTED = 4
REQ_HEADER = struct.Struct("<4sBI BB I")
RESP_HEADER = struct.Struct("<4sBI BiII")
SAFE_NAME = re.compile(rb"[A-Za-z0-9_.-]{1,39}\.py\Z", re.IGNORECASE)


class ProtocolError(Exception):
    pass


def _read_exact(sock, length: int, *, idle_timeout: float = 3600.0, frame_timeout: float = 10.0) -> bytes:
    if length < 0:
        raise ProtocolError("negative length")
    chunks = bytearray()
    deadline = None
    while len(chunks) < length:
        if deadline is None:
            deadline = time.monotonic() + idle_timeout
        try:
            data = sock.recv(min(4096, length - len(chunks)))
        except TimeoutError:
            if time.monotonic() >= deadline:
                raise ProtocolError("request timed out")
            continue
        if not data:
            raise EOFError("peer disconnected")
        if len(chunks) == 0:
            deadline = time.monotonic() + frame_timeout
        chunks.extend(data)
        if time.monotonic() > deadline:
            raise ProtocolError("partial frame timed out")
    return bytes(chunks)


def _collect_output(stream, proc, lock, state, bucket, cap: int) -> None:
    try:
        while True:
            block = stream.read(4096)
            if not block:
                return
            with lock:
                room = max(0, cap - state["total"])
                if room:
                    state[bucket].extend(block[:room])
                    state["total"] += min(len(block), room)
                if len(block) > room:
                    state["overflow"] = True
                    try:
                        proc.kill()
                    except OSError:
                        pass
    except (OSError, ValueError):
        return


def run_source(name: bytes, source: bytes, *, timeout: float = 15.0,
               max_script_size: int = 16384, max_output_size: int = 8192):
    """Return (status, returncode, stdout, stderr) from the real host interpreter."""
    if not SAFE_NAME.fullmatch(name) or b".." == name or b"/" in name or b"\\" in name:
        return STATUS_REJECTED, 2, b"", b"Bridge rejected an unsafe script name.\n"
    if len(source) > max_script_size:
        return STATUS_REJECTED, 2, b"", b"Script exceeds the configured size limit.\n"
    if not (0.1 <= timeout <= 60.0):
        raise ValueError("timeout must be between 0.1 and 60 seconds")
    with tempfile.TemporaryDirectory(prefix="aarambh-python-") as td:
        script_path = Path(td) / name.decode("ascii")
        script_path.write_bytes(source)
        env = os.environ.copy()
        env["PYTHONIOENCODING"] = "utf-8"
        try:
            proc = subprocess.Popen(
                [sys.executable, str(script_path)], cwd=td, env=env,
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                shell=False,
            )
        except OSError as exc:
            return STATUS_REJECTED, 127, b"", (f"Could not start Python: {exc}\n".encode("utf-8", "replace"))[:max_output_size]
        lock = threading.Lock()
        state = {"stdout": bytearray(), "stderr": bytearray(), "total": 0, "overflow": False}
        readers = [
            threading.Thread(target=_collect_output, args=(proc.stdout, proc, lock, state, "stdout", max_output_size), daemon=True),
            threading.Thread(target=_collect_output, args=(proc.stderr, proc, lock, state, "stderr", max_output_size), daemon=True),
        ]
        for thread in readers:
            thread.start()
        status = STATUS_OK
        try:
            returncode = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            status = STATUS_TIMEOUT
            proc.kill()
            returncode = proc.wait()
        for thread in readers:
            thread.join(timeout=2.0)
        with lock:
            if state["overflow"]:
                status = STATUS_OUTPUT_LIMIT
            out = bytes(state["stdout"])
            err = bytes(state["stderr"])
        proc.stdout.close()
        proc.stderr.close()
        if status == STATUS_OK and returncode != 0:
            status = STATUS_EXIT_ERROR
        # PM32's VGA text display is ASCII/CP437; keep framing intact and make
        # unsupported Unicode visible as '?' instead of leaking UTF-8 byte noise.
        out = out.decode("utf-8", "replace").encode("ascii", "replace")
        err = err.decode("utf-8", "replace").encode("ascii", "replace")
        return status, int(returncode), out, err


def python_version():
    return f"Python {sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}\r\n".encode("ascii")


def launch_host_app(app: bytes) -> bytes:
    """Launch one allow-listed Windows application without a shell.

    This is deliberately tiny: only calc.exe is reachable, and success is
    reported only after subprocess.Popen has created the process.
    """
    exe_name = ALLOWED_APPS.get(app)
    if not exe_name:
        raise ValueError("application is not allow-listed")
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    exe = system_root / "System32" / exe_name
    if not exe.is_file():
        raise FileNotFoundError(f"{exe} is missing")
    subprocess.Popen(
        [str(exe)],
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        close_fds=True,
    )
    return b"Windows Calculator launched on the host.\r\n"


class BridgeConfig:
    """Limits and state shared by the listen-mode server and connect-mode client."""

    def __init__(self, *, execution_timeout=15.0, max_script_size=16384,
                 max_output_size=8192, app_launcher=None):
        if not (1 <= max_script_size <= 16384):
            raise ValueError("max_script_size must be between 1 and AarFS's 16384-byte limit")
        if not (1 <= max_output_size <= 8192):
            raise ValueError("max_output_size must be between 1 and 8192 bytes")
        if not (0.1 <= execution_timeout <= 60.0):
            raise ValueError("execution_timeout must be between 0.1 and 60 seconds")
        self.last_event = "idle"
        self.last_request = None
        self.last_result = None
        self.last_app = None
        self.app_launcher = app_launcher or launch_host_app
        self.execution_timeout = float(execution_timeout)
        self.max_script_size = int(max_script_size)
        self.max_output_size = int(max_output_size)


class _BridgeHandler(socketserver.BaseRequestHandler):
    def handle(self):
        serve_socket(self.request, self.server)


def serve_socket(sock: socket.socket, config: BridgeConfig) -> None:
    """Serve protocol frames on one connected socket until it closes."""
    sock.settimeout(1.0)
    while True:
        try:
            magic = _read_exact(sock, 4, idle_timeout=3600.0, frame_timeout=10.0)
        except (EOFError, ProtocolError, OSError):
            return
        if magic == PING_MAGIC:
            config.last_event = "ping"
            try:
                sock.sendall(PONG_MAGIC)
            except OSError:
                return
            continue
        if magic != REQ_MAGIC:
            return
        try:
            rest = _read_exact(sock, REQ_HEADER.size - 4)
            _, version, request_id, kind, name_len, source_len = REQ_HEADER.unpack(magic + rest)
            config.last_event = "request"
            if version != VERSION or name_len > 39:
                raise ProtocolError("unsupported request")
            if source_len > config.max_script_size:
                err = b"Script exceeds the configured size limit.\\n"
                sock.sendall(RESP_HEADER.pack(RESP_MAGIC, VERSION, request_id,
                                              STATUS_REJECTED, 2, 0, len(err)) + err)
                return
            name = _read_exact(sock, name_len) if name_len else b""
            source = _read_exact(sock, source_len) if source_len else b""
            config.last_request = (name, source)
            if kind == PY_VERSION and name_len == 0 and source_len == 0:
                status, code, out, err = STATUS_OK, 0, python_version(), b""
            elif kind == LAUNCH_APP:
                app = source.strip().lower()
                config.last_app = app
                if name_len != 0 or not (1 <= source_len <= 32) or app not in ALLOWED_APPS:
                    status, code, out, err = STATUS_REJECTED, 2, b"", b"Unsupported host application request.\n"
                else:
                    try:
                        out = config.app_launcher(app)
                        status, code, err = STATUS_OK, 0, b""
                    except Exception as exc:  # report a real launch failure, never fake success
                        status, code, out = STATUS_REJECTED, 2, b""
                        err = f"Host application launch failed: {exc}\n".encode("ascii", "replace")
                        err = err[:config.max_output_size]
            elif kind == RUN_SCRIPT:
                status, code, out, err = run_source(
                    name, source, timeout=config.execution_timeout,
                    max_script_size=config.max_script_size,
                    max_output_size=config.max_output_size,
                )
            else:
                status, code, out, err = STATUS_REJECTED, 2, b"", b"Unsupported request type.\n"
            # Defensive final cap; stdout and stderr together never exceed the wire limit.
            combined = out + err
            if len(combined) > config.max_output_size:
                out = combined[:config.max_output_size]
                err = b""
                status = STATUS_OUTPUT_LIMIT
            config.last_result = (status, code, out, err)
            header = RESP_HEADER.pack(RESP_MAGIC, VERSION, request_id, status, code, len(out), len(err))
            sock.sendall(header + out + err)
            config.last_event = f"response:{status}:{code}:{len(out)}:{len(err)}"
        except (EOFError, ProtocolError, OSError, struct.error):
            return


def serve_client(host: str, port: int, config: BridgeConfig, *,
                 stop_event: threading.Event | None = None,
                 reconnect_delay: float = 0.5, verbose: bool = False) -> None:
    """Connect to QEMU's loopback chardev listener and serve until stopped."""
    while True:
        if stop_event and stop_event.is_set():
            return
        try:
            sock = socket.create_connection((host, port), timeout=2.0)
        except OSError:
            config.last_event = "waiting-for-qemu"
            if stop_event:
                if stop_event.wait(reconnect_delay):
                    return
            else:
                time.sleep(reconnect_delay)
            continue
        config.last_event = "connected"
        if verbose:
            print(f"Connected to QEMU bridge listener at {host}:{port}", flush=True)
        try:
            serve_socket(sock, config)
        finally:
            try:
                sock.close()
            except OSError:
                pass
        if verbose:
            print("QEMU bridge connection closed; reconnecting.", flush=True)
        if stop_event and stop_event.wait(reconnect_delay):
            return
        if not stop_event:
            time.sleep(reconnect_delay)


class PythonBridgeServer(BridgeConfig, socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address=("127.0.0.1", 40242), *, execution_timeout=15.0,
                 max_script_size=16384, max_output_size=8192, app_launcher=None):
        host, port = address
        if host != "127.0.0.1":
            raise ValueError("The bridge must bind to 127.0.0.1 only")
        if not (0 <= port <= 65535):
            raise ValueError("port must be between 0 and 65535")
        BridgeConfig.__init__(self, execution_timeout=execution_timeout,
                              max_script_size=max_script_size,
                              max_output_size=max_output_size,
                              app_launcher=app_launcher)
        self.last_event = "listening"
        socketserver.ThreadingTCPServer.__init__(self, (host, port), _BridgeHandler)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Loopback-only host Python bridge for Aarambh PM32/QEMU "
                    "(connects to QEMU's COM2 chardev listener)")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=40242)
    parser.add_argument("--timeout", type=float, default=15.0, help="script execution timeout (0.1..60 seconds)")
    parser.add_argument("--max-script-size", type=int, default=16384)
    parser.add_argument("--max-output-size", type=int, default=8192)
    parser.add_argument("--listen", action="store_true",
                        help="listen instead of connecting (test/manual server mode)")
    args = parser.parse_args(argv)
    if sys.version_info < (3, 8):
        parser.error("Python 3.8 or newer is required")
    if args.host != "127.0.0.1":
        parser.error("the bridge is loopback-only; --host must be 127.0.0.1")
    print(f"Interpreter: {sys.executable} ({sys.version.split()[0]}); timeout={args.timeout:g}s; limits={args.max_script_size} script / {args.max_output_size} output bytes")
    print("Host application allowlist: " + ", ".join(sorted(app.decode("ascii") for app in ALLOWED_APPS)))
    print("Scripts run with this Windows account's permissions; Ctrl+C stops the bridge.", flush=True)
    if args.listen:
        try:
            server = PythonBridgeServer(("127.0.0.1", args.port), execution_timeout=args.timeout,
                                        max_script_size=args.max_script_size,
                                        max_output_size=args.max_output_size)
        except (OSError, ValueError) as exc:
            parser.error(str(exc))
        print(f"Aarambh Python bridge listening on 127.0.0.1:{server.server_address[1]}")
        try:
            server.serve_forever(poll_interval=0.2)
        except KeyboardInterrupt:
            print("\nStopping Aarambh Python bridge.")
        finally:
            server.server_close()
        return
    config = BridgeConfig(execution_timeout=args.timeout,
                          max_script_size=args.max_script_size,
                          max_output_size=args.max_output_size)
    print(f"Connecting to QEMU's COM2 listener at {args.host}:{args.port}; retrying until QEMU is ready.", flush=True)
    print("Launch QEMU with -PythonBridge -PythonBridgePort " + str(args.port) + " (or start it now).", flush=True)
    try:
        serve_client(args.host, args.port, config, verbose=True)
    except KeyboardInterrupt:
        print("\nStopping Aarambh Python bridge.")


if __name__ == "__main__":
    main()