// ALU - Arithmetic Logic Unit
// The "calculator" of the CPU
// Takes two 32-bit inputs, performs operation based on 'op'
// Supports RV32I base + M extension (multiply/divide)
module alu (
    input  logic [31:0] a,       // first operand
    input  logic [31:0] b,       // second operand
    input  logic [4:0]  op,      // which operation (5 bits for M extension)
    output logic [31:0] result,  // answer
    output logic        zero,    // is result zero? (for BEQ, BNE)
    output logic        lt,      // a < b signed? (for BLT, BGE)
    output logic        ltu      // a < b unsigned? (for BLTU, BGEU)
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

// Multiply results (64-bit)
wire signed [63:0] mul_ss = $signed(a) * $signed(b);           // signed × signed
wire signed [63:0] mul_su = $signed(a) * $signed({1'b0, b});   // signed × unsigned
wire        [63:0] mul_uu = a * b;                              // unsigned × unsigned

// Division results
// Handle division by zero: RISC-V spec says DIV/REM return specific values
wire div_by_zero = (b == 0);
wire signed [31:0] a_signed = $signed(a);
wire signed [31:0] b_signed = $signed(b);

// Signed division overflow: -2^31 / -1 = overflow (return -2^31)
wire div_overflow = (a == 32'h80000000) && (b == 32'hFFFFFFFF);

wire [31:0] div_s  = div_by_zero ? 32'hFFFFFFFF :
                     div_overflow ? 32'h80000000 :
                     $signed(a_signed / b_signed);
wire [31:0] div_u  = div_by_zero ? 32'hFFFFFFFF : a / b;
wire [31:0] rem_s  = div_by_zero ? a :
                     div_overflow ? 32'h0 :
                     $signed(a_signed % b_signed);
wire [31:0] rem_u  = div_by_zero ? a : a % b;

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

        // M extension - Multiply
        OP_MUL:    result = mul_ss[31:0];   // Lower 32 bits
        OP_MULH:   result = mul_ss[63:32];  // Upper 32 bits (signed×signed)
        OP_MULHSU: result = mul_su[63:32];  // Upper 32 bits (signed×unsigned)
        OP_MULHU:  result = mul_uu[63:32];  // Upper 32 bits (unsigned×unsigned)

        // M extension - Divide
        OP_DIV:  result = div_s;
        OP_DIVU: result = div_u;
        OP_REM:  result = rem_s;
        OP_REMU: result = rem_u;

        default: result = 0;
    endcase
end

// Comparison flags - used for branch instructions
assign zero = (result == 0);                        // BEQ, BNE
assign lt   = ($signed(a) < $signed(b));            // BLT, BGE, SLT
assign ltu  = (a < b);                              // BLTU, BGEU, SLTU

endmodule
