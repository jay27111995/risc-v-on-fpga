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
// Log Entry Format (128 bits):
//   [127:96] - data      (32 bits)
//   [95:32]  - timestamp (64 bits - cycle count, wraps every ~2339 years @ 250MHz)
//   [31:20]  - reserved  (12 bits)
//   [19:1]   - address   (19 bits - covers full BAR range)
//   [0]      - type      (0=read, 1=write)
//
// Pipeline: Entry is built in cycle N, written to memory in cycle N+1.
//           Timestamp captures the actual event time (cycle N).
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
    input  logic [19:0] in_addr,
    input  logic [31:0] in_wdata,
    input  logic        in_wen,
    input  logic        in_ren,
    output logic [31:0] out_rdata,

    // Downstream interface (to slave)
    output logic [19:0] out_addr,
    output logic [31:0] out_wdata,
    output logic        out_wen,
    output logic        out_ren,
    input  logic [31:0] in_rdata,

    // Log read interface
    input  logic [3:0]  log_idx,       // Which log entry to read (0 = newest)
    output logic [127:0] log_entry,    // Log entry data
    output logic [31:0] log_count,     // Total transactions logged
    output logic [31:0] log_cycle      // Current cycle count (lower 32 bits)
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
    // Cycle Counter (64-bit)
    // =========================================================================

    logic [63:0] cycle_cnt;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            cycle_cnt <= 64'h0;
        else
            cycle_cnt <= cycle_cnt + 1;
    end

    assign log_cycle = cycle_cnt[31:0];

    // =========================================================================
    // Transaction Log (2-stage pipeline)
    // =========================================================================
    //
    // Stage 1: Capture event and build 128-bit entry
    // Stage 2: Write entry to memory
    //
    // This splits the timing path so concatenation and memory write
    // don't happen in the same cycle.

    // Circular buffer
    logic [127:0] log_mem [0:LOG_DEPTH-1];
    logic [$clog2(LOG_DEPTH)-1:0] log_wr_ptr;
    logic [31:0] trans_count;

    // Pipeline registers (Stage 1 → Stage 2)
    logic [127:0] entry_reg;
    logic         write_pending;

    // Read tracking (for capturing read data 1 cycle later)
    logic        pending_read;
    logic [19:0] pending_addr;
    logic [63:0] pending_time;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            log_wr_ptr <= '0;
            trans_count <= 32'h0;
            entry_reg <= 128'h0;
            write_pending <= 1'b0;
            pending_read <= 1'b0;
            pending_addr <= 20'h0;
            pending_time <= 64'h0;
        end else if (log_clear) begin
            log_wr_ptr <= '0;
            trans_count <= 32'h0;
            write_pending <= 1'b0;
            pending_read <= 1'b0;
        end else begin
            // Default: no write pending next cycle
            write_pending <= 1'b0;

            // ---- Stage 2: Write to memory (from previous cycle) ----
            if (write_pending) begin
                log_mem[log_wr_ptr] <= entry_reg;
                log_wr_ptr <= log_wr_ptr + 1;
                trans_count <= trans_count + 1;
            end

            // ---- Stage 1: Build entry ----

            // Track pending read request
            if (in_ren && log_enable) begin
                pending_read <= 1'b1;
                pending_addr <= in_addr;
                pending_time <= cycle_cnt;
            end

            // Write transaction: capture immediately
            if (in_wen && log_enable) begin
                entry_reg <= {
                    in_wdata,               // [127:96] - write data
                    cycle_cnt,              // [95:32]  - timestamp
                    12'h0,                  // [31:20]  - reserved
                    in_addr[19:1],          // [19:1]   - address
                    1'b1                    // [0]      - type=write
                };
                write_pending <= 1'b1;
            end

            // Read transaction: capture when data returns (1 cycle after ren)
            if (pending_read) begin
                pending_read <= 1'b0;
                entry_reg <= {
                    in_rdata,               // [127:96] - read data
                    pending_time,           // [95:32]  - timestamp (from request time)
                    12'h0,                  // [31:20]  - reserved
                    pending_addr[19:1],     // [19:1]   - address
                    1'b0                    // [0]      - type=read
                };
                write_pending <= 1'b1;
            end
        end
    end

    assign log_count = trans_count;

    // Read log entry (0 = newest, 1 = second newest, etc.)
    wire [$clog2(LOG_DEPTH)-1:0] read_idx = log_wr_ptr - 1 - log_idx;
    assign log_entry = log_mem[read_idx];

endmodule
