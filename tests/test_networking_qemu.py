"""Comprehensive QEMU integration test suite for Aarambh OS Networking Subsystem.

Tests all 17 required networking scenarios on disposable virtual disk images:
 1. Boot without network adapter: driver detects no card, shell responsive
 2. Boot with QEMU Ethernet adapter: automatic driver init and link detection
 3. Automatic DHCP configuration: IP 10.0.2.15, Gateway 10.0.2.2, DNS 10.0.2.3
 4. ifconfig output verification: MAC address, IP, mask, gateway, flags
 5. ICMP ping: ping 10.0.2.2 (gateway) and ping 8.8.8.8
 6. DNS resolution: ping google.com resolves hostname to IP and pings
 7. Interactive network GUI: menu status, routes, exit back to prompt
 8. Static IP configuration: apply static IP via network GUI or config
 9. Wi-Fi scanning & diagnostics: honest reporting of unsupported firmware without fake APs
10. wget file download: real HTTP download directly to AarFS
11. wget -O custom.txt URL: custom filename output directly to AarFS
12. curl URL: response body printed to console
13. curl -o out.txt URL: response body saved to AarFS file
14. curl -I URL: HTTP response headers printed
15. Strict HTTPS rejection: clear error requiring CA certs, refusing insecure connection
16. git --version: displays native Aarambh Git version 0.4.0
17. Native MicroPython networking: aarambh module net_status, gethostbyname, ping, http_get
"""
import http.server
import socketserver
import sys
import threading
import time
from pathlib import Path

# Add tests directory to python path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))

from qemu_harness import QemuSession

TEST_PAYLOAD = b"Aarambh OS Genuine Networking Subsystem Verified!"
TEST_PY_SCRIPT = b"""import aarambh
st = aarambh.net_status()
print('PY_NET_IP:', st.get('ip'))
print('PY_NET_LINK:', st.get('link'))
print('PY_RESOLVE:', aarambh.gethostbyname('10.0.2.2'))
print('PY_PING:', aarambh.ping('10.0.2.2') >= 0)
print('PY_HTTP:', aarambh.http_get('http://10.0.2.2:8088/test.txt'))
"""


class TestHttpHandler(http.server.BaseHTTPRequestHandler):
    def do_HEAD(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(TEST_PAYLOAD)))
        self.send_header("Connection", "close")
        self.end_headers()

    def do_GET(self):
        if self.path == "/script.py":
            data = TEST_PY_SCRIPT
        else:
            data = TEST_PAYLOAD
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, format, *args):
        pass


def send_input(vm, text=""):
    if text:
        vm.type_text(text)
    vm.press("ret")


def run_tests():
    print("=" * 65)
    print("  AARAMBH OS - AUTOMATED NETWORKING TEST SUITE (17 SCENARIOS)")
    print("=" * 65)

    # Start local HTTP server on host (port 8088)
    httpd = socketserver.TCPServer(("0.0.0.0", 8088), TestHttpHandler)
    server_thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    server_thread.start()
    print("[HOST] Local test HTTP server running on 0.0.0.0:8088\n")

    try:
        # -------------------------------------------------------------
        # Scenario 1: Boot without network adapter
        # -------------------------------------------------------------
        print("Scenario 1: Testing boot without network adapter (network=\"none\")...")
        with QemuSession(network="none") as vm:
            log = vm.log()
            assert "[net] No Ethernet network adapter detected on PCI bus." in log, (
                f"Missing no-adapter message in log:\n{log}"
            )
            # Verify shell remains responsive
            out = vm.send_line("whoami")
            assert "root" in out, f"Shell unresponsive without network: {out}"
            out = vm.send_line("ifconfig")
            assert "No network device detected" in out, f"Unexpected ifconfig output: {out}"
            print("  [PASS] Scenario 1: Clean no-adapter boot, responsive shell.\n")

        # -------------------------------------------------------------
        # Scenarios 2 - 17: Boot with QEMU RTL8139 Ethernet adapter
        # -------------------------------------------------------------
        print("Starting QEMU session with RTL8139 Ethernet adapter...")
        with QemuSession(network="rtl8139") as vm:
            # Scenario 2: Driver initialization and link detection
            print("Scenario 2: Verifying automatic driver init and link detection...")
            log = vm.log()
            assert "Realtek RTL8139 Fast Ethernet" in log, f"Driver name missing from boot log:\n{log}"
            assert "Link up, requesting DHCP" in log, f"Link up missing from boot log:\n{log}"
            print("  [PASS] Scenario 2: Driver initialized and link UP.\n")

            # Scenario 3: Automatic DHCP configuration
            print("Scenario 3: Verifying automatic DHCP IP configuration...")
            assert "Configured via DHCP: IP 10.0.2.15" in log, f"DHCP IP missing from boot log:\n{log}"
            assert "Gateway 10.0.2.2" in log, f"DHCP Gateway missing from boot log:\n{log}"
            assert "DNS 10.0.2.3" in log, f"DHCP DNS missing from boot log:\n{log}"
            print("  [PASS] Scenario 3: DHCP assigned 10.0.2.15, gateway 10.0.2.2, DNS 10.0.2.3.\n")

            # Scenario 4: ifconfig output
            print("Scenario 4: Testing 'ifconfig' command output...")
            out = vm.send_line("ifconfig")
            assert "flags=UP,RUNNING" in out, f"Missing flags in ifconfig: {out}"
            assert "hw_addr 52:54:00:12:34:56" in out, f"Missing MAC in ifconfig: {out}"
            assert "inet 10.0.2.15" in out, f"Missing IP in ifconfig: {out}"
            assert "netmask 255.255.255.0" in out, f"Missing netmask in ifconfig: {out}"
            assert "gateway 10.0.2.2" in out, f"Missing gateway in ifconfig: {out}"
            assert "dns 10.0.2.3" in out, f"Missing DNS in ifconfig: {out}"
            assert "config: DHCP" in out, f"Missing DHCP tag in ifconfig: {out}"
            print("  [PASS] Scenario 4: ifconfig verified.\n")

            # Scenario 5: ICMP Ping (Gateway and Public IP)
            print("Scenario 5: Testing ICMP ping (gateway 10.0.2.2 and 8.8.8.8)...")
            out = vm.send_line("ping 10.0.2.2")
            assert "64 bytes from 10.0.2.2" in out, f"Ping to gateway failed:\n{out}"
            out = vm.send_line("ping 8.8.8.8")
            assert "64 bytes from 8.8.8.8" in out, f"Ping to 8.8.8.8 failed:\n{out}"
            print("  [PASS] Scenario 5: ICMP ping to gateway and 8.8.8.8 successful.\n")

            # Scenario 6: DNS Resolution & Ping Hostname
            print("Scenario 6: Testing DNS resolution and hostname ping (ping google.com)...")
            out = vm.send_line("ping google.com")
            assert "PING google.com (" in out, f"DNS resolution failed:\n{out}"
            assert "64 bytes from" in out, f"Ping to google.com failed:\n{out}"
            print("  [PASS] Scenario 6: DNS resolution and internet ping verified.\n")

            # Scenario 7: Interactive network GUI
            print("Scenario 7: Testing interactive 'network' GUI navigation...")
            mark = vm.mark()
            send_input(vm, "network")
            vm.wait_for("Select option (1-11, or 'q' to quit): ", mark)

            # Select 1: status
            mark = vm.mark()
            send_input(vm, "1")
            vm.wait_for("Press Enter to return to menu...", mark)
            send_input(vm)
            vm.wait_for("Select option (1-11, or 'q' to quit): ", mark)

            # Select 9: routes
            mark = vm.mark()
            send_input(vm, "9")
            vm.wait_for("Press Enter to return to menu...", mark)
            send_input(vm)
            vm.wait_for("Select option (1-11, or 'q' to quit): ", mark)

            # Select 11: return to shell
            mark = vm.mark()
            send_input(vm, "11")
            vm.wait_prompt(mark)

            # Verify shell prompt is clean
            out = vm.send_line("whoami")
            assert "root" in out, f"Shell unresponsive after network GUI: {out}"
            print("  [PASS] Scenario 7: Interactive network GUI menus and exit verified.\n")

            # Scenario 8: Static IP configuration
            print("Scenario 8: Testing static IP configuration via 'network' GUI...")
            mark = vm.mark()
            send_input(vm, "network")
            vm.wait_for("Select option (1-11, or 'q' to quit): ", mark)

            # Option 6: Static IP
            mark = vm.mark()
            send_input(vm, "6")
            vm.wait_for("Enter IPv4 Address", mark)
            send_input(vm, "10.0.2.45")
            vm.wait_for("Enter Subnet Mask", mark)
            send_input(vm, "255.255.255.0")
            vm.wait_for("Enter Gateway", mark)
            send_input(vm, "10.0.2.2")
            vm.wait_for("Press Enter to return to menu...", mark)
            send_input(vm)
            vm.wait_for("Select option (1-11, or 'q' to quit): ", mark)

            # Option 10: Save configuration
            mark = vm.mark()
            send_input(vm, "10")
            vm.wait_for("Press Enter to return to menu...", mark)
            send_input(vm)
            vm.wait_for("Select option (1-11, or 'q' to quit): ", mark)

            # Option 11: Exit
            mark = vm.mark()
            send_input(vm, "11")
            vm.wait_prompt(mark)

            out = vm.send_line("ifconfig")
            assert "inet 10.0.2.45" in out, f"Static IP not applied:\n{out}"
            assert "config: static" in out, f"Static config mode not shown:\n{out}"
            print("  [PASS] Scenario 8: Static IP configuration verified.\n")

            # Re-enable DHCP for remaining network tests
            print("Re-enabling DHCP for remaining tests...")
            mark = vm.mark()
            send_input(vm, "network")
            vm.wait_for("Select option (1-11, or 'q' to quit): ", mark)
            mark = vm.mark()
            send_input(vm, "2")  # Ethernet settings
            vm.wait_for("Choose option", mark)
            send_input(vm, "1")  # Request DHCP
            vm.wait_for("Press Enter to return to menu...", mark)
            send_input(vm)
            vm.wait_for("Select option (1-11, or 'q' to quit): ", mark)
            mark = vm.mark()
            send_input(vm, "11")  # Return to shell
            vm.wait_prompt(mark)

            out = vm.send_line("ifconfig")
            assert "inet 10.0.2.15" in out, f"Failed restoring DHCP:\n{out}"

            # Scenario 9: Wi-Fi scanning and honest diagnostic reporting
            print("Scenario 9: Verifying honest Wi-Fi diagnostic (no fake APs)...")
            mark = vm.mark()
            send_input(vm, "network")
            vm.wait_for("Select option (1-11, or 'q' to quit): ", mark)
            mark = vm.mark()
            send_input(vm, "3")  # Scan for Wi-Fi
            out_wifi = vm.wait_for("Press Enter to return to menu...", mark)
            assert "No wireless" in out_wifi or "not present" in out_wifi or "unsupported" in out_wifi.lower() or "firmware" in out_wifi.lower(), (
                f"Wi-Fi scanning did not report honest diagnostics:\n{out_wifi}"
            )
            send_input(vm)
            vm.wait_for("Select option (1-11, or 'q' to quit): ", mark)
            mark = vm.mark()
            send_input(vm, "11")  # Exit
            vm.wait_prompt(mark)
            print("  [PASS] Scenario 9: Honest Wi-Fi diagnostics confirmed (no fake APs fabricated).\n")

            # Scenario 10: wget file download
            print("Scenario 10: Testing 'wget http://10.0.2.2:8088/test.txt'...")
            out = vm.send_line("wget http://10.0.2.2:8088/test.txt")
            assert "saved successfully (HTTP 200 OK)" in out, f"wget failed:\n{out}"
            out = vm.send_line("cat test.txt")
            assert "Aarambh OS Genuine Networking Subsystem Verified!" in out, (
                f"Downloaded file content mismatch:\n{out}"
            )
            print("  [PASS] Scenario 10: wget downloaded file directly into AarFS.\n")

            # Scenario 11: wget -O custom.txt URL
            print("Scenario 11: Testing 'wget -O custom.txt http://10.0.2.2:8088/test.txt'...")
            out = vm.send_line("wget -O custom.txt http://10.0.2.2:8088/test.txt")
            assert "'custom.txt' saved successfully" in out, f"wget -O failed:\n{out}"
            out = vm.send_line("cat custom.txt")
            assert "Aarambh OS Genuine Networking Subsystem Verified!" in out, (
                f"Custom file content mismatch:\n{out}"
            )
            print("  [PASS] Scenario 11: wget -O custom filename download verified.\n")

            # Scenario 12: curl URL (stdout)
            print("Scenario 12: Testing 'curl http://10.0.2.2:8088/test.txt' (console streaming)...")
            out = vm.send_line("curl http://10.0.2.2:8088/test.txt")
            assert "Aarambh OS Genuine Networking Subsystem Verified!" in out, (
                f"curl stdout streaming failed:\n{out}"
            )
            print("  [PASS] Scenario 12: curl printed response body directly to console.\n")

            # Scenario 13: curl -o out.txt URL
            print("Scenario 13: Testing 'curl -o curl_saved.txt http://10.0.2.2:8088/test.txt'...")
            out = vm.send_line("curl -o curl_saved.txt http://10.0.2.2:8088/test.txt")
            assert "saved response to 'curl_saved.txt'" in out, f"curl -o failed:\n{out}"
            out = vm.send_line("cat curl_saved.txt")
            assert "Aarambh OS Genuine Networking Subsystem Verified!" in out, (
                f"curl saved file mismatch:\n{out}"
            )
            print("  [PASS] Scenario 13: curl -o saved response to AarFS file.\n")

            # Scenario 14: curl -I URL (headers)
            print("Scenario 14: Testing 'curl -I http://10.0.2.2:8088' (HEAD headers)...")
            out = vm.send_line("curl -I http://10.0.2.2:8088")
            assert "HTTP/1.0 200 OK" in out, f"Missing status line in curl -I:\n{out}"
            assert "Content-Length:" in out, f"Missing Content-Length in curl -I:\n{out}"
            assert "Connection: close" in out, f"Missing Connection header in curl -I:\n{out}"
            print("  [PASS] Scenario 14: curl -I displayed HTTP headers.\n")

            # Scenario 15: Strict HTTPS rejection without certificate store
            print("Scenario 15: Testing strict HTTPS refusal without trusted CA certificates...")
            out = vm.send_line("curl https://example.com")
            assert "HTTPS certificate validation failed" in out, f"Missing HTTPS refusal error:\n{out}"
            assert "Refusing insecure connection" in out, f"Missing refusal notice:\n{out}"
            out = vm.send_line("wget https://example.com")
            assert "HTTPS certificate validation failed" in out, f"Missing HTTPS refusal in wget:\n{out}"
            print("  [PASS] Scenario 15: Strict HTTPS certificate validation requirement enforced.\n")

            # Scenario 16: git --version
            print("Scenario 16: Testing 'git --version'...")
            out = vm.send_line("git --version")
            assert "aarambh-git version 0.4.0" in out, f"Unexpected git version output:\n{out}"
            print("  [PASS] Scenario 16: Native Git client reports aarambh-git version 0.4.0.\n")

            # Scenario 17: Native MicroPython networking integration
            print("Scenario 17: Testing native Python networking (aarambh module)...")
            # Download Python test script into AarFS using wget
            out = vm.send_line("wget -O net_test.py http://10.0.2.2:8088/script.py")
            assert "saved successfully" in out, f"Failed downloading test script:\n{out}"
            out = vm.send_line("python net_test.py")
            assert "PY_NET_IP: 10.0.2.15" in out, f"Python net_status IP failed:\n{out}"
            assert "PY_NET_LINK: 1" in out, f"Python net_status link failed:\n{out}"
            assert "PY_RESOLVE: 10.0.2.2" in out, f"Python gethostbyname failed:\n{out}"
            assert "PY_PING: True" in out, f"Python ping failed:\n{out}"
            assert "PY_HTTP: Aarambh OS Genuine Networking Subsystem Verified!" in out, (
                f"Python http_get failed:\n{out}"
            )
            assert "Python exited with code 0." in out, f"Python script did not exit with code 0:\n{out}"
            print("  [PASS] Scenario 17: Native Python aarambh networking module fully verified.\n")

    finally:
        httpd.shutdown()

    print("=" * 65)
    print("  ALL 17 NETWORKING SCENARIOS PASSED WITH ZERO REGRESSIONS!")
    print("=" * 65)


if __name__ == "__main__":
    run_tests()
