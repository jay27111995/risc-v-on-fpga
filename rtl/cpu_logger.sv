// CPU Memory Access Logger
// ============================================================================
//
// Logs CPU memory accesses:
// - IMEM reads (instruction fetches)
// - DMEM writes (store instructions)
// - DMEM reads (load instructions)
//
// Log entry format (96 bits):
//   [95:64] - data (instruction, load data, or store data)
//   [63:32] - address
//   [31:16] - timestamp (lower 16 bits of cycle counter)
//   [15:2]  - reserved
//   [1:0]   - type: 00=IMEM fetch, 01=DMEM read, 10=DMEM write
//
// ============================================================================

module cpu_logger #(
    parameter LOG_DEPTH = 32   // Number of transactions to log (power of 2)
) (
    input  logic        clk,
    input  logic        rst_n,
    
    // IMEM fetch interface (directly connects)
    input  logic [31:0] imem_addr,
    input  logic [31:0] imem_rdata,
    input  logic        imem_valid,    // Fetch is happening (e.g., cpu_running)
    
    // DMEM interface (directly connects)
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
            cycle_cnt <= 32'h0;
        else
            cycle_cnt <= cycle_cnt + 1;
    end
    
    assign log_cycle = cycle_cnt;
    
    // =========================================================================
    // Transaction Log
    // =========================================================================
    
    logic [95:0] log_mem [0:LOG_DEPTH-1];
    logic [$clog2(LOG_DEPTH)-1:0] log_wr_ptr;
    logic [31:0] trans_count;
    
    // Track previous fetch address to avoid logging same fetch repeatedly
    logic [31:0] prev_imem_addr;
    logic        prev_imem_valid;
    
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            log_wr_ptr <= '0;
            trans_count <= 32'h0;
            prev_imem_addr <= 32'hFFFFFFFF;
            prev_imem_valid <= 1'b0;
        end else begin
            prev_imem_valid <= imem_valid;
            
            // Log IMEM fetch (only when address changes or first valid)
            if (imem_valid && (imem_addr != prev_imem_addr || !prev_imem_valid)) begin
                prev_imem_addr <= imem_addr;
                log_mem[log_wr_ptr] <= {
                    imem_rdata,             // [95:64] - instruction
                    imem_addr,              // [63:32] - PC
                    cycle_cnt[15:0],        // [31:16] - timestamp
                    14'h0,                  // [15:2]  - reserved
                    TYPE_IMEM_FETCH         // [1:0]   - type
                };
                log_wr_ptr <= log_wr_ptr + 1;
                trans_count <= trans_count + 1;
            end
            
            // Log DMEM write (store)
            if (dmem_wen) begin
                log_mem[log_wr_ptr] <= {
                    dmem_wdata,             // [95:64] - store data
                    dmem_addr,              // [63:32] - address
                    cycle_cnt[15:0],        // [31:16] - timestamp
                    14'h0,                  // [15:2]  - reserved
                    TYPE_DMEM_WRITE         // [1:0]   - type
                };
                log_wr_ptr <= log_wr_ptr + 1;
                trans_count <= trans_count + 1;
            end
            
            // Log DMEM read (load)
            if (dmem_ren) begin
                log_mem[log_wr_ptr] <= {
                    dmem_rdata,             // [95:64] - load data
                    dmem_addr,              // [63:32] - address
                    cycle_cnt[15:0],        // [31:16] - timestamp
                    14'h0,                  // [15:2]  - reserved
                    TYPE_DMEM_READ          // [1:0]   - type
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
