// Register File - 32 registers x 32 bits
// 2 read ports (combinational), 1 write port (sequential)
// x0 is hardwired to 0
// Includes write-first bypass: reading a register being written returns the new value
module regfile (
    input  logic        clk,
    input  logic        we,         // write enable
    input  logic [4:0]  rs1_addr,   // read port 1 address
    input  logic [4:0]  rs2_addr,   // read port 2 address
    input  logic [4:0]  rd_addr,    // write port address
    input  logic [31:0] rd_data,    // data to write
    output logic [31:0] rs1_data,   // read port 1 data
    output logic [31:0] rs2_data    // read port 2 data
);

// 32 registers, each 32 bits
logic [31:0] regs [0:31];

// Write (sequential) - but never write to x0
always_ff @(posedge clk) begin
    if (we && rd_addr != 0)
        regs[rd_addr] <= rd_data;
end

// Read (combinational) with write-first bypass
// If reading the same register being written, return the write data
// x0 always returns 0
assign rs1_data = (rs1_addr == 0) ? 32'b0 :
                  (we && rs1_addr == rd_addr) ? rd_data :
                  regs[rs1_addr];

assign rs2_data = (rs2_addr == 0) ? 32'b0 :
                  (we && rs2_addr == rd_addr) ? rd_data :
                  regs[rs2_addr];

endmodule
