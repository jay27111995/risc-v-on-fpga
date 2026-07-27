`timescale 1 ps / 1 ps
`default_nettype none

// AXI Core Hardware Wrapper
// ============================================================================
//
// Bridges PCIe AXI-Lite (64-bit) to RISC-V SoC (32-bit).
// Uses bus64to32 adapter for clean 64-bit to 32-bit conversion.
//
// ============================================================================

module axi_core_hw(
    input  wire          clk,
    input  wire          cpu_clk,    // Unused - kept for compatibility
    input  wire          rst,

    // AXI Master (unused, directly tied off)
    output logic [3:0]    axm_m0_awid,
    output logic [63:0]   axm_m0_awaddr,
    output logic [7:0]    axm_m0_awlen,
    output wire [2:0]     axm_m0_awprot,
    output logic          axm_m0_awvalid,
    input  wire           axm_m0_awready,
    output logic [1023:0] axm_m0_wdata,
    output wire           axm_m0_wlast,
    output logic          axm_m0_wvalid,
    input  wire           axm_m0_wready,
    input  wire [3:0]     axm_m0_bid,
    input  wire           axm_m0_bvalid,
    output logic          axm_m0_bready,
    output logic [3:0]    axm_m0_arid,
    output logic [63:0]   axm_m0_araddr,
    output logic [7:0]    axm_m0_arlen,
    output wire [2:0]     axm_m0_arprot,
    output logic          axm_m0_arvalid,
    input  wire           axm_m0_arready,
    input  wire [3:0]     axm_m0_rid,
    input  wire [1023:0]  axm_m0_rdata,
    input  wire           axm_m0_rlast,
    input  wire           axm_m0_rvalid,
    output wire           axm_m0_rready,

    // AXI Lite Slave (from PCIe)
    input  wire [21:0]    axi_lite_s_awaddr,
    input  wire           axi_lite_s_awvalid,
    output logic          axi_lite_s_awready,
    input  wire [63:0]    axi_lite_s_wdata,
    input  wire [7:0]     axi_lite_s_wstrb,
    input  wire           axi_lite_s_wvalid,
    output logic          axi_lite_s_wready,
    output logic          axi_lite_s_bvalid,
    input  wire           axi_lite_s_bready,
    input  wire [21:0]    axi_lite_s_araddr,
    input  wire           axi_lite_s_arvalid,
    output logic          axi_lite_s_arready,
    output logic [63:0]   axi_lite_s_rdata,
    output logic          axi_lite_s_rvalid,
    input  wire           axi_lite_s_rready
);

  // =========================================================================
  // AXI Master - Directly tied off (unused)
  // =========================================================================
  
  assign axm_m0_awprot = 3'b000;
  assign axm_m0_arprot = 3'b000;
  assign axm_m0_wlast = 1'b1;
  assign axm_m0_rready = 1'b1;
  assign axm_m0_bready = 1'b1;
  assign axm_m0_awid = 4'b0;
  assign axm_m0_awaddr = 64'b0;
  assign axm_m0_awlen = 8'b0;
  assign axm_m0_awvalid = 1'b0;
  assign axm_m0_wdata = 1024'b0;
  assign axm_m0_wvalid = 1'b0;
  assign axm_m0_arid = 4'b0;
  assign axm_m0_araddr = 64'b0;
  assign axm_m0_arlen = 8'b0;
  assign axm_m0_arvalid = 1'b0;

  // =========================================================================
  // AXI-Lite Slave - Read Channel
  // =========================================================================
  //
  // State machine:
  //   R_IDLE:  Wait for arvalid
  //   R_ADDR:  Accept address (arready), capture it
  //   R_WAIT:  Wait for bus64to32 to complete (adapter_done)
  //   R_RESP:  Return data (rvalid), wait for rready
  //   R_DONE:  One cycle delay before next transaction

  typedef enum logic [2:0] {R_IDLE, R_ADDR, R_WAIT, R_RESP, R_DONE} r_state_t;
  r_state_t r_state;

  // Captured read address
  logic [15:0] r_addr_reg;

  always_ff @(posedge clk) begin
    if (rst) begin
      r_state <= R_IDLE;
      r_addr_reg <= 16'h0;
    end else begin
      case (r_state)
        R_IDLE: begin
          if (axi_lite_s_arvalid)
            r_state <= R_ADDR;
        end
        R_ADDR: begin
          // Capture address, move to wait
          r_addr_reg <= axi_lite_s_araddr[15:0];
          r_state <= R_WAIT;
        end
        R_WAIT: begin
          // Wait for adapter to complete
          if (adapter_done)
            r_state <= R_RESP;
        end
        R_RESP: begin
          // Hold rvalid until rready
          if (axi_lite_s_rready)
            r_state <= R_DONE;
        end
        R_DONE: begin
          r_state <= R_IDLE;
        end
      endcase
    end
  end

  // Read channel outputs
  assign axi_lite_s_arready = (r_state == R_ADDR);
  assign axi_lite_s_rvalid  = (r_state == R_RESP);

  // =========================================================================
  // AXI-Lite Slave - Write Channel
  // =========================================================================
  //
  // State machine:
  //   W_IDLE:  Wait for awvalid && wvalid
  //   W_ADDR:  Accept address/data (awready/wready), capture them
  //   W_WAIT:  Wait for bus64to32 to complete (adapter_done)
  //   W_RESP:  Return response (bvalid), wait for bready
  //   W_DONE:  One cycle delay before next transaction

  typedef enum logic [2:0] {W_IDLE, W_ADDR, W_WAIT, W_RESP, W_DONE} w_state_t;
  w_state_t w_state;

  // Captured write address and data
  logic [15:0] w_addr_reg;
  logic [63:0] w_data_reg;
  logic        w_strb_valid;

  always_ff @(posedge clk) begin
    if (rst) begin
      w_state <= W_IDLE;
      w_addr_reg <= 16'h0;
      w_data_reg <= 64'h0;
      w_strb_valid <= 1'b0;
    end else begin
      case (w_state)
        W_IDLE: begin
          if (axi_lite_s_awvalid && axi_lite_s_wvalid)
            w_state <= W_ADDR;
        end
        W_ADDR: begin
          // Capture address and data, move to wait
          w_addr_reg <= axi_lite_s_awaddr[15:0];
          w_data_reg <= axi_lite_s_wdata;
          w_strb_valid <= (axi_lite_s_wstrb != 8'h00);
          w_state <= W_WAIT;
        end
        W_WAIT: begin
          // Wait for adapter to complete
          if (adapter_done)
            w_state <= W_RESP;
        end
        W_RESP: begin
          // Hold bvalid until bready
          if (axi_lite_s_bready)
            w_state <= W_DONE;
        end
        W_DONE: begin
          w_strb_valid <= 1'b0;
          w_state <= W_IDLE;
        end
      endcase
    end
  end

  // Write channel outputs
  assign axi_lite_s_awready = (w_state == W_ADDR);
  assign axi_lite_s_wready  = (w_state == W_ADDR);
  assign axi_lite_s_bvalid  = (w_state == W_RESP);

  // =========================================================================
  // Bus 64-to-32 Adapter
  // =========================================================================
  //
  // Request pulses: assert for one cycle when entering W_WAIT or R_WAIT
  // The adapter captures addr/data on the same edge and starts processing.
  
  // Detect rising edge of WAIT states
  logic w_wait_prev, r_wait_prev;
  always_ff @(posedge clk) begin
    if (rst) begin
      w_wait_prev <= 1'b0;
      r_wait_prev <= 1'b0;
    end else begin
      w_wait_prev <= (w_state == W_WAIT);
      r_wait_prev <= (r_state == R_WAIT);
    end
  end
  
  // Request pulses: first cycle of WAIT state
  wire adapter_wen = (w_state == W_WAIT) && !w_wait_prev && w_strb_valid;
  wire adapter_ren = (r_state == R_WAIT) && !r_wait_prev;
  
  // Address mux: write has priority
  wire [15:0] adapter_addr = (w_state == W_WAIT) ? w_addr_reg : r_addr_reg;
  
  // Adapter signals
  wire        adapter_done;
  wire [63:0] adapter_rdata;
  wire [15:0] soc_addr;
  wire [31:0] soc_wdata;
  wire        soc_wen;
  wire        soc_ren;
  wire [31:0] soc_rdata;

  bus64to32 u_adapter (
    .clk       (clk),
    .rst_n     (~rst),
    
    // 64-bit side (from AXI state machine)
    .in_addr   (adapter_addr),
    .in_wdata  (w_data_reg),
    .in_wen    (adapter_wen),
    .in_ren    (adapter_ren),
    .out_rdata (adapter_rdata),
    .out_done  (adapter_done),
    
    // 32-bit side (to SoC)
    .out_addr  (soc_addr),
    .out_wdata (soc_wdata),
    .out_wen   (soc_wen),
    .out_ren   (soc_ren),
    .in_rdata  (soc_rdata)
  );

  // Read data output
  assign axi_lite_s_rdata = adapter_rdata;

  // =========================================================================
  // RISC-V SoC Instance
  // =========================================================================

  riscv_soc u_soc (
    .clk    (clk),
    .rst_n  (~rst),
    .addr   (soc_addr),
    .wdata  (soc_wdata),
    .wen    (soc_wen),
    .ren    (soc_ren),
    .rdata  (soc_rdata)
  );

endmodule

`default_nettype wire
