"""QEMU tests for native MicroPython execution inside Aarambh OS.

Tests run directly inside the guest VM without the Windows host helper.
"""
from qemu_harness import QemuSession


def main():
    print("Starting QEMU native Python test session (disposable disk, no host bridge)...")
    with QemuSession() as vm:
        # Test 1: Output
        print("Testing Test 1: print('Hello from Aarambh!')...")
        vm.send_line("touch test1.py")
        vm.send_line("scribe test1.py print('Hello from Aarambh!')")
        out1 = vm.send_line("python test1.py")
        print(f"Test 1 output:\n{out1}")
        assert "Hello from Aarambh!" in out1, f"Missing output in Test 1: {out1}"
        assert "Python exited with code 0." in out1, f"Missing exit code 0 in Test 1: {out1}"

        # Test 2: Arithmetic
        print("Testing Test 2: Arithmetic x=10; y=20; print(x+y)...")
        vm.send_line("touch test2.py")
        vm.send_line("scribe test2.py x = 10; y = 20; print(x + y)")
        out2 = vm.send_line("python test2.py")
        print(f"Test 2 output:\n{out2}")
        assert "30" in out2, f"Missing 30 in Test 2: {out2}"
        assert "Python exited with code 0." in out2, f"Missing exit code 0 in Test 2: {out2}"

        # Test 3: Variables and strings
        print("Testing Test 3: Variables and strings...")
        vm.send_line("touch test3.py")
        vm.send_line("scribe test3.py name = 'Aarambh'; print('Hello', name)")
        out3 = vm.send_line("python test3.py")
        print(f"Test 3 output:\n{out3}")
        assert "Hello Aarambh" in out3, f"Missing 'Hello Aarambh' in Test 3: {out3}"
        assert "Python exited with code 0." in out3, f"Missing exit code 0 in Test 3: {out3}"

        # Test 4: Exception handling without kernel crash
        print("Testing Test 4: Exception handling (division by zero)...")
        vm.send_line("touch test4.py")
        vm.send_line("scribe test4.py 1 // 0")
        out4 = vm.send_line("python test4.py")
        print(f"Test 4 output:\n{out4}")
        assert "ZeroDivisionError" in out4, f"Missing ZeroDivisionError in Test 4: {out4}"
        assert "Python exited with code 1." in out4, f"Missing exit code 1 in Test 4: {out4}"

        # Verify kernel stays responsive after exception
        id_out = vm.send_line("identity")
        assert "Aarambh OS PM32" in id_out, f"Kernel not responsive: {id_out}"

        # Test 5: 'py native' command
        print("Testing Test 5: 'py native test1.py'...")
        out5 = vm.send_line("py native test1.py")
        print(f"Test 5 output:\n{out5}")
        assert "Hello from Aarambh!" in out5, f"Missing output in Test 5: {out5}"
        assert "Python exited with code 0." in out5, f"Missing exit code 0 in Test 5: {out5}"

        # Test 6: 'py test1.py' fallback when host helper unavailable
        print("Testing Test 6: 'py test1.py' fallback when helper unavailable...")
        out6 = vm.send_line("py test1.py")
        print(f"Test 6 output:\n{out6}")
        assert "helper unavailable; executing natively via MicroPython" in out6, f"Missing fallback in Test 6: {out6}"
        assert "Hello from Aarambh!" in out6, f"Missing output in Test 6: {out6}"
        assert "Python exited with code 0." in out6, f"Missing exit code 0 in Test 6: {out6}"

        # Verify AarFS integrity
        ls_out = vm.send_line("ls")
        for fn in ("test1.py", "test2.py", "test3.py", "test4.py"):
            assert fn in ls_out, f"AarFS entry {fn} missing after runs: {ls_out}"

    print("ALL NATIVE PYTHON TESTS PASSED!")


if __name__ == "__main__":
    main()
