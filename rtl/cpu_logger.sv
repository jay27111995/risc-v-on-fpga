// CPU Memory Access Logger
// ============================================================================
//
// Logs CPU memory accesses for debugging:
// - IMEM reads (instruction fetches) - optional, controlled by log_imem
// - DMEM writes (store instructions)
// - DMEM reads (load instructions)
//
// When the CPU halts (e.g., on EBREAK), logging stops automatically since
// no more memory transactions occur.
//
// Control:
//   log_enable - when high, logging is active; when low, logging paused
//   log_clear  - pulse high to reset log pointer and count
//   log_imem   - when high, log instruction fetches; when low, DMEM only
//
// Log entry format (96 bits):
//   [95:64] - data      (32 bits - instruction, load data, or store data)
//   [63:32] - timestamp (32 bits - full cycle counter)
//   [31:20] - reserved  (12 bits)
//   [19:2]  - address   (18 bits - supports up to 256KB)
//   [1:0]   - type      (2 bits - 00=IFETCH, 01=DLOAD, 10=DSTORE)
//
// ============================================================================

module cpu_logger #(
    parameter LOG_DEPTH = 32   // Number of transactions to log (power of 2)
) (
    input  logic        clk,
    input  logic        rst_n,
    
    // Control
    input  logic        log_enable,    // Enable logging
    input  logic        log_clear,     // Clear log (pulse)
    input  logic        log_imem,      // Log IMEM fetches (0 = DMEM only)
    
    // IMEM fetch interface
    input  logic [31:0] imem_addr,
    input  logic [31:0] imem_rdata,
    input  logic        imem_valid,    // Fetch is happening
    
    // DMEM interface
    input  logic [31:0] dmem_addr,
    input  logic [31:0] dmem_wdata,
    input  logic [31:0] dmem_rdata,
    input  logic        dmem_wen,
    input  logic        dmem_ren,
    
    // Log read interface
    input  logic [4:0]  log_idx,       // Which log entry to read (0 = newest)
    output logic [95:0] log_entry,     // Log entry data
    output logic [31:0] log_count,     // Total transactions logged
    output logic [31:0] log_cycle      // Current cycle count
);

    // Transaction types
    localparam TYPE_IMEM_FETCH = 2'b00;
    localparam TYPE_DMEM_READ  = 2'b01;
    localparam TYPE_DMEM_WRITE = 2'b10;

    // =========================================================================
    // Cycle Counter
    // =========================================================================
    
    logic [31:0] cycle_cnt;
    
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            cycle_cnt <= '0;
        else
            cycle_cnt <= cycle_cnt + 1;
    end
    
    assign log_cycle = cycle_cnt;
    
    // =========================================================================
    // IMEM Interface - Register for timing
    // =========================================================================
    // Registering breaks the critical path from block RAM output.
    
    logic [31:0] imem_addr_r;
    logic [31:0] imem_rdata_r;
    logic        imem_valid_r;
    logic [31:0] imem_timestamp_r;
    
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            imem_addr_r <= '0;
            imem_rdata_r <= '0;
            imem_valid_r <= 1'b0;
            imem_timestamp_r <= '0;
        end else begin
            imem_addr_r <= imem_addr;
            imem_rdata_r <= imem_rdata;
            imem_valid_r <= imem_valid;
            imem_timestamp_r <= cycle_cnt;
        end
    end
    
    // IMEM fetch is loggable when valid and IMEM logging enabled
    wire imem_log_this = log_imem && imem_valid_r;
    
    // =========================================================================
    // Transaction Log
    // =========================================================================
    
    logic [95:0] log_mem [0:LOG_DEPTH-1];
    logic [$clog2(LOG_DEPTH)-1:0] log_wr_ptr;
    logic [31:0] trans_count;
    
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            log_wr_ptr <= '0;
            trans_count <= '0;
        end else if (log_clear) begin
            log_wr_ptr <= '0;
            trans_count <= '0;
        end else if (log_enable) begin
            // Priority: DMEM write > DMEM read > IMEM fetch
            if (dmem_wen) begin
                // Log DMEM write (store) - highest priority
                log_mem[log_wr_ptr] <= {
                    dmem_wdata,             // [95:64] - store data
                    cycle_cnt,              // [63:32] - timestamp (full 32-bit)
                    12'h0,                  // [31:20] - reserved
                    dmem_addr[19:2],        // [19:2]  - address (word-aligned, 18 bits)
                    TYPE_DMEM_WRITE         // [1:0]   - type
                };
                log_wr_ptr <= log_wr_ptr + 1;
                trans_count <= trans_count + 1;
            end else if (dmem_ren) begin
                // Log DMEM read (load) - second priority
                log_mem[log_wr_ptr] <= {
                    dmem_rdata,             // [95:64] - load data
                    cycle_cnt,              // [63:32] - timestamp (full 32-bit)
                    12'h0,                  // [31:20] - reserved
                    dmem_addr[19:2],        // [19:2]  - address (word-aligned, 18 bits)
                    TYPE_DMEM_READ          // [1:0]   - type
                };
                log_wr_ptr <= log_wr_ptr + 1;
                trans_count <= trans_count + 1;
            end else if (imem_log_this) begin
                // Log IMEM fetch - lowest priority (uses registered data)
                log_mem[log_wr_ptr] <= {
                    imem_rdata_r,           // [95:64] - instruction
                    imem_timestamp_r,       // [63:32] - timestamp (from when fetch happened)
                    12'h0,                  // [31:20] - reserved
                    imem_addr_r[19:2],      // [19:2]  - address (word-aligned, 18 bits)
                    TYPE_IMEM_FETCH         // [1:0]   - type
                };
                log_wr_ptr <= log_wr_ptr + 1;
                trans_count <= trans_count + 1;
            end
        end
    end
    
    assign log_count = trans_count;
    
    // Read log entry (0 = newest, 1 = second newest, etc.)
    wire [$clog2(LOG_DEPTH)-1:0] read_idx = log_wr_ptr - 1 - log_idx[$clog2(LOG_DEPTH)-1:0];
    assign log_entry = log_mem[read_idx];

endmodule
