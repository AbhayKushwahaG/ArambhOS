"""Unit and local-protocol tests for the Aarambh host Python helper."""
import socket
import struct
import threading
import time
import unittest
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import host_python_bridge as bridge

REQ = struct.Struct("<4sBI BB I")
RESP = struct.Struct("<4sBI BiII")


class PythonBridgeTests(unittest.TestCase):
    def start_server(self, **kwargs):
        server = bridge.PythonBridgeServer(("127.0.0.1", 0), **kwargs)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.shutdown)
        self.addCleanup(server.server_close)
        return server, thread

    def request(self, sock, reqid, kind, name=b"", source=b""):
        sock.sendall(REQ.pack(bridge.REQ_MAGIC, bridge.VERSION, reqid, kind, len(name), len(source)) + name + source)
        header = b""
        while len(header) < RESP.size:
            part = sock.recv(RESP.size - len(header))
            if not part:
                raise AssertionError("helper closed during response header")
            header += part
        magic, version, got_id, status, code, out_len, err_len = RESP.unpack(header)
        payload = bytearray()
        while len(payload) < out_len + err_len:
            part = sock.recv(out_len + err_len - len(payload))
            if not part:
                raise AssertionError("helper closed during response body")
            payload.extend(part)
        self.assertEqual((magic, version, got_id), (bridge.RESP_MAGIC, bridge.VERSION, reqid))
        return status, code, bytes(payload[:out_len]), bytes(payload[out_len:])

    def test_real_python_features_over_protocol_and_reuse_connection(self):
        server, _ = self.start_server()
        with socket.create_connection(server.server_address, timeout=3) as sock:
            sock.settimeout(4)
            sock.sendall(bridge.PING_MAGIC)
            self.assertEqual(sock.recv(4), bridge.PONG_MAGIC)
            result = self.request(sock, 1, bridge.RUN_SCRIPT, b"multi.py",
                                  b"print('first')\nprint('second')\nx=6\nprint(x*7)\n")
            self.assertEqual(result, (bridge.STATUS_OK, 0, b"first\r\nsecond\r\n42\r\n", b""))
            self.assertEqual(self.request(sock, 2, bridge.RUN_SCRIPT, b"empty.py", b"")[:2], (0, 0))
            version = self.request(sock, 3, bridge.PY_VERSION)
            self.assertEqual(version[0], bridge.STATUS_OK)
            self.assertTrue(version[2].startswith(b"Python 3."))
            again = self.request(sock, 4, bridge.RUN_SCRIPT, b"again.py", b"print(9)\n")
            self.assertEqual(again[2], b"9\r\n")

    def test_exceptions_exit_codes_and_unicode(self):
        status, code, out, err = bridge.run_source(b"error.py", b"raise ValueError('bad')\n")
        self.assertEqual(status, bridge.STATUS_EXIT_ERROR)
        self.assertNotEqual(code, 0)
        self.assertIn(b"Traceback", err)
        status, code, _, err = bridge.run_source(b"exit.py", b"raise SystemExit(7)\n")
        self.assertEqual((status, code), (bridge.STATUS_EXIT_ERROR, 7))
        self.assertEqual(bridge.run_source(b"unicode.py", "print('café')\n".encode())[2], b"caf?\r\n")

    def test_oversized_source_and_output_are_bounded(self):
        status, code, _, err = bridge.run_source(b"large.py", b"#" + b"x" * 100,
                                                 max_script_size=64)
        self.assertEqual(status, bridge.STATUS_REJECTED)
        self.assertIn(b"size limit", err)
        status, _, out, err = bridge.run_source(b"output.py", b"print('x'*1000)\n",
                                                max_output_size=64)
        self.assertEqual(status, bridge.STATUS_OUTPUT_LIMIT)
        self.assertLessEqual(len(out) + len(err), 64)

    def test_timeout_and_unsafe_names(self):
        status, _, _, _ = bridge.run_source(b"slow.py", b"import time; time.sleep(2)\n", timeout=0.2)
        self.assertEqual(status, bridge.STATUS_TIMEOUT)
        for name in (b"../evil.py", b"x.txt", b"a\\b.py"):
            self.assertEqual(bridge.run_source(name, b"print(1)\n")[0], bridge.STATUS_REJECTED)

    def test_malformed_and_interrupted_clients_do_not_kill_helper(self):
        server, _ = self.start_server()
        with socket.create_connection(server.server_address, timeout=3) as sock:
            sock.sendall(b"APY")
        with socket.create_connection(server.server_address, timeout=3) as sock:
            sock.settimeout(3)
            sock.sendall(b"NOPE")
            self.assertEqual(sock.recv(1), b"")
        with socket.create_connection(server.server_address, timeout=3) as sock:
            sock.settimeout(4)
            self.assertEqual(self.request(sock, 1, bridge.RUN_SCRIPT, b"ok.py", b"print(42)\n")[2], b"42\r\n")

    def test_host_app_launch_protocol_and_allowlist(self):
        launched = []

        def stub(app):
            launched.append(app)
            return b"stub calculator launched\r\n"

        server, _ = self.start_server(app_launcher=stub)
        with socket.create_connection(server.server_address, timeout=3) as sock:
            sock.settimeout(4)
            status, code, out, err = self.request(sock, 1, bridge.LAUNCH_APP, b"", b"calc")
            self.assertEqual((status, code, out, err),
                             (bridge.STATUS_OK, 0, b"stub calculator launched\r\n", b""))
            self.assertEqual(launched, [b"calc"])
            bad = self.request(sock, 2, bridge.LAUNCH_APP, b"", b"notepad")
            self.assertEqual(bad[0], bridge.STATUS_REJECTED)
            self.assertIn(b"Unsupported host application", bad[3])
            too_long = self.request(sock, 3, bridge.LAUNCH_APP, b"", b"x" * 33)
            self.assertEqual(too_long[0], bridge.STATUS_REJECTED)
            self.assertEqual(server.last_app, b"x" * 33)

    def test_default_host_launcher_rejects_unknown_apps(self):
        with self.assertRaises(ValueError):
            bridge.launch_host_app(b"notepad")
        with self.assertRaises(ValueError):
            bridge.launch_host_app(b"cmd")

    def test_helper_unavailable_is_connection_refused(self):
        probe = socket.socket()
        probe.bind(("127.0.0.1", 0))
        addr = probe.getsockname()
        probe.close()
        with self.assertRaises(OSError):
            socket.create_connection(addr, timeout=0.2)


if __name__ == "__main__":
    unittest.main(verbosity=2)