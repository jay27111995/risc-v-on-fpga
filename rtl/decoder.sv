// Instruction Decoder
// Extracts fields from 32-bit RISC-V instruction
//
// RISC-V Instruction Formats:
//
// R-type (register-register): ADD, SUB, AND, OR, XOR
// [31:25]  [24:20] [19:15] [14:12] [11:7] [6:0]
//  funct7   rs2     rs1    funct3   rd    opcode
//    7       5       5       3       5      7
//
// I-type (immediate): ADDI, LW
// [31:20]    [19:15] [14:12] [11:7] [6:0]
//  imm[11:0]  rs1    funct3   rd    opcode
//    12        5       3       5      7
//
// S-type (store): SW
// [31:25]    [24:20] [19:15] [14:12] [11:7]    [6:0]
//  imm[11:5]  rs2     rs1    funct3  imm[4:0]  opcode
//    7         5       5       3       5         7
//
// B-type (branch): BEQ, BNE
// [31]     [30:25]   [24:20] [19:15] [14:12] [11:8]    [7]      [6:0]
//  imm[12]  imm[10:5]  rs2     rs1    funct3  imm[4:1]  imm[11]  opcode
//    1        6         5       5       3       4         1        7
//
module decoder (
    input  logic [31:0] instr,       // 32-bit instruction
    
    // Register addresses
    output logic [4:0]  rs1,         // source register 1
    output logic [4:0]  rs2,         // source register 2
    output logic [4:0]  rd,          // destination register
    
    // Immediate value (sign-extended to 32 bits)
    output logic [31:0] imm,
    
    // Control signals
    output logic [3:0]  alu_op,      // ALU operation (4 bits)
    output logic        reg_write,   // write to register file?
    output logic        alu_src,     // ALU source: 0=rs2, 1=immediate
    output logic        mem_read,    // load from memory?
    output logic        mem_write,   // store to memory?
    output logic [2:0]  mem_op,      // memory size: 000=LB, 001=LH, 010=LW, 100=LBU, 101=LHU
    output logic        branch,      // branch instruction?
    output logic [2:0]  branch_op,   // branch type (funct3): 000=BEQ, 001=BNE, etc.
    output logic        jump,        // JAL or JALR instruction?
    output logic        jump_reg,    // JALR (jump to register+offset)?
    output logic        lui,         // LUI instruction (rd = imm)?
    output logic        auipc,       // AUIPC instruction (rd = PC + imm)?
    output logic        ebreak       // EBREAK instruction (halt CPU)?
);

// Extract fixed fields (same position for all formats)
logic [6:0] opcode;
logic [2:0] funct3;
logic [6:0] funct7;

assign opcode = instr[6:0];
assign funct3 = instr[14:12];
assign funct7 = instr[31:25];

// Register fields
assign rs1 = instr[19:15];
assign rs2 = instr[24:20];
assign rd  = instr[11:7];

// Opcodes
localparam OP_RTYPE  = 7'b0110011;  // ADD, SUB, AND, OR, XOR
localparam OP_ITYPE  = 7'b0010011;  // ADDI, ANDI, ORI, XORI
localparam OP_LOAD   = 7'b0000011;  // LW
localparam OP_STORE  = 7'b0100011;  // SW
localparam OP_BRANCH = 7'b1100011;  // BEQ, BNE
localparam OP_JAL    = 7'b1101111;  // JAL
localparam OP_JALR   = 7'b1100111;  // JALR
localparam OP_LUI    = 7'b0110111;  // LUI
localparam OP_AUIPC  = 7'b0010111;  // AUIPC
localparam OP_SYSTEM = 7'b1110011;  // ECALL, EBREAK

// Decode logic
always_comb begin
    // Defaults
    imm = 32'b0;
    alu_op = 4'b0000;
    reg_write = 0;
    alu_src = 0;
    mem_read = 0;
    mem_write = 0;
    mem_op = 3'b010;  // Default to word (LW/SW)
    branch = 0;
    branch_op = 3'b000;
    jump = 0;
    jump_reg = 0;
    lui = 0;
    auipc = 0;
    ebreak = 0;
    
    case (opcode)
        OP_RTYPE: begin  // R-type: ADD, SUB, AND, OR, XOR, SLL, SRL, SRA, SLT, SLTU
            reg_write = 1;
            alu_src = 0;  // use rs2
            case (funct3)
                3'b000: alu_op = (funct7[5]) ? 4'b0001 : 4'b0000;  // SUB or ADD
                3'b001: alu_op = 4'b0101;  // SLL
                3'b010: alu_op = 4'b1000;  // SLT
                3'b011: alu_op = 4'b1001;  // SLTU
                3'b100: alu_op = 4'b0100;  // XOR
                3'b101: alu_op = (funct7[5]) ? 4'b0111 : 4'b0110;  // SRA or SRL
                3'b110: alu_op = 4'b0011;  // OR
                3'b111: alu_op = 4'b0010;  // AND
                default: alu_op = 4'b0000;
            endcase
        end
        
        OP_ITYPE: begin  // I-type: ADDI, ANDI, ORI, XORI, SLLI, SRLI, SRAI, SLTI, SLTIU
            reg_write = 1;
            alu_src = 1;  // use immediate
            // I-type immediate: instr[31:20] = 12-bit signed immediate
            // Sign-extend to 32 bits by replicating bit 31
            imm = {{20{instr[31]}}, instr[31:20]};
            case (funct3)
                3'b000: alu_op = 4'b0000;  // ADDI
                3'b010: alu_op = 4'b1000;  // SLTI
                3'b011: alu_op = 4'b1001;  // SLTIU
                3'b001: begin              // SLLI
                    alu_op = 4'b0101;
                    imm = {27'b0, instr[24:20]};  // shamt is bottom 5 bits
                end
                3'b101: begin             // SRLI or SRAI
                    alu_op = (instr[30]) ? 4'b0111 : 4'b0110;
                    imm = {27'b0, instr[24:20]};  // shamt is bottom 5 bits
                end
                3'b111: alu_op = 4'b0010;  // ANDI
                3'b110: alu_op = 4'b0011;  // ORI
                3'b100: alu_op = 4'b0100;  // XORI
                default: alu_op = 4'b0000;
            endcase
        end
        
        OP_LOAD: begin  // LB, LH, LW, LBU, LHU: rd = memory[rs1 + imm]
            reg_write = 1;   // write loaded data to rd
            alu_src = 1;     // ALU uses immediate
            mem_read = 1;    // read from data memory
            mem_op = funct3; // 000=LB, 001=LH, 010=LW, 100=LBU, 101=LHU
            alu_op = 4'b0000; // ADD: address = rs1 + imm
            // I-type immediate (same as ADDI)
            imm = {{20{instr[31]}}, instr[31:20]};
        end
        
        OP_STORE: begin  // SB, SH, SW: memory[rs1 + imm] = rs2
            reg_write = 0;   // no register write
            alu_src = 1;     // ALU uses immediate
            mem_write = 1;   // write to data memory
            mem_op = funct3; // 000=SB, 001=SH, 010=SW
            alu_op = 4'b0000; // ADD: address = rs1 + imm
            // S-type immediate: split across instr[31:25] and instr[11:7]
            imm = {{20{instr[31]}}, instr[31:25], instr[11:7]};
        end
        
        OP_BRANCH: begin  // BEQ, BNE: if (condition) PC += imm
            branch = 1;
            branch_op = funct3;  // 000=BEQ, 001=BNE, 100=BLT, 101=BGE, 110=BLTU, 111=BGEU
            alu_op = 4'b0001; // SUB: compare rs1 - rs2, check zero flag
            // B-type immediate: imm[12|10:5|4:1|11], shifted left by 1 (2-byte aligned)
            imm = {{20{instr[31]}}, instr[7], instr[30:25], instr[11:8], 1'b0};
        end
        
        OP_JAL: begin  // JAL: rd = PC + 4, PC = PC + imm
            reg_write = 1;   // Write PC+4 to rd
            jump = 1;
            // J-type immediate: imm[20|10:1|11|19:12], shifted left by 1
            imm = {{12{instr[31]}}, instr[19:12], instr[20], instr[30:21], 1'b0};
        end
        
        OP_JALR: begin  // JALR: rd = PC + 4, PC = rs1 + imm
            reg_write = 1;   // Write PC+4 to rd
            jump = 1;
            jump_reg = 1;    // Target is rs1 + imm
            alu_src = 1;     // ALU uses immediate
            alu_op = 4'b0000; // ADD: compute rs1 + imm
            // I-type immediate
            imm = {{20{instr[31]}}, instr[31:20]};
        end
        
        OP_LUI: begin  // LUI: rd = imm << 12
            reg_write = 1;
            lui = 1;
            // U-type immediate: upper 20 bits, lower 12 bits are 0
            imm = {instr[31:12], 12'b0};
        end
        
        OP_AUIPC: begin  // AUIPC: rd = PC + (imm << 12)
            reg_write = 1;
            auipc = 1;
            // U-type immediate: upper 20 bits, lower 12 bits are 0
            imm = {instr[31:12], 12'b0};
        end
        
        OP_SYSTEM: begin  // ECALL, EBREAK
            // EBREAK: instr = 0x00100073 (imm[11:0] = 0x001)
            // ECALL:  instr = 0x00000073 (imm[11:0] = 0x000)
            if (instr[20]) begin  // imm[0] = 1 means EBREAK
                ebreak = 1;
            end
            // ECALL not implemented - treated as NOP
        end
        
        default: begin
            // NOP or unknown - do nothing
        end
    endcase
end

endmodule
