// Simple RISC-V CPU
// Connects: PC → IMEM → Decoder → Regfile → ALU → DMEM → Regfile
//
// Supported instructions:
//   R-type: ADD, SUB, AND, OR, XOR
//   I-type: ADDI, ANDI, ORI, XORI, LW
//   S-type: SW
//   B-type: BEQ
//
// Data Path:
//
//         ┌─────────────────────────────────────────────────────────────┐
//         │                                                             │
//         ▼                                                             │
//      ┌────┐      ┌──────┐      ┌─────────┐      ┌─────────┐           │
//      │ PC │─────►│ IMEM │─────►│ Decoder │─────►│ Regfile │           │
//      └────┘      └──────┘      └────┬────┘      └────┬────┘           │
//         ▲                           │                │                │
//         │                           │          rs1_data  rs2_data     │
//         │                           │                │       │        │
//         │                           ▼                ▼       │        │
//         │                      ┌─────────┐       ┌───────┐   │        │
//         │            imm ─────►│alu_src  │──────►│  ALU  │   │        │
//         │                      │   MUX   │       └───┬───┘   │        │
//         │                      └─────────┘           │       │        │
//         │                                       alu_result   │        │
//         │                                            │       │        │
//         │                                            ▼       ▼        │
//         │                                       ┌──────────────┐      │
//         │                                       │     DMEM     │      │
//         │                                       └──────┬───────┘      │
//         │                                              │              │
//         │                                         mem_rdata           │
//         │                                              │              │
//         │                                              ▼              │
//         │                                       ┌─────────────┐       │
//         │                                       │ mem_read    │       │
//         │                                       │    MUX      ├───────┘
//         │                                       └─────────────┘
//         │                                           rd_data
//         │                                              │
//         │          ┌───────────────────────────────────┘
//         │          │ (write back to regfile)
//         │          ▼
//         │     branch_addr
//         │          │
//         └──────────┘ (if take_branch)
//
module cpu (
    input  logic clk,
    input  logic rst,
    output logic [31:0] pc_out,      // for debug: current PC
    output logic [31:0] result       // for debug: ALU result or mem data
);

// Internal wires
logic [31:0] instr;          // instruction from IMEM
logic [31:0] rs1_data;       // data from register rs1
logic [31:0] rs2_data;       // data from register rs2
logic [31:0] imm;            // immediate value
logic [31:0] alu_b;          // ALU input B (rs2 or immediate)
logic [31:0] alu_result;     // ALU output
logic        alu_zero;       // ALU zero flag
logic [31:0] mem_rdata;      // data from DMEM
logic [31:0] rd_data;        // data to write to register (ALU result or mem data)

// Decoder outputs
logic [4:0]  rs1, rs2, rd;
logic [2:0]  alu_op;
logic        reg_write;
logic        alu_src;        // 0=rs2, 1=immediate
logic        mem_read;       // LW: read from memory
logic        mem_write;      // SW: write to memory
logic        branch;

// Branch logic
logic        take_branch;
logic [31:0] branch_addr;

assign take_branch = branch & alu_zero;  // branch if BEQ and values equal
assign branch_addr = pc_out + imm;       // branch target = PC + offset

// Program Counter
pc pc_inst (
    .clk(clk),
    .rst(rst),
    .branch(take_branch),
    .branch_addr(branch_addr),
    .pc_out(pc_out)
);

// Instruction Memory
imem imem_inst (
    .addr(pc_out),
    .instr(instr)
);

// Decoder
decoder decoder_inst (
    .instr(instr),
    .rs1(rs1),
    .rs2(rs2),
    .rd(rd),
    .imm(imm),
    .alu_op(alu_op),
    .reg_write(reg_write),
    .alu_src(alu_src),
    .mem_read(mem_read),
    .mem_write(mem_write),
    .branch(branch)
);

// Register File
regfile regfile_inst (
    .clk(clk),
    .we(reg_write),
    .rs1_addr(rs1),
    .rs2_addr(rs2),
    .rd_addr(rd),
    .rd_data(rd_data),           // changed: now comes from mux
    .rs1_data(rs1_data),
    .rs2_data(rs2_data)
);

// ALU input mux: select rs2 or immediate
assign alu_b = alu_src ? imm : rs2_data;

// ALU
alu alu_inst (
    .a(rs1_data),
    .b(alu_b),
    .op(alu_op),
    .result(alu_result),
    .zero(alu_zero)
);

// Data Memory
dmem dmem_inst (
    .clk(clk),
    .addr(alu_result),           // address = rs1 + imm (from ALU)
    .wdata(rs2_data),            // data to store (from rs2)
    .we(mem_write),              // SW: write enable
    .re(mem_read),               // LW: read enable
    .rdata(mem_rdata)            // data loaded
);

// Write-back mux: ALU result or memory data to register
assign rd_data = mem_read ? mem_rdata : alu_result;

// Debug output
assign result = rd_data;

endmodule
