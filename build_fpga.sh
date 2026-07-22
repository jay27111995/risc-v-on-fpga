#!/usr/bin/env bash

# RISC-V on FPGA build script for Intel Agilex 7

set -o pipefail

if [ $# -ne 1 ]; then
  echo "Error: Missing required argument."
  echo "Usage: $0 <rev_id>"
  exit 1
fi
if [[ ! $1 =~ ^0x[0-9a-fA-F][0-9a-fA-F]$ ]]; then
  echo "Error: Revision id must be 8-bit hex integer (e.g. 0x2a)."
  echo "Usage: $0 <rev_id>"
  exit 1
fi
revid=$1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

echo "=========================================="
echo "RISC-V on FPGA Build"
echo "Revision ID: ${revid}"
echo "Build dir: ${BUILD_DIR}"
echo "=========================================="

# Step 1: Setup build directory and copy RTL files
echo ""
echo "[Step 1/4] Setting up build directory..."
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cp "${SCRIPT_DIR}"/rtl/* "${BUILD_DIR}/"

cd "${BUILD_DIR}"

# Step 2: Configure and generate
echo "[Step 2/4] Generating Qsys..."
rm -f pcie_ed.qsf
sed -i "s/__REVID__/${revid}/" pcie_ed.tcl
qsys-script --script=pcie_ed.tcl --export pcie_ed.qsys
if [ ! -f pcie_ed.qsys ]; then
  echo "Error: pcie_ed.qsys was not generated"
  exit 1
fi

echo "[Step 3/4] Generating Verilog..."
qsys-generate pcie_ed.qsys --synthesis=VERILOG
if [ ! -d "ip/pcie_ed" ]; then
  echo "Error: qsys-generate failed"
  exit 1
fi

# Step 4: Quartus compile
echo "[Step 4/4] Running Quartus compilation..."
rm -f pcie_ed.qsf
cp "${SCRIPT_DIR}/rtl/pcie_ed.qsf" pcie_ed.qsf

if ! quartus_sh --flow compile pcie_ed; then
  echo "Error: Quartus compilation failed"
  exit 1
fi

# Package output
gitdesc=$(git -C "${SCRIPT_DIR}" describe --dirty --always --abbrev=7 2>/dev/null || echo "unknown")
output=output_files/pcie_ed.sof
md5=$(md5sum "${output}" | cut -d ' ' -f1)
final="riscv-soc-revid-${revid}-git-${gitdesc}-md5-${md5}.sof"
cp "${output}" "${final}"
cp "${final}" "${SCRIPT_DIR}/"

echo ""
echo "=========================================="
echo "Build complete: ${final}"
echo "=========================================="
