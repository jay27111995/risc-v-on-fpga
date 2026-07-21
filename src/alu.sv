// ALU - Arithmetic Logic Unit
// The "calculator" of the CPU
// Takes two 32-bit inputs, performs operation based on 'op'
module alu (
    input  logic [31:0] a,       // first operand
    input  logic [31:0] b,       // second operand
    input  logic [2:0]  op,      // which operation
    output logic [31:0] result,  // answer
    output logic        zero     // is result zero? (for branches)
);

// Operation codes
localparam OP_ADD = 3'b000;
localparam OP_SUB = 3'b001;
localparam OP_AND = 3'b010;
localparam OP_OR  = 3'b011;
localparam OP_XOR = 3'b100;

always_comb begin
    case (op)
        OP_ADD: result = a + b;
        OP_SUB: result = a - b;
        OP_AND: result = a & b;
        OP_OR:  result = a | b;
        OP_XOR: result = a ^ b;
        default: result = 0;
    endcase
end

// Zero flag - used for branch instructions (BEQ, BNE)
assign zero = (result == 0);

endmodule
