#!/usr/bin/env python3
"""
RISC-V FPGA Test Suite

Usage:
    ./run_tests.py                     # defaults
    ./run_tests.py -v                  # verbose
    ./run_tests.py --pcie 0000:b1:00.0 --iommu 12
    ./run_tests.py --test ecall_test   # run single test
"""

import argparse
import subprocess
import sys
import os
import time
import select
from pathlib import Path

# Colors
RED = '\033[0;31m'
GREEN = '\033[0;32m'
YELLOW = '\033[0;33m'
BLUE = '\033[0;34m'
NC = '\033[0m'

class TestRunner:
    def __init__(self, pcie_addr, iommu_group, verbose=False, timeout=5):
        self.script_dir = Path(__file__).parent.resolve()
        self.pcie_addr = pcie_addr
        self.iommu_group = iommu_group
        self.verbose = verbose
        self.timeout = timeout
        
        self.elf_loader = self.script_dir / "host" / "bin" / "elf_loader"
        self.uart_console = self.script_dir / "host" / "bin" / "uart_console"
        
        # End markers that indicate test completed
        self.end_markers = ["DONE", "ALL TESTS PASSED", "HALTING", "Test Complete"]
        
        self.passed = 0
        self.failed = 0
        self.skipped = 0
        
    def log(self, msg):
        """Print only in verbose mode"""
        if self.verbose:
            print(f"    {msg}")
    
    def run_cmd(self, cmd, timeout=30, capture=True, cwd=None, env=None):
        """Run command and return (returncode, stdout, stderr)"""
        self.log(f"Running: {' '.join(cmd)}")
        try:
            result = subprocess.run(
                cmd,
                capture_output=capture,
                text=True,
                timeout=timeout,
                cwd=cwd,
                env=env,
            )
            return result.returncode, result.stdout, result.stderr
        except subprocess.TimeoutExpired:
            self.log("Command timed out")
            return -1, "", "timeout"
        except KeyboardInterrupt:
            print(f"\n{YELLOW}Interrupted{NC}")
            sys.exit(130)
        except Exception as e:
            self.log(f"Command failed: {e}")
            return -1, "", str(e)
    
    def build_host(self):
        """Build host tools"""
        print(f"{YELLOW}[1/3] Building host tools...{NC}")
        host_dir = self.script_dir / "host"
        
        rc, stdout, stderr = self.run_cmd(["bash", "build.sh"], timeout=60, cwd=host_dir)
        if rc != 0:
            print(f"{RED}✗ Host build failed{NC}")
            if self.verbose:
                print(f"    stdout: {stdout}")
                print(f"    stderr: {stderr}")
            return False
        
        print(f"{GREEN}✓ Host tools built{NC}")
        return True
    
    def build_sw(self):
        """Build all SW examples"""
        print(f"{YELLOW}[2/3] Building SW examples...{NC}")
        sw_dir = self.script_dir / "sw"
        
        rc, stdout, stderr = self.run_cmd(["bash", "build_all.sh"], timeout=120, capture=not self.verbose, cwd=sw_dir)
        if self.verbose:
            print(stdout)
        return True
    
    def run_test(self, name):
        """Run a single test and return True if passed"""
        elf = self.script_dir / "sw" / "build" / f"{name}.elf"
        
        if not elf.exists():
            print(f"{YELLOW}⊘{NC} {name} (not built)")
            self.skipped += 1
            return None
        
        # Load ELF
        self.log(f"Loading {elf}")
        cmd = ["sudo", str(self.elf_loader), str(elf), self.pcie_addr, self.iommu_group]
        rc, stdout, stderr = self.run_cmd(cmd, timeout=10)
        
        if rc != 0:
            print(f"{RED}✗{NC} {name} (load failed)")
            self.log(f"stdout: {stdout}")
            self.log(f"stderr: {stderr}")
            self.failed += 1
            return False
        
        self.log(f"Load output: {stdout.strip()}")
        
        # Read UART output until we see end marker or timeout
        output = self.read_uart_until_done()
        
        # Analyze result
        output_lower = output.lower()
        
        if "fail" in output_lower or "error" in output_lower and "0 error" not in output_lower:
            print(f"{RED}✗{NC} {name}")
            for line in output.strip().split('\n')[-5:]:
                print(f"    | {line}")
            self.failed += 1
            return False
        elif "pass" in output_lower or "done" in output_lower:
            print(f"{GREEN}✓{NC} {name}")
            if self.verbose:
                for line in output.strip().split('\n'):
                    print(f"    | {line}")
            self.passed += 1
            return True
        elif output.strip():
            print(f"{GREEN}✓{NC} {name} (completed)")
            if self.verbose:
                for line in output.strip().split('\n')[-5:]:
                    print(f"    | {line}")
            self.passed += 1
            return True
        else:
            print(f"{YELLOW}?{NC} {name} (no output)")
            self.skipped += 1
            return None
    
    def read_uart_until_done(self):
        """Read uart_console output until end marker or timeout"""
        cmd = ["sudo", str(self.uart_console), self.pcie_addr, self.iommu_group]
        
        try:
            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            
            output_lines = []
            start_time = time.time()
            found_end = False
            
            while time.time() - start_time < self.timeout:
                ready, _, _ = select.select([proc.stdout], [], [], 0.1)
                if ready:
                    line = proc.stdout.readline()
                    if not line:
                        break  # EOF
                    output_lines.append(line)
                    self.log(f"UART: {line.rstrip()}")
                    
                    # Check for end marker
                    line_upper = line.upper()
                    for marker in self.end_markers:
                        if marker in line_upper:
                            found_end = True
                            break
                    
                    if found_end:
                        break
            
            # Kill uart_console
            subprocess.run(["sudo", "fuser", "-k", "-9", f"/dev/vfio/{self.iommu_group}"],
                          capture_output=True, timeout=2)
            proc.kill()
            try:
                proc.wait(timeout=1)
            except:
                pass
            time.sleep(0.5)  # Wait for VFIO to release
            
            return ''.join(output_lines)
            
        except Exception as e:
            self.log(f"UART read error: {e}")
            subprocess.run(["sudo", "fuser", "-k", "-9", f"/dev/vfio/{self.iommu_group}"],
                          capture_output=True, timeout=2)
            time.sleep(0.5)
            return ""
    
    def run_suite(self, tests=None):
        """Run the full test suite"""
        print(f"{BLUE}=== RISC-V Test Suite ==={NC}")
        print(f"PCIe: {self.pcie_addr}, IOMMU group: {self.iommu_group}")
        print(f"Verbose: {self.verbose}")
        print()
        
        # Check prerequisites
        if not self.elf_loader.exists():
            print(f"{RED}Error: elf_loader not found at {self.elf_loader}{NC}")
            print("Run build first or check path")
            return False
        
        # Test groups
        mmode_tests = ["ecall_test", "timer_int_test", "wfi_test", "fence_test"]
        csr_tests = ["csr_test", "csr_test2", "csr_hazard_test"]
        general_tests = ["hello_sum", "sum", "sum2", "factorial", "sort_test", 
                        "string_test", "linkedlist_test", "tree_test", "uart_test", "hello_short"]
        
        if tests:
            # Run specific tests
            print("--- Selected tests ---")
            for t in tests:
                self.run_test(t)
        else:
            # Run all tests
            print("--- M-mode tests ---")
            for t in mmode_tests:
                self.run_test(t)
            print()
            
            print("--- CSR tests ---")
            for t in csr_tests:
                self.run_test(t)
            print()
            
            print("--- General tests ---")
            for t in general_tests:
                self.run_test(t)
        
        print()
        print("=" * 40)
        print(f"Results: {GREEN}{self.passed} passed{NC}, {RED}{self.failed} failed{NC}, {YELLOW}{self.skipped} skipped{NC}")
        
        if self.failed == 0 and self.passed > 0:
            print(f"{GREEN}All tests passed!{NC}")
            return True
        return False


def main():
    # Handle Ctrl+C gracefully
    import signal
    signal.signal(signal.SIGINT, lambda s, f: sys.exit(130))
    
    parser = argparse.ArgumentParser(description="RISC-V FPGA Test Suite")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    parser.add_argument("--pcie", default="0000:b1:00.0", help="PCIe address")
    parser.add_argument("--iommu", default="12", help="IOMMU group")
    parser.add_argument("--timeout", type=int, default=5, help="Max seconds to wait for test completion")
    parser.add_argument("--test", "-t", action="append", help="Run specific test(s)")
    parser.add_argument("--skip-build", action="store_true", help="Skip building, just run tests")
    
    args = parser.parse_args()
    
    runner = TestRunner(
        pcie_addr=args.pcie,
        iommu_group=args.iommu,
        verbose=args.verbose,
        timeout=args.timeout,
    )
    
    if not args.skip_build:
        if not runner.build_host():
            sys.exit(1)
        runner.build_sw()
        print()
    
    print(f"{YELLOW}[3/3] Running tests on FPGA...{NC}")
    print()
    
    success = runner.run_suite(tests=args.test)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
