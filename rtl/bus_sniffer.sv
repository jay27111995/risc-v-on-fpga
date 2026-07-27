// Bus Sniffer - Logs all transactions passing through
// ============================================================================
//
// Passthrough module that captures transaction history for debug.
// Stores last N transactions in a circular buffer readable via separate port.
//
// Control:
//   log_enable - when high, logging is active; when low, logging paused
//   log_clear  - pulse high to reset log pointer and count
//
// Transaction log entry (128 bits):
//   [127:64] - data (write data or read data)
//   [63:48]  - address
//   [47:32]  - timestamp (lower 16 bits of cycle counter)
//   [31:1]   - reserved
//   [0]      - type: 0=read, 1=write
//
// ============================================================================

module bus_sniffer #(
    parameter LOG_DEPTH = 32   // Number of transactions to log (power of 2)
) (
    input  logic        clk,
    input  logic        rst_n,
    
    // Control
    input  logic        log_enable,    // Enable logging
    input  logic        log_clear,     // Clear log (pulse)
    
    // Upstream interface (from master)
    input  logic [15:0] in_addr,
    input  logic [31:0] in_wdata,
    input  logic        in_wen,
    input  logic        in_ren,
    output logic [31:0] out_rdata,
    
    // Downstream interface (to slave)
    output logic [15:0] out_addr,
    output logic [31:0] out_wdata,
    output logic        out_wen,
    output logic        out_ren,
    input  logic [31:0] in_rdata,
    
    // Log read interface
    input  logic [3:0]  log_idx,      // Which log entry to read (0 = newest)
    output logic [127:0] log_entry,   // Log entry data
    output logic [31:0] log_count,    // Total transactions logged
    output logic [31:0] log_cycle     // Current cycle count
);

    // =========================================================================
    // Passthrough - directly connect upstream to downstream
    // =========================================================================
    
    assign out_addr  = in_addr;
    assign out_wdata = in_wdata;
    assign out_wen   = in_wen;
    assign out_ren   = in_ren;
    assign out_rdata = in_rdata;
    
    // =========================================================================
    // Cycle Counter
    // =========================================================================
    
    logic [31:0] cycle_cnt;
    
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            cycle_cnt <= 32'h0;
        else
            cycle_cnt <= cycle_cnt + 1;
    end
    
    assign log_cycle = cycle_cnt;
    
    // =========================================================================
    // Transaction Log
    // =========================================================================
    
    // Circular buffer
    logic [127:0] log_mem [0:LOG_DEPTH-1];
    logic [$clog2(LOG_DEPTH)-1:0] log_wr_ptr;
    logic [31:0] trans_count;
    
    // Capture read data one cycle later
    logic        pending_read;
    logic [15:0] pending_addr;
    logic [15:0] pending_time;
    
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            log_wr_ptr <= '0;
            trans_count <= 32'h0;
            pending_read <= 1'b0;
            pending_addr <= 16'h0;
            pending_time <= 16'h0;
        end else if (log_clear) begin
            log_wr_ptr <= '0;
            trans_count <= 32'h0;
            pending_read <= 1'b0;
        end else begin
            // Track pending read (only when logging enabled)
            if (in_ren && log_enable) begin
                pending_read <= 1'b1;
                pending_addr <= in_addr;
                pending_time <= cycle_cnt[15:0];
            end
            
            // Log write immediately (only when logging enabled)
            if (in_wen && log_enable) begin
                log_mem[log_wr_ptr] <= {
                    in_wdata,               // [127:96] - write data (32-bit, upper zero)
                    32'h0,                  // [95:64]  - padding
                    in_addr,                // [63:48]  - address
                    cycle_cnt[15:0],        // [47:32]  - timestamp
                    31'h0,                  // [31:1]   - reserved
                    1'b1                    // [0]      - type=write
                };
                log_wr_ptr <= log_wr_ptr + 1;
                trans_count <= trans_count + 1;
            end
            
            // Log read when data returns (1 cycle after ren)
            if (pending_read) begin
                pending_read <= 1'b0;
                log_mem[log_wr_ptr] <= {
                    in_rdata,               // [127:96] - read data (32-bit, upper zero)
                    32'h0,                  // [95:64]  - padding
                    pending_addr,           // [63:48]  - address
                    pending_time,           // [47:32]  - timestamp
                    31'h0,                  // [31:1]   - reserved
                    1'b0                    // [0]      - type=read
                };
                log_wr_ptr <= log_wr_ptr + 1;
                trans_count <= trans_count + 1;
            end
        end
    end
    
    assign log_count = trans_count;
    
    // Read log entry (0 = newest, 1 = second newest, etc.)
    wire [$clog2(LOG_DEPTH)-1:0] read_idx = log_wr_ptr - 1 - log_idx;
    assign log_entry = log_mem[read_idx];

endmodule
