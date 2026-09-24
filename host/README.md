# Host Tools

Tools that run on the host PC to interact with the FPGA over PCIe.

## Building

```bash
./build.sh
```

## Tools

### elf_loader
Loads an ELF file to the FPGA, routing sections to correct memories.

```bash
sudo ./bin/elf_loader <program.elf> <pci_addr> <iommu_group>
sudo ./bin/elf_loader ../sw/build/hello_sum.elf 0000:b1:00.0 12
```

- `.text` (code) → IMEM
- `.rodata`, `.data`, `.bss` → DMEM

### uart_console
Interactive UART terminal over PCIe.

```bash
sudo ./bin/uart_console <pci_addr> <iommu_group>
sudo ./bin/uart_console 0000:b1:00.0 12
```

Ctrl-C to exit.

### Other Tools
- `loader` - Old binary loader (IMEM only)
- `riscv_host` - Instruction tests
- `test_sniffer` - Bus sniffer debug
- `test_logger` - CPU logger debug

## PCIe Setup

```bash
PCIE_EP=0000:b1:00.0
GRP=12

# Bind to VFIO
echo ${PCIE_EP} | sudo tee /sys/bus/pci/devices/${PCIE_EP}/driver/unbind
echo vfio-pci | sudo tee /sys/bus/pci/devices/${PCIE_EP}/driver_override
echo ${PCIE_EP} | sudo tee /sys/bus/pci/drivers/vfio-pci/bind
```

## UART Memory Map

| DMEM Offset | Description |
|-------------|-------------|
| 0x100-0x13F | TX buffer (16 words) |
| 0x140 | TX_HEAD |
| 0x144 | TX_TAIL |
| 0x200-0x23F | RX buffer (16 words) |
| 0x240 | RX_HEAD |
| 0x244 | RX_TAIL |
