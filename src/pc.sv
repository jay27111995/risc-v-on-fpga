// Program Counter
// Points to current instruction address
// Increments by 4 each cycle, or jumps to new address
module pc (
    input  logic        clk,
    input  logic        rst,
    input  logic        branch,      // take branch?
    input  logic [31:0] branch_addr, // where to branch
    output logic [31:0] pc_out       // current PC value
);

always_ff @(posedge clk or posedge rst) begin
    if (rst)
        pc_out <= 32'b0;             // start at address 0
    else if (branch)
        pc_out <= branch_addr;       // jump to branch target
    else
        pc_out <= pc_out + 4;        // next instruction
end

endmodule
