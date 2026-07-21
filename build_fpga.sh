#!/usr/bin/env bash

# RISC-V on FPGA build script
# Builds the design for Intel Agilex 7

# Check if exactly one argument was provided
if [ $# -ne 1 ]; then
  echo "Error: Missing required argument."
  echo "Usage: $0 <rev_id>"
  exit 1
fi
if [[ ! $1 =~ ^0x[0-9a-f][0-9a-f]$ ]]; then
  echo "Error: Revision id must be 8-bit hex integer (e.g. 0x2a)."
  echo "Usage: $0 <rev_id>"
  exit 1
fi
revid=$1

set -x -e

# Clean and create build directory
rm -rf build
mkdir build

# Copy FPGA build configuration files
cp fpga/* build/

# Copy RTL source files
cp src/alu.sv build/
cp src/regfile.sv build/
cp src/decoder.sv build/
cp src/riscv_soc.sv build/

cd build

# ============================================
# Platform Designer (Qsys) - Generate system
# ============================================

# Insert revision ID into TCL script
rm -f pcie_ed.qsf
sed -i "s/__REVID__/${revid}/" pcie_ed.tcl

# Generate Qsys system from TCL description
# This creates the PCIe + AXI interconnect
qsys-script --script=pcie_ed.tcl --export pcie_ed.qsys

# Generate Verilog from Qsys system
qsys-generate pcie_ed.qsys --synthesis=VERILOG

# ============================================
# Quartus - Synthesize, Place, Route
# ============================================

# Copy pin assignments and settings
rm -f pcie_ed.qsf
cp ../fpga/pcie_ed.qsf pcie_ed.qsf

# Run full Quartus flow:
#   1. Analysis & Synthesis
#   2. Fitter (Place & Route)
#   3. Timing Analysis
#   4. Assembler (generate .sof)
quartus_sh --flow compile pcie_ed

# ============================================
# Output - Name bitstream with metadata
# ============================================

# Get git description for traceability
gitdesc=$(git describe --dirty --always --abbrev=7)

# Calculate MD5 for verification
output=output_files/pcie_ed.sof
md5=$(md5sum ${output} | cut -d ' ' -f1)

# Final filename with revision, git hash, and checksum
final=riscv-soc-revid-${revid}-git-${gitdesc}-md5-${md5}.sof

# Copy to build dir and project root
cp output_files/pcie_ed.sof ${final}
cp ${final} ../

echo "Build complete: ${final}"
