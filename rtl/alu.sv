// ALU - Arithmetic Logic Unit
// The "calculator" of the CPU
// Takes two 32-bit inputs, performs operation based on 'op'
module alu (
    input  logic [31:0] a,       // first operand
    input  logic [31:0] b,       // second operand
    input  logic [3:0]  op,      // which operation (4 bits)
    output logic [31:0] result,  // answer
    output logic        zero,    // is result zero? (for BEQ, BNE)
    output logic        lt,      // a < b signed? (for BLT, BGE)
    output logic        ltu      // a < b unsigned? (for BLTU, BGEU)
);

// Operation codes
localparam OP_ADD  = 4'b0000;
localparam OP_SUB  = 4'b0001;
localparam OP_AND  = 4'b0010;
localparam OP_OR   = 4'b0011;
localparam OP_XOR  = 4'b0100;
localparam OP_SLL  = 4'b0101;  // Shift left logical
localparam OP_SRL  = 4'b0110;  // Shift right logical
localparam OP_SRA  = 4'b0111;  // Shift right arithmetic
localparam OP_SLT  = 4'b1000;  // Set less than (signed)
localparam OP_SLTU = 4'b1001;  // Set less than (unsigned)

// Shift amount is bottom 5 bits of b (0-31)
wire [4:0] shamt = b[4:0];

always_comb begin
    case (op)
        OP_ADD:  result = a + b;
        OP_SUB:  result = a - b;
        OP_AND:  result = a & b;
        OP_OR:   result = a | b;
        OP_XOR:  result = a ^ b;
        OP_SLL:  result = a << shamt;
        OP_SRL:  result = a >> shamt;
        OP_SRA:  result = $signed(a) >>> shamt;
        OP_SLT:  result = {31'b0, lt};   // 1 if a < b signed, else 0
        OP_SLTU: result = {31'b0, ltu};  // 1 if a < b unsigned, else 0
        default: result = 0;
    endcase
end

// Comparison flags - used for branch instructions
assign zero = (result == 0);                        // BEQ, BNE
assign lt   = ($signed(a) < $signed(b));            // BLT, BGE, SLT
assign ltu  = (a < b);                              // BLTU, BGEU, SLTU

endmodule
