// 64-bit to 32-bit Bus Adapter
// ============================================================================
//
// Converts 64-bit bus transactions to 32-bit:
// - Write: splits 64-bit write into two sequential 32-bit writes
// - Read: combines two sequential 32-bit reads into 64-bit response
//
// Assumes downstream memory has 1-cycle read latency (registered output).
//
// Active-high handshake:
// - req: host asserts to start transaction
// - done: adapter asserts when transaction complete
//
// ============================================================================

module bus64to32 (
    input  logic        clk,
    input  logic        rst_n,

    // 64-bit side (upstream)
    input  logic [19:0] in_addr,
    input  logic [63:0] in_wdata,
    input  logic        in_wen,
    input  logic        in_ren,
    output logic [63:0] out_rdata,
    output logic        out_done,

    // 32-bit side (downstream)
    output logic [19:0] out_addr,
    output logic [31:0] out_wdata,
    output logic        out_wen,
    output logic        out_ren,
    input  logic [31:0] in_rdata
);

    // =========================================================================
    // State Machine
    // =========================================================================
    //
    // Write: IDLE -> W_LO -> W_HI -> DONE
    // Read:  IDLE -> R_LO -> R_LO_CAP -> R_HI -> R_HI_CAP -> DONE
    //
    // R_LO_CAP and R_HI_CAP account for 1-cycle memory read latency.

    typedef enum logic [2:0] {
        IDLE,
        W_LO,       // Write lower 32 bits
        W_HI,       // Write upper 32 bits
        R_LO,       // Read request for lower 32 bits
        R_LO_CAP,   // Capture lower 32 bits (memory has 1-cycle latency)
        R_HI,       // Read request for upper 32 bits
        R_HI_CAP,   // Capture upper 32 bits
        DONE
    } state_t;

    state_t state, state_next;

    // Registered request
    logic [19:0] addr_reg;
    logic [63:0] wdata_reg;
    logic [31:0] rdata_lo;
    logic [31:0] rdata_hi;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= IDLE;
            addr_reg <= 20'h0;
            wdata_reg <= 64'h0;
            rdata_lo <= 32'h0;
            rdata_hi <= 32'h0;
        end else begin
            state <= state_next;

            // Capture request on IDLE
            if (state == IDLE && (in_wen || in_ren)) begin
                addr_reg <= in_addr;
                wdata_reg <= in_wdata;
            end

            // Capture lower read data (after 1-cycle latency)
            if (state == R_LO_CAP) begin
                rdata_lo <= in_rdata;
            end

            // Capture upper read data (after 1-cycle latency)
            if (state == R_HI_CAP) begin
                rdata_hi <= in_rdata;
            end
        end
    end

    // Next state logic
    always_comb begin
        state_next = state;
        case (state)
            IDLE:     if (in_wen) state_next = W_LO;
                      else if (in_ren) state_next = R_LO;
            W_LO:     state_next = W_HI;
            W_HI:     state_next = DONE;
            R_LO:     state_next = R_LO_CAP;  // Wait for read data
            R_LO_CAP: state_next = R_HI;
            R_HI:     state_next = R_HI_CAP;  // Wait for read data
            R_HI_CAP: state_next = DONE;
            DONE:     state_next = IDLE;
        endcase
    end

    // =========================================================================
    // Output Logic
    // =========================================================================

    // Done signal
    assign out_done = (state == DONE);

    // Read data: combine captured lower and upper
    assign out_rdata = {rdata_hi, rdata_lo};

    // Downstream address, data, and enables
    always_comb begin
        out_addr = 20'h0;
        out_wdata = 32'h0;
        out_wen = 1'b0;
        out_ren = 1'b0;

        case (state)
            W_LO: begin
                out_addr = addr_reg;
                out_wdata = wdata_reg[31:0];
                out_wen = 1'b1;
            end
            W_HI: begin
                out_addr = addr_reg + 20'd4;
                out_wdata = wdata_reg[63:32];
                out_wen = 1'b1;
            end
            R_LO: begin
                out_addr = addr_reg;
                out_ren = 1'b1;
            end
            R_LO_CAP: begin
                out_addr = addr_reg;  // Hold address for read mux
            end
            R_HI: begin
                out_addr = addr_reg + 20'd4;
                out_ren = 1'b1;
            end
            R_HI_CAP: begin
                out_addr = addr_reg + 20'd4;  // Hold address for read mux
            end
            default: ;
        endcase
    end

endmodule
