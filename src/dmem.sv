// Data Memory (RAM)
// Stores variables, stack, heap - read and write
//
// Unlike IMEM (read-only), DMEM supports both LW and SW
//
// LW (load):  rdata = memory[addr]
// SW (store): memory[addr] = wdata
//
module dmem (
    input  logic        clk,
    input  logic [31:0] addr,      // address from ALU (rs1 + imm)
    input  logic [31:0] wdata,     // data to write (from rs2)
    input  logic        we,        // write enable (mem_write from decoder)
    input  logic        re,        // read enable (mem_read from decoder)
    output logic [31:0] rdata      // data read out (goes to rd)
);

// 256 words of data memory (1KB)
logic [31:0] mem [0:255];

// Word-aligned access: addr[9:2] gives word index
// (divide by 4, since each word is 4 bytes)
logic [7:0] word_addr;
assign word_addr = addr[9:2];

// Write (sequential) - happens on clock edge
always_ff @(posedge clk) begin
    if (we)
        mem[word_addr] <= wdata;
end

// Read (combinational) - instant
assign rdata = re ? mem[word_addr] : 32'b0;

// Initialize with some test data
initial begin
    for (int i = 0; i < 256; i++)
        mem[i] = 32'b0;
    
    // Put some values for testing
    mem[0] = 32'd100;    // address 0x00: value 100
    mem[1] = 32'd200;    // address 0x04: value 200
    mem[2] = 32'd300;    // address 0x08: value 300
end

endmodule
