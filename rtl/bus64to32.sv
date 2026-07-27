// 64-bit to 32-bit Bus Adapter
// ============================================================================
//
// Converts 64-bit bus transactions to 32-bit:
// - Write: splits 64-bit write into two sequential 32-bit writes
// - Read: combines two sequential 32-bit reads into 64-bit response
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
    input  logic [15:0] in_addr,
    input  logic [63:0] in_wdata,
    input  logic        in_wen,
    input  logic        in_ren,
    output logic [63:0] out_rdata,
    output logic        out_done,
    
    // 32-bit side (downstream)
    output logic [15:0] out_addr,
    output logic [31:0] out_wdata,
    output logic        out_wen,
    output logic        out_ren,
    input  logic [31:0] in_rdata
);

    // =========================================================================
    // State Machine
    // =========================================================================
    
    typedef enum logic [2:0] {
        IDLE,
        W_LO,       // Write lower 32 bits
        W_HI,       // Write upper 32 bits
        R_LO,       // Read lower 32 bits
        R_HI,       // Read upper 32 bits
        DONE
    } state_t;
    
    state_t state, state_next;
    
    // Registered request
    logic [15:0] addr_reg;
    logic [63:0] wdata_reg;
    logic [31:0] rdata_lo;
    
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= IDLE;
            addr_reg <= 16'h0;
            wdata_reg <= 64'h0;
            rdata_lo <= 32'h0;
        end else begin
            state <= state_next;
            
            // Capture request
            if (state == IDLE && (in_wen || in_ren)) begin
                addr_reg <= in_addr;
                wdata_reg <= in_wdata;
            end
            
            // Capture lower read data
            if (state == R_LO) begin
                rdata_lo <= in_rdata;
            end
        end
    end
    
    // Next state logic
    always_comb begin
        state_next = state;
        case (state)
            IDLE:   if (in_wen) state_next = W_LO;
                    else if (in_ren) state_next = R_LO;
            W_LO:   state_next = W_HI;
            W_HI:   state_next = DONE;
            R_LO:   state_next = R_HI;
            R_HI:   state_next = DONE;
            DONE:   state_next = IDLE;
        endcase
    end
    
    // =========================================================================
    // Output Logic
    // =========================================================================
    
    // Done signal
    assign out_done = (state == DONE);
    
    // Read data: combine lower (captured) and upper (current)
    assign out_rdata = {in_rdata, rdata_lo};
    
    // Downstream address and data
    always_comb begin
        out_addr = 16'h0;
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
                out_addr = addr_reg + 16'd4;
                out_wdata = wdata_reg[63:32];
                out_wen = 1'b1;
            end
            R_LO: begin
                out_addr = addr_reg;
                out_ren = 1'b1;
            end
            R_HI: begin
                out_addr = addr_reg + 16'd4;
                out_ren = 1'b1;
            end
            default: ;
        endcase
    end

endmodule
