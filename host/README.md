# RISC-V Host Tools

Host-side programs for interacting with the RISC-V SoC over PCIe.

## Building

```bash
bash build.sh
```

## Programs

### loader

Load a binary to IMEM:

```bash
sudo ./bin/loader <program.bin> <pci_addr> <iommu_group>
sudo ./bin/loader --no-run <program.bin> <pci_addr> <iommu_group>
```

Use `--no-run` for UART programs (let uart_console start the CPU).

### uart_console

Virtual UART terminal:

```bash
sudo ./bin/uart_console <pci_addr> <iommu_group>
```

- Starts the CPU
- Shows CPU output (TX)
- Sends keyboard input to CPU (RX)
- Ctrl-C to exit

### Other Tools

- `riscv_host` - RV32I instruction tests
- `test_logger` - CPU logger test
- `test_sniffer` - Bus sniffer test
- `test_programs` - Run sum.c/factorial.c

## Typical Usage

```bash
# Set environment
export PCIE_EP=0000:b1:00.0
export GRP=12

# Load and run UART program
sudo ./bin/loader --no-run ../sw/uart_test.bin $PCIE_EP $GRP
sudo ./bin/uart_console $PCIE_EP $GRP

# Load and run non-UART program
sudo ./bin/loader ../sw/sum.bin $PCIE_EP $GRP 100
```

## VFIO Setup

```bash
PCI=0000:b1:00.0
echo $PCI | sudo tee /sys/bus/pci/devices/$PCI/driver/unbind 2>/dev/null
echo vfio-pci | sudo tee /sys/bus/pci/devices/$PCI/driver_override
echo $PCI | sudo tee /sys/bus/pci/drivers/vfio-pci/bind
GRP=$(basename $(readlink /sys/bus/pci/devices/$PCI/iommu_group))
```
