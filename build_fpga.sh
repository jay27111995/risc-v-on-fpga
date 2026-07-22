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

# Step 1: Setup build directory
echo ""
echo "[Step 1/7] Setting up build directory..."
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

# Step 2: Copy FPGA files
echo "[Step 2/7] Copying FPGA files..."
cp "${SCRIPT_DIR}"/fpga/*.sv "${BUILD_DIR}/" 2>/dev/null || true
cp "${SCRIPT_DIR}"/fpga/*.tcl "${BUILD_DIR}/"
cp "${SCRIPT_DIR}"/fpga/*.sdc.terp "${BUILD_DIR}/" 2>/dev/null || true
cp "${SCRIPT_DIR}"/fpga/pcie_ed.qsf "${BUILD_DIR}/"

# Step 3: Copy source files
echo "[Step 3/7] Copying source files..."
for f in alu.sv regfile.sv decoder.sv riscv_soc.sv; do
  if [ -f "${SCRIPT_DIR}/src/${f}" ]; then
    cp "${SCRIPT_DIR}/src/${f}" "${BUILD_DIR}/"
  else
    echo "Warning: ${f} not found in src/"
  fi
done

cd "${BUILD_DIR}"

# Step 4: Configure revision ID
echo "[Step 4/7] Configuring revision ID..."
rm -f pcie_ed.qsf.bak
sed -i "s/__REVID__/${revid}/" pcie_ed.tcl
if grep -q "__REVID__" pcie_ed.tcl; then
  echo "Error: Failed to substitute __REVID__ in pcie_ed.tcl"
  exit 1
fi

# Step 5: Generate Qsys system
echo "[Step 5/7] Running qsys-script..."
if ! qsys-script --script=pcie_ed.tcl --export pcie_ed.qsys; then
  echo "Error: qsys-script failed"
  exit 1
fi

if [ ! -f pcie_ed.qsys ]; then
  echo "Error: pcie_ed.qsys was not generated"
  exit 1
fi

# Step 6: Generate Verilog
echo "[Step 6/7] Running qsys-generate..."
if ! qsys-generate pcie_ed.qsys --synthesis=VERILOG; then
  echo "Error: qsys-generate failed"
  exit 1
fi

# Step 7: Quartus compile
echo "[Step 7/7] Running Quartus compilation (this may take 30+ minutes)..."
rm -f pcie_ed.qsf
cp "${SCRIPT_DIR}/fpga/pcie_ed.qsf" pcie_ed.qsf

if ! quartus_sh --flow compile pcie_ed; then
  echo "Error: Quartus compilation failed"
  exit 1
fi

# Package output
echo ""
echo "=========================================="
echo "Build successful! Packaging output..."
echo "=========================================="

gitdesc=$(git -C "${SCRIPT_DIR}" describe --dirty --always --abbrev=7 2>/dev/null || echo "unknown")
output=output_files/pcie_ed.sof

if [ ! -f "${output}" ]; then
  echo "Error: Output file ${output} not found"
  exit 1
fi

md5=$(md5sum "${output}" | cut -d ' ' -f1)
final="riscv-soc-revid-${revid}-git-${gitdesc}-md5-${md5}.sof"
cp "${output}" "${final}"
cp "${final}" "${SCRIPT_DIR}/"

echo ""
echo "=========================================="
echo "Build complete: ${final}"
echo "=========================================="
