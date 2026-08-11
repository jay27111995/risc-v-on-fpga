// CPU Logger - Logs CPU memory accesses
// ============================================================================
//
// Captures CPU memory transactions for debug:
// - Instruction fetches (IMEM reads) - optional, controlled by log_imem
// - Data loads (DMEM reads)
// - Data stores (DMEM writes)
//
// Logging stops naturally when CPU halts since no more transactions occur.
//
// Log Entry Format (128 bits):
//   [127:96] - data      (32 bits)
//   [95:32]  - timestamp (64 bits - cycle count, wraps every ~2339 years @ 250MHz)
//   [31:20]  - reserved  (12 bits)
//   [19:2]   - address   (18 bits - word-aligned, covers 256KB)
//   [1:0]    - type      (00=IFETCH, 01=DLOAD, 10=DSTORE)
//
// Pipeline: Entry is built in cycle N, written to memory in cycle N+1.
//           Timestamp captures the actual event time (cycle N).
//
// Control Register:
//   [0] = log_enable (default 1)
//   [1] = log_clear (write 1 to clear, auto-clears)
//   [2] = log_imem (default 0 = DMEM only)
//
// ============================================================================

module cpu_logger #(
    parameter LOG_DEPTH = 256  // Number of transactions to log (power of 2)
) (
    input  logic        clk,
    input  logic        rst_n,

    // Control
    input  logic        log_enable,    // Enable logging
    input  logic        log_clear,     // Clear log (pulse)
    input  logic        log_imem,      // Log IMEM fetches (0 = DMEM only)

    // IMEM interface (directly from CPU - combinational read, valid same cycle)
    input  logic [31:0] imem_addr,
    input  logic [31:0] imem_rdata,
    input  logic        imem_valid,    // High when instruction fetch is valid

    // DMEM interface (directly from CPU)
    input  logic [31:0] dmem_addr,
    input  logic [31:0] dmem_wdata,
    input  logic [31:0] dmem_rdata,
    input  logic        dmem_wen,
    input  logic        dmem_ren,

    // Log read interface
    input  logic [7:0]  log_idx,       // Which log entry to read (0 = newest)
    output logic [127:0] log_entry,    // Log entry data
    output logic [31:0] log_count,     // Total transactions logged
    output logic [31:0] log_cycle      // Current cycle count (lower 32 bits)
);

    // =========================================================================
    // Transaction Types
    // =========================================================================

    localparam TYPE_IFETCH = 2'b00;
    localparam TYPE_DLOAD  = 2'b01;
    localparam TYPE_DSTORE = 2'b10;

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

    // Circular buffer - initialize to zero on clear for clean reads
    logic [127:0] log_mem [0:LOG_DEPTH-1];
    logic [$clog2(LOG_DEPTH)-1:0] log_wr_ptr;
    logic [31:0] trans_count;

    // Pipeline registers (Stage 1 → Stage 2)
    logic [127:0] entry_reg;
    logic         write_pending;

    // Priority for simultaneous events: DSTORE > DLOAD > IFETCH
    // (In practice, DMEM and IMEM accesses don't overlap in our simple pipeline)
    wire log_dstore = dmem_wen && log_enable;
    wire log_dload  = dmem_ren && log_enable && !dmem_wen;
    wire log_ifetch = imem_valid && log_enable && log_imem && !dmem_wen && !dmem_ren;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            log_wr_ptr <= '0;
            trans_count <= 32'h0;
            entry_reg <= 128'h0;
            write_pending <= 1'b0;
        end else if (log_clear) begin
            log_wr_ptr <= '0;
            trans_count <= 32'h0;
            write_pending <= 1'b0;
            // Note: We don't zero log_mem here to save logic.
            // Reads beyond trans_count return stale data (acceptable for debug).
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
            // IMEM and DMEM data are valid in the SAME cycle as the request.
            // Capture immediately, write to memory next cycle.

            if (log_dstore) begin
                // DMEM store: capture write data
                entry_reg <= {
                    dmem_wdata,             // [127:96] - write data
                    cycle_cnt,              // [95:32]  - timestamp
                    12'h0,                  // [31:20]  - reserved
                    dmem_addr[19:2],        // [19:2]   - address (word)
                    TYPE_DSTORE             // [1:0]    - type
                };
                write_pending <= 1'b1;
            end else if (log_dload) begin
                // DMEM load: capture read data
                entry_reg <= {
                    dmem_rdata,             // [127:96] - read data
                    cycle_cnt,              // [95:32]  - timestamp
                    12'h0,                  // [31:20]  - reserved
                    dmem_addr[19:2],        // [19:2]   - address (word)
                    TYPE_DLOAD              // [1:0]    - type
                };
                write_pending <= 1'b1;
            end else if (log_ifetch) begin
                // IMEM fetch: capture instruction (data valid same cycle as valid signal)
                entry_reg <= {
                    imem_rdata,             // [127:96] - instruction
                    cycle_cnt,              // [95:32]  - timestamp
                    12'h0,                  // [31:20]  - reserved
                    imem_addr[19:2],        // [19:2]   - address (word)
                    TYPE_IFETCH             // [1:0]    - type
                };
                write_pending <= 1'b1;
            end
        end
    end

    assign log_count = trans_count;

    // Read log entry (0 = newest, 1 = second newest, etc.)
    wire [$clog2(LOG_DEPTH)-1:0] read_idx = log_wr_ptr - 1 - log_idx[$clog2(LOG_DEPTH)-1:0];
    assign log_entry = log_mem[read_idx];

endmodule
