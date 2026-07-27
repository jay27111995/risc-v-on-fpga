// BAR Interface Adapter
// ============================================================================
//
// Converts 64-bit AXI-Lite transactions to appropriate SoC accesses:
// - IMEM (0x1000-0x1FFF): 32-bit, needs 64→32 split on write, 32→64 combine on read
// - DMEM (0x2000-0x3FFF): 64-bit native, pass through directly
// - Control (0x0000-0x0FFF): 64-bit native, pass through directly
//
// Write path (64→32 for IMEM):
//   Cycle 1: Write lower 32 bits to addr
//   Cycle 2: Write upper 32 bits to addr+4
//
// Read path (32→64 for IMEM):
//   Cycle 1: Read from addr, capture in lower 32
//   Cycle 2: Read from addr+4, capture in upper 32
//   Return combined 64-bit
//
// ============================================================================

module bar_adapter (
    input  logic        clk,
    input  logic        rst_n,
    
    // AXI-side interface (64-bit)
    input  logic [15:0] axi_addr,
    input  logic [63:0] axi_wdata,
    input  logic        axi_wen,       // Write request (active for 1 cycle)
    input  logic        axi_ren,       // Read request (active for 1 cycle)
    output logic [63:0] axi_rdata,
    output logic        axi_wdone,     // Write complete
    output logic        axi_rdone,     // Read complete, data valid
    
    // SoC-side interface (directly to riscv_soc)
    output logic [15:0] soc_addr,
    output logic [63:0] soc_wdata,
    output logic        soc_wen,
    output logic        soc_ren,
    input  logic [63:0] soc_rdata
);

    // Address range detection
    wire is_imem = (axi_addr[15:12] == 4'h1);  // 0x1000-0x1FFF
    // DMEM and control are 64-bit native, no conversion needed
    
    // =========================================================================
    // Write State Machine
    // =========================================================================
    
    typedef enum logic [1:0] {
        W_IDLE,
        W_EVEN,     // Writing lower 32 bits (IMEM only)
        W_ODD,      // Writing upper 32 bits (IMEM only)
        W_DONE
    } w_state_t;
    
    w_state_t w_state, w_state_next;
    
    // Captured write data
    logic [15:0] w_addr_reg;
    logic [63:0] w_data_reg;
    
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            w_state <= W_IDLE;
            w_addr_reg <= 16'h0;
            w_data_reg <= 64'h0;
        end else begin
            w_state <= w_state_next;
            if (axi_wen && w_state == W_IDLE) begin
                w_addr_reg <= axi_addr;
                w_data_reg <= axi_wdata;
            end
        end
    end
    
    always_comb begin
        w_state_next = w_state;
        case (w_state)
            W_IDLE: if (axi_wen) w_state_next = is_imem ? W_EVEN : W_DONE;
            W_EVEN: w_state_next = W_ODD;
            W_ODD:  w_state_next = W_DONE;
            W_DONE: w_state_next = W_IDLE;
        endcase
    end
    
    // Write outputs
    assign axi_wdone = (w_state == W_DONE);
    
    // =========================================================================
    // Read State Machine
    // =========================================================================
    
    typedef enum logic [1:0] {
        R_IDLE,
        R_EVEN,     // Reading lower 32 bits (IMEM only)
        R_ODD,      // Reading upper 32 bits (IMEM only)
        R_DONE
    } r_state_t;
    
    r_state_t r_state, r_state_next;
    
    // Captured read address and data
    logic [15:0] r_addr_reg;
    logic [63:0] r_data_reg;
    logic        r_is_imem;
    
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            r_state <= R_IDLE;
            r_addr_reg <= 16'h0;
            r_data_reg <= 64'h0;
            r_is_imem <= 1'b0;
        end else begin
            r_state <= r_state_next;
            if (axi_ren && r_state == R_IDLE) begin
                r_addr_reg <= axi_addr;
                r_is_imem <= is_imem;
            end
            // Capture read data
            if (r_state == R_EVEN) begin
                r_data_reg[31:0] <= soc_rdata[31:0];
            end
            if (r_state == R_ODD) begin
                r_data_reg[63:32] <= soc_rdata[31:0];
            end
            // For non-IMEM, capture full 64-bit
            if (r_state == R_IDLE && axi_ren && !is_imem) begin
                // Will capture on next cycle when R_DONE
            end
            if (r_state == R_DONE && !r_is_imem) begin
                r_data_reg <= soc_rdata;
            end
        end
    end
    
    always_comb begin
        r_state_next = r_state;
        case (r_state)
            R_IDLE: if (axi_ren) r_state_next = r_is_imem ? R_EVEN : R_DONE;
            R_EVEN: r_state_next = R_ODD;
            R_ODD:  r_state_next = R_DONE;
            R_DONE: r_state_next = R_IDLE;
        endcase
    end
    
    // Fix: need to check is_imem at request time, not continuously
    always_comb begin
        r_state_next = r_state;
        case (r_state)
            R_IDLE: if (axi_ren) r_state_next = is_imem ? R_EVEN : R_DONE;
            R_EVEN: r_state_next = R_ODD;
            R_ODD:  r_state_next = R_DONE;
            R_DONE: r_state_next = R_IDLE;
        endcase
    end
    
    // Read outputs  
    assign axi_rdone = (r_state == R_DONE);
    assign axi_rdata = r_data_reg;
    
    // =========================================================================
    // SoC Interface Mux
    // =========================================================================
    
    always_comb begin
        soc_addr = 16'h0;
        soc_wdata = 64'h0;
        soc_wen = 1'b0;
        soc_ren = 1'b0;
        
        // Write path
        case (w_state)
            W_EVEN: begin
                soc_addr = w_addr_reg;
                soc_wdata = {32'h0, w_data_reg[31:0]};
                soc_wen = 1'b1;
            end
            W_ODD: begin
                soc_addr = w_addr_reg + 16'd4;
                soc_wdata = {32'h0, w_data_reg[63:32]};
                soc_wen = 1'b1;
            end
            W_DONE: begin
                // Non-IMEM write (64-bit pass-through) happens here
                if (!is_imem) begin
                    soc_addr = w_addr_reg;
                    soc_wdata = w_data_reg;
                    soc_wen = 1'b1;
                end
            end
            default: ;
        endcase
        
        // Read path (priority over write if both active)
        case (r_state)
            R_IDLE: begin
                if (axi_ren) begin
                    soc_addr = axi_addr;
                    soc_ren = 1'b1;
                end
            end
            R_EVEN: begin
                soc_addr = r_addr_reg;
                soc_ren = 1'b1;
            end
            R_ODD: begin
                soc_addr = r_addr_reg + 16'd4;
                soc_ren = 1'b1;
            end
            default: ;
        endcase
    end

endmodule
