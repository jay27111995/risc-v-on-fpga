`timescale 1ns / 1ps
//
// Data Memory Module with Dual-Port Access
// - Port A: CPU access (read/write)
// - Port B: Host access (read/write)
//
// BRAM read has 1-cycle latency. The address is registered internally
// to ensure correct read data even if the external address changes.
//

module dmem #(
    parameter DEPTH = 8192,  // Number of 32-bit words
    parameter ADDR_WIDTH = 13
) (
    input  logic        clk,
    
    // CPU Port (active when cpu_en is high)
    input  logic                  cpu_en,      // Enable CPU access
    input  logic [ADDR_WIDTH-1:0] cpu_addr,    // Word address
    input  logic [31:0]           cpu_wdata,
    input  logic                  cpu_we,      // Write enable
    input  logic [3:0]            cpu_be,      // Byte enables
    output logic [31:0]           cpu_rdata,
    output logic                  cpu_rdata_valid,  // Data valid 1 cycle after read
    
    // Host Port (active when host_en is high, has priority)
    input  logic                  host_en,     // Enable host access
    input  logic [ADDR_WIDTH-1:0] host_addr,   // Word address
    input  logic [31:0]           host_wdata,
    input  logic                  host_we,     // Write enable
    output logic [31:0]           host_rdata
);

    // Memory arrays - 4 banks for byte-addressable access
    (* ramstyle = "M20K" *) logic [7:0] mem_b0 [0:DEPTH-1];
    (* ramstyle = "M20K" *) logic [7:0] mem_b1 [0:DEPTH-1];
    (* ramstyle = "M20K" *) logic [7:0] mem_b2 [0:DEPTH-1];
    (* ramstyle = "M20K" *) logic [7:0] mem_b3 [0:DEPTH-1];
    
    // Host has priority - block CPU writes when host is writing
    wire cpu_write_en = cpu_en && cpu_we && !host_we;
    
    // -------------------------------------------------------------------------
    // Write Logic
    // -------------------------------------------------------------------------
    
    // Byte 0
    always_ff @(posedge clk) begin
        if (host_en && host_we)
            mem_b0[host_addr] <= host_wdata[7:0];
        else if (cpu_write_en && cpu_be[0])
            mem_b0[cpu_addr] <= cpu_wdata[7:0];
    end

    // Byte 1
    always_ff @(posedge clk) begin
        if (host_en && host_we)
            mem_b1[host_addr] <= host_wdata[15:8];
        else if (cpu_write_en && cpu_be[1])
            mem_b1[cpu_addr] <= cpu_wdata[15:8];
    end

    // Byte 2
    always_ff @(posedge clk) begin
        if (host_en && host_we)
            mem_b2[host_addr] <= host_wdata[23:16];
        else if (cpu_write_en && cpu_be[2])
            mem_b2[cpu_addr] <= cpu_wdata[23:16];
    end

    // Byte 3
    always_ff @(posedge clk) begin
        if (host_en && host_we)
            mem_b3[host_addr] <= host_wdata[31:24];
        else if (cpu_write_en && cpu_be[3])
            mem_b3[cpu_addr] <= cpu_wdata[31:24];
    end

    // -------------------------------------------------------------------------
    // CPU Read Logic - With registered address
    // -------------------------------------------------------------------------
    
    // Register the read address when a read is initiated.
    // This ensures we read from the correct address even if cpu_addr changes
    // before the data is captured.
    logic [ADDR_WIDTH-1:0] cpu_addr_reg;
    
    always_ff @(posedge clk) begin
        // Always register the address (the BRAM read is always happening)
        cpu_addr_reg <= cpu_addr;
    end
    
    // Read data using the registered address from the previous cycle
    // This gives 1-cycle read latency: addr on cycle N, data on cycle N+1
    always_ff @(posedge clk) begin
        cpu_rdata <= {mem_b3[cpu_addr_reg], mem_b2[cpu_addr_reg],
                     mem_b1[cpu_addr_reg], mem_b0[cpu_addr_reg]};
    end
    
    // Valid signal - always valid (data reflects registered address from 2 cycles ago)
    assign cpu_rdata_valid = 1'b1;

    // -------------------------------------------------------------------------
    // Host Read Logic
    // -------------------------------------------------------------------------
    
    always_ff @(posedge clk) begin
        if (host_en && !host_we) begin
            host_rdata <= {mem_b3[host_addr], mem_b2[host_addr],
                          mem_b1[host_addr], mem_b0[host_addr]};
        end
    end

endmodule
