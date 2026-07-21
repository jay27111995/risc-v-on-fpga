// Instruction Memory (ROM)
// Holds the program - addressed by PC
// PC gives byte address, we return 32-bit instruction
module imem (
    input  logic [31:0] addr,       // address from PC
    output logic [31:0] instr       // instruction output
);

// 64 words of instruction memory (256 bytes)
logic [31:0] mem [0:63];

// Word-aligned access: addr[7:2] gives word index (6 bits for 64 entries)
// (divide by 4, since each instruction is 4 bytes)
assign instr = mem[addr[7:2]];

// Load a test program
initial begin
    // Comprehensive test: R-type, I-type, LW, SW, BEQ
    //
    // Test 1: I-type (ADDI)
    mem[0] = 32'h00500093;  // ADDI x1, x0, 5     x1 = 5
    mem[1] = 32'h00300113;  // ADDI x2, x0, 3     x2 = 3
    
    // Test 2: R-type (ADD, SUB)
    mem[2] = 32'h002081b3;  // ADD  x3, x1, x2    x3 = 5 + 3 = 8
    mem[3] = 32'h40208233;  // SUB  x4, x1, x2    x4 = 5 - 3 = 2
    
    // Test 3: Store and Load
    mem[4] = 32'h00302023;  // SW   x3, 0(x0)     mem[0] = 8
    mem[5] = 32'h00002283;  // LW   x5, 0(x0)     x5 = mem[0] = 8
    
    // Test 4: Load from pre-initialized memory
    mem[6] = 32'h00402303;  // LW   x6, 4(x0)     x6 = mem[4] = 200
    
    // Loop forever
    mem[7] = 32'h00000063;  // BEQ  x0, x0, 0     loop
    
    // Fill rest with NOPs
    for (int i = 8; i < 64; i++)
        mem[i] = 32'h00000013;  // NOP (ADDI x0, x0, 0)
end

endmodule
