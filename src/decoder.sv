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
    output logic [2:0]  alu_op,      // ALU operation
    output logic        reg_write,   // write to register file?
    output logic        alu_src,     // ALU source: 0=rs2, 1=immediate
    output logic        mem_read,    // load from memory?
    output logic        mem_write,   // store to memory?
    output logic        branch       // branch instruction?
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

// Decode logic
always_comb begin
    // Defaults
    imm = 32'b0;
    alu_op = 3'b000;
    reg_write = 0;
    alu_src = 0;
    mem_read = 0;
    mem_write = 0;
    branch = 0;
    
    case (opcode)
        OP_RTYPE: begin  // R-type: ADD, SUB, AND, OR, XOR
            reg_write = 1;
            alu_src = 0;  // use rs2
            case (funct3)
                3'b000: alu_op = (funct7[5]) ? 3'b001 : 3'b000;  // SUB or ADD
                3'b111: alu_op = 3'b010;  // AND
                3'b110: alu_op = 3'b011;  // OR
                3'b100: alu_op = 3'b100;  // XOR
                default: alu_op = 3'b000;
            endcase
        end
        
        OP_ITYPE: begin  // I-type: ADDI, ANDI, ORI, XORI
            reg_write = 1;
            alu_src = 1;  // use immediate
            // I-type immediate: instr[31:20] = 12-bit signed immediate
            // Sign-extend to 32 bits by replicating bit 31
            imm = {{20{instr[31]}}, instr[31:20]};
            case (funct3)
                3'b000: alu_op = 3'b000;  // ADDI
                3'b111: alu_op = 3'b010;  // ANDI
                3'b110: alu_op = 3'b011;  // ORI
                3'b100: alu_op = 3'b100;  // XORI
                default: alu_op = 3'b000;
            endcase
        end
        
        OP_LOAD: begin  // LW: rd = memory[rs1 + imm]
            reg_write = 1;   // write loaded data to rd
            alu_src = 1;     // ALU uses immediate
            mem_read = 1;    // read from data memory
            alu_op = 3'b000; // ADD: address = rs1 + imm
            // I-type immediate (same as ADDI)
            imm = {{20{instr[31]}}, instr[31:20]};
        end
        
        OP_STORE: begin  // SW: memory[rs1 + imm] = rs2
            reg_write = 0;   // no register write
            alu_src = 1;     // ALU uses immediate
            mem_write = 1;   // write to data memory
            alu_op = 3'b000; // ADD: address = rs1 + imm
            // S-type immediate: split across instr[31:25] and instr[11:7]
            imm = {{20{instr[31]}}, instr[31:25], instr[11:7]};
        end
        
        OP_BRANCH: begin  // BEQ: if (rs1 == rs2) PC += imm
            branch = 1;
            alu_op = 3'b001; // SUB: compare rs1 - rs2, check zero flag
            // B-type immediate: imm[12|10:5|4:1|11], shifted left by 1 (2-byte aligned)
            imm = {{20{instr[31]}}, instr[7], instr[30:25], instr[11:8], 1'b0};
        end
        
        default: begin
            // NOP or unknown - do nothing
        end
    endcase
end

endmodule
