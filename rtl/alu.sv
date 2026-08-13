// ALU - Arithmetic Logic Unit
// ============================================================================
// The "calculator" of the CPU
// Takes two 32-bit inputs, performs operation based on 'op'
// Supports RV32I base + M extension (multiply/divide)
//
// NOTE: Multiply and divide operations use external pipelined units.
// This ALU detects these ops and expects results from external modules.
// ============================================================================

module alu (
    input  logic [31:0] a,       // first operand
    input  logic [31:0] b,       // second operand
    input  logic [4:0]  op,      // which operation (5 bits for M extension)
    output logic [31:0] result,  // answer
    output logic        zero,    // is result zero? (for BEQ, BNE)
    output logic        lt,      // a < b signed? (for BLT, BGE)
    output logic        ltu,     // a < b unsigned? (for BLTU, BGEU)

    // Multiplier interface
    output logic        is_mul_op,     // This is a multiply operation
    input  logic [31:0] mul_result,    // From external multiplier

    // Divider interface
    output logic        is_div_op,     // This is a divide operation
    input  logic [31:0] div_quotient,  // From external divider
    input  logic [31:0] div_remainder  // From external divider
);

// Operation codes - RV32I base
localparam OP_ADD  = 5'b00000;
localparam OP_SUB  = 5'b00001;
localparam OP_AND  = 5'b00010;
localparam OP_OR   = 5'b00011;
localparam OP_XOR  = 5'b00100;
localparam OP_SLL  = 5'b00101;  // Shift left logical
localparam OP_SRL  = 5'b00110;  // Shift right logical
localparam OP_SRA  = 5'b00111;  // Shift right arithmetic
localparam OP_SLT  = 5'b01000;  // Set less than (signed)
localparam OP_SLTU = 5'b01001;  // Set less than (unsigned)

// Operation codes - M extension (multiply/divide)
localparam OP_MUL    = 5'b10000;  // Multiply low (signed×signed)
localparam OP_MULH   = 5'b10001;  // Multiply high (signed×signed)
localparam OP_MULHSU = 5'b10010;  // Multiply high (signed×unsigned)
localparam OP_MULHU  = 5'b10011;  // Multiply high (unsigned×unsigned)
localparam OP_DIV    = 5'b10100;  // Divide (signed)
localparam OP_DIVU   = 5'b10101;  // Divide (unsigned)
localparam OP_REM    = 5'b10110;  // Remainder (signed)
localparam OP_REMU   = 5'b10111;  // Remainder (unsigned)

// Shift amount is bottom 5 bits of b (0-31)
wire [4:0] shamt = b[4:0];

// Detect multiply operations
assign is_mul_op = (op == OP_MUL) || (op == OP_MULH) ||
                   (op == OP_MULHSU) || (op == OP_MULHU);

// Detect divide operations
assign is_div_op = (op == OP_DIV) || (op == OP_DIVU) ||
                   (op == OP_REM) || (op == OP_REMU);

always_comb begin
    case (op)
        // RV32I base
        OP_ADD:  result = a + b;
        OP_SUB:  result = a - b;
        OP_AND:  result = a & b;
        OP_OR:   result = a | b;
        OP_XOR:  result = a ^ b;
        OP_SLL:  result = a << shamt;
        OP_SRL:  result = a >> shamt;
        OP_SRA:  result = $signed(a) >>> shamt;
        OP_SLT:  result = {31'b0, lt};
        OP_SLTU: result = {31'b0, ltu};

        // M extension - Multiply (from external pipelined multiplier)
        OP_MUL:    result = mul_result;
        OP_MULH:   result = mul_result;
        OP_MULHSU: result = mul_result;
        OP_MULHU:  result = mul_result;

        // M extension - Divide (from external multi-cycle divider)
        OP_DIV:  result = div_quotient;
        OP_DIVU: result = div_quotient;
        OP_REM:  result = div_remainder;
        OP_REMU: result = div_remainder;

        default: result = 0;
    endcase
end

// Comparison flags - used for branch instructions
assign zero = (result == 0);                        // BEQ, BNE
assign lt   = ($signed(a) < $signed(b));            // BLT, BGE, SLT
assign ltu  = (a < b);                              // BLTU, BGEU, SLTU

endmodule
