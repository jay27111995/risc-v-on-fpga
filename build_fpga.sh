#!/usr/bin/env bash

# RISC-V on FPGA build script for Intel Agilex 7

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

rm -rf build
mkdir build
cp fpga/* build/
cp src/alu.sv build/
cp src/regfile.sv build/
cp src/decoder.sv build/
cp src/riscv_soc.sv build/
cd build
rm -f pcie_ed.qsf
sed -i "s/__REVID__/${revid}/" pcie_ed.tcl
qsys-script --script=pcie_ed.tcl --export pcie_ed.qsys
qsys-generate pcie_ed.qsys --synthesis=VERILOG
rm -f pcie_ed.qsf
cp ../fpga/pcie_ed.qsf pcie_ed.qsf
quartus_sh --flow compile pcie_ed
gitdesc=$(git describe --dirty --always --abbrev=7)
output=output_files/pcie_ed.sof
md5=$(md5sum ${output} | cut -d ' ' -f1)
final=riscv-soc-revid-${revid}-git-${gitdesc}-md5-${md5}.sof
cp output_files/pcie_ed.sof ${final}
cp ${final} ../
echo "Build complete: ${final}"
