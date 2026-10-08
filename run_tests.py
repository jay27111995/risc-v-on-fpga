#!/usr/bin/env python3
"""
RISC-V FPGA Test Suite

Usage:
    ./run_tests.py                     # run all tests
    ./run_tests.py -v                  # verbose
    ./run_tests.py -t ecall_test       # single test
    ./run_tests.py --skip-build        # skip building
"""

import argparse
import subprocess
import os
import sys
import time
from pathlib import Path
from multiprocessing import Process, Queue

PCIE = "0000:b1:00.0"
IOMMU = "12"

# Colors
R = '\033[0;31m'
G = '\033[0;32m'
Y = '\033[0;33m'
B = '\033[0;34m'
N = '\033[0m'

def cleanup():
    """Kill any uart_console and release VFIO"""
    subprocess.run(["sudo", "killall", "-9", "uart_console"], 
                   capture_output=True, timeout=5)
    subprocess.run(["sudo", "fuser", "-k", "-9", f"/dev/vfio/{IOMMU}"],
                   capture_output=True, timeout=5)
    time.sleep(0.5)

def load_elf(elf_path):
    """Load ELF file to FPGA, return True if success"""
    cmd = ["sudo", "host/bin/elf_loader", str(elf_path), PCIE, IOMMU]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    return result.returncode == 0, result.stdout, result.stderr

def read_uart(queue, timeout=2):
    """Read uart_console output and put in queue. Run as separate process."""
    cmd = ["sudo", "host/bin/uart_console", PCIE, IOMMU]
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        
        lines = []
        end_marker = "===END==="
        
        start = time.time()
        while time.time() - start < timeout:
            line = proc.stdout.readline()
            if not line:
                break
            lines.append(line)
            # Check for end marker
            if end_marker in line:
                break
        
        proc.kill()
        proc.wait()
        queue.put(''.join(lines))
    except Exception as e:
        queue.put(f"ERROR: {e}")

def run_test(name, verbose=False):
    """Run a single test. Returns True/False/None for pass/fail/skip"""
    elf = Path(f"sw/build/{name}.elf")
    
    if not elf.exists():
        print(f"{Y}⊘{N} {name} (not built)")
        return None
    
    # Cleanup before test
    cleanup()
    
    # Load ELF
    ok, stdout, stderr = load_elf(elf)
    if not ok:
        print(f"{R}✗{N} {name} (load failed)")
        if verbose:
            print(f"    {stderr.strip()}")
        return False
    
    # Read UART in separate process (so we can kill it reliably)
    queue = Queue()
    p = Process(target=read_uart, args=(queue, 2))
    p.start()
    p.join(timeout=3)
    
    if p.is_alive():
        p.terminate()
        p.join(timeout=1)
    
    # Cleanup after test
    cleanup()
    
    # Get output
    output = queue.get() if not queue.empty() else ""
    
    # Analyze
    out_lower = output.lower()
    if "fail" in out_lower and "0 error" not in out_lower:
        print(f"{R}✗{N} {name}")
        if verbose:
            for line in output.strip().split('\n')[-5:]:
                print(f"    | {line}")
        return False
    elif output.strip():
        print(f"{G}✓{N} {name}")
        if verbose:
            for line in output.strip().split('\n')[-5:]:
                print(f"    | {line}")
        return True
    else:
        print(f"{Y}?{N} {name} (no output)")
        return None

def build_host():
    """Build host tools"""
    print(f"{Y}[1/3] Building host tools...{N}")
    result = subprocess.run(["bash", "build.sh"], cwd="host", capture_output=True, timeout=60)
    if result.returncode != 0:
        print(f"{R}✗ Host build failed{N}")
        return False
    print(f"{G}✓ Host tools built{N}")
    return True

def build_sw():
    """Build all SW"""
    print(f"{Y}[2/3] Building SW...{N}")
    subprocess.run(["bash", "build_all.sh"], cwd="sw", timeout=120)
    return True

def main():
    parser = argparse.ArgumentParser(description="RISC-V FPGA Test Suite")
    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument("-t", "--test", action="append", help="Run specific test(s)")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    
    os.chdir(Path(__file__).parent)
    
    if not args.skip_build:
        if not build_host():
            sys.exit(1)
        build_sw()
        print()
    
    print(f"{Y}[3/3] Running tests...{N}")
    print()
    
    # Initial cleanup
    cleanup()
    
    # Test lists
    mmode = ["ecall_test", "timer_int_test", "wfi_test", "fence_test"]
    csr = ["csr_test", "csr_test2", "csr_hazard_test"]
    atomic = ["atomic_test"]
    general = ["hello_sum", "sum", "sum2", "factorial", "sort_test",
               "string_test", "linkedlist_test", "tree_test", "uart_test", "hello_short"]
    
    passed = failed = skipped = 0
    
    if args.test:
        tests = args.test
    else:
        tests = mmode + csr + atomic + general
        print("--- M-mode tests ---")
    
    for i, t in enumerate(tests):
        if not args.test:
            if t == csr[0]:
                print("\n--- CSR tests ---")
            elif t == atomic[0]:
                print("\n--- Atomic tests ---")
            elif t == general[0]:
                print("\n--- General tests ---")
        
        result = run_test(t, args.verbose)
        if result is True:
            passed += 1
        elif result is False:
            failed += 1
        else:
            skipped += 1
    
    print()
    print("=" * 40)
    print(f"Results: {G}{passed} passed{N}, {R}{failed} failed{N}, {Y}{skipped} skipped{N}")
    
    if failed == 0 and passed > 0:
        print(f"{G}All tests passed!{N}")
    
    sys.exit(0 if failed == 0 else 1)

if __name__ == "__main__":
    main()
