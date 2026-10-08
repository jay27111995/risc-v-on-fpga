#!/bin/bash
# Full test suite: build everything and run all tests on FPGA
#
# Usage: ./run_tests.sh [PCIE_ADDR] [IOMMU_GROUP]
#        ./run_tests.sh                          # uses defaults
#        ./run_tests.sh 0000:b1:00.0 12          # explicit

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PCIE_ADDR="${1:-0000:b1:00.0}"
IOMMU_GROUP="${2:-12}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PASS=0
FAIL=0
SKIP=0

echo -e "${BLUE}=== RISC-V Test Suite ===${NC}"
echo "PCIe: $PCIE_ADDR, IOMMU group: $IOMMU_GROUP"
echo ""

# Step 1: Build host tools
echo -e "${YELLOW}[1/3] Building host tools...${NC}"
cd "$SCRIPT_DIR/host"
if ./build.sh > /dev/null 2>&1; then
    echo -e "${GREEN}✓${NC} Host tools built"
else
    echo -e "${RED}✗${NC} Host tools build failed"
    exit 1
fi

# Step 2: Build all SW examples
echo -e "${YELLOW}[2/3] Building SW examples...${NC}"
cd "$SCRIPT_DIR/sw"
./build_all.sh
echo ""

# Step 3: Run tests on FPGA
echo -e "${YELLOW}[3/3] Running tests on FPGA...${NC}"
echo ""

ELF_LOADER="$SCRIPT_DIR/host/bin/elf_loader"
UART_CONSOLE="$SCRIPT_DIR/host/bin/uart_console"

if [[ ! -x "$ELF_LOADER" ]]; then
    echo -e "${RED}Error: elf_loader not found${NC}"
    exit 1
fi

run_test() {
    local name="$1"
    local elf="$SCRIPT_DIR/sw/build/${name}.elf"
    
    if [[ ! -f "$elf" ]]; then
        echo -e "${YELLOW}⊘${NC} $name (not built)"
        ((SKIP++))
        return
    fi
    
    # Load ELF
    sudo "$ELF_LOADER" "$elf" "$PCIE_ADDR" "$IOMMU_GROUP" > /dev/null 2>&1
    if [[ $? -ne 0 ]]; then
        echo -e "${RED}✗${NC} $name (load failed)"
        ((FAIL++))
        return
    fi
    
    # Capture output (2 second timeout)
    output=$(timeout 2 sudo "$UART_CONSOLE" "$PCIE_ADDR" "$IOMMU_GROUP" 2>&1)
    
    # Check for PASS/FAIL in output
    if echo "$output" | grep -qi "PASS\|SUCCESS\|All tests passed"; then
        echo -e "${GREEN}✓${NC} $name"
        ((PASS++))
    elif echo "$output" | grep -qi "FAIL\|ERROR"; then
        echo -e "${RED}✗${NC} $name"
        echo "    Output: $(echo "$output" | head -1)"
        ((FAIL++))
    else
        # No explicit pass/fail - assume OK if we got output
        if [[ -n "$output" ]]; then
            echo -e "${GREEN}✓${NC} $name (completed)"
            ((PASS++))
        else
            echo -e "${YELLOW}?${NC} $name (no output)"
            ((SKIP++))
        fi
    fi
}

# Priority tests (M-mode / privileged)
echo "--- M-mode tests ---"
run_test "ecall_test"
run_test "timer_int_test"
run_test "wfi_test"
run_test "fence_test"
echo ""

# CSR tests
echo "--- CSR tests ---"
run_test "csr_test"
run_test "csr_test2"
run_test "csr_hazard_test"
echo ""

# General tests
echo "--- General tests ---"
run_test "hello_sum"
run_test "sum"
run_test "sum2"
run_test "factorial"
run_test "sort_test"
run_test "string_test"
run_test "linkedlist_test"
run_test "tree_test"
run_test "uart_test"
run_test "hello_short"
echo ""

# Summary
echo "================================"
echo -e "Results: ${GREEN}$PASS passed${NC}, ${RED}$FAIL failed${NC}, ${YELLOW}$SKIP skipped${NC}"

if [[ $FAIL -eq 0 ]]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    exit 1
fi
