`timescale 1 ps / 1 ps
`default_nettype none

// AXI Core Hardware Wrapper
// ============================================================================
//
// Bridges PCIe AXI-Lite (64-bit) to RISC-V SoC.
// Converts 64-bit AXI transactions to two sequential 32-bit SoC accesses.
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
  // Converts 64-bit AXI read to two sequential 32-bit SoC reads:
  //   R_S0: Idle, wait for arvalid
  //   R_S1: Accept address (arready)
  //   R_S2: Read even word from SoC
  //   R_S3: Capture even word
  //   R_S4: Read odd word from SoC (addr+4)
  //   R_S5: Capture odd word
  //   R_S6: Return combined 64-bit (rvalid)
  //   R_S7: Done

  typedef enum {R_S0, R_S1, R_S2, R_S3, R_S4, R_S5, R_S6, R_S7} r_state_t;
  r_state_t next_r_state, r_state;

  always_ff @(posedge clk) begin
    if (rst) r_state <= R_S0;
    else r_state <= next_r_state;
  end

  always_comb begin
    next_r_state = r_state;
    axi_lite_s_arready = 0;
    axi_lite_s_rvalid = 0;
    case (r_state)
      R_S0: if (axi_lite_s_arvalid) next_r_state = R_S1;
      R_S1: begin
        axi_lite_s_arready = 1;
        next_r_state = R_S2;
      end
      R_S2: next_r_state = R_S3;
      R_S3: next_r_state = R_S4;
      R_S4: next_r_state = R_S5;
      R_S5: next_r_state = R_S6;
      R_S6: begin
        axi_lite_s_rvalid = 1;
        if (axi_lite_s_rready) next_r_state = R_S7;
      end
      R_S7: next_r_state = R_S0;
    endcase
  end

  // Capture read address
  logic [21:0] bar_raddr_base;
  always_ff @(posedge clk) begin
    if (r_state == R_S0 && axi_lite_s_arvalid)
      bar_raddr_base <= axi_lite_s_araddr[21:0];
  end
  
  // Read address: even in R_S2/R_S3, odd (addr+4) in R_S4/R_S5
  wire [21:0] bar_raddr = ((r_state == R_S4) || (r_state == R_S5)) ? 
                          (bar_raddr_base + 22'd4) : bar_raddr_base;
  
  // Capture read data
  logic [31:0] rdata_even;
  logic [31:0] rdata_odd;

  // =========================================================================
  // AXI-Lite Slave - Write Channel
  // =========================================================================
  //
  // Converts 64-bit AXI write to two sequential 32-bit SoC writes:
  //   W_S0: Idle, wait for awvalid && wvalid
  //   W_S1: Accept (awready/wready), capture address/data
  //   W_S2: Write even word (lower 32 bits)
  //   W_S3: Write odd word (upper 32 bits) at addr+4
  //   W_S4: Return response (bvalid)
  //   W_S5: Done

  typedef enum {W_S0, W_S1, W_S2, W_S3, W_S4, W_S5} w_state_t;
  w_state_t next_w_state, w_state;

  always_ff @(posedge clk) begin
    if (rst) w_state <= W_S0;
    else w_state <= next_w_state;
  end

  always_comb begin
    next_w_state = w_state;
    axi_lite_s_awready = 0;
    axi_lite_s_wready = 0;
    axi_lite_s_bvalid = 0;
    case (w_state)
      W_S0: if (axi_lite_s_awvalid && axi_lite_s_wvalid) next_w_state = W_S1;
      W_S1: begin
        axi_lite_s_awready = 1;
        axi_lite_s_wready = 1;
        next_w_state = W_S2;
      end
      W_S2: next_w_state = W_S3;
      W_S3: next_w_state = W_S4;
      W_S4: begin
        axi_lite_s_bvalid = 1;
        if (axi_lite_s_bready) next_w_state = W_S5;
      end
      W_S5: next_w_state = W_S0;
    endcase
  end

  // Capture write address and data
  logic [21:0] bar_waddr_base;
  logic [63:0] bar_wdata_captured;
  logic        bar_wstrb_valid;
  
  always_ff @(posedge clk) begin
    if (rst) begin
      bar_wstrb_valid <= 1'b0;
    end else if (w_state == W_S1) begin
      bar_waddr_base <= axi_lite_s_awaddr[21:0];
      bar_wdata_captured <= axi_lite_s_wdata;
      bar_wstrb_valid <= (axi_lite_s_wstrb != 8'h00);
    end else if (w_state == W_S5) begin
      bar_wstrb_valid <= 1'b0;
    end
  end
  
  // Write address: even in W_S2, odd (addr+4) in W_S3
  wire [21:0] bar_waddr = (w_state == W_S3) ? (bar_waddr_base + 22'd4) : bar_waddr_base;
  
  // Write data: lower 32 in W_S2, upper 32 in W_S3
  wire [63:0] bar_wdata = (w_state == W_S3) ? {32'b0, bar_wdata_captured[63:32]} : 
                                               {32'b0, bar_wdata_captured[31:0]};
  
  // Write enable: pulse in W_S2 and W_S3
  wire bar_wen = ((w_state == W_S2) || (w_state == W_S3)) && bar_wstrb_valid;

  // =========================================================================
  // RISC-V SoC Instance
  // =========================================================================
  
  // Read enable
  wire bar_ren = (r_state == R_S2) || (r_state == R_S4);
  
  // Address mux
  wire [15:0] soc_addr = bar_wen ? bar_waddr[15:0] : bar_raddr[15:0];
  
  wire [63:0] soc_rdata;
  
  riscv_soc u_soc(
    .clk(clk),
    .rst_n(~rst),
    .addr(soc_addr),
    .wdata(bar_wdata),
    .wen(bar_wen),
    .ren(bar_ren),
    .rdata(soc_rdata)
  );
  
  // =========================================================================
  // Read Data Capture
  // =========================================================================
  
  always_ff @(posedge clk) begin
    if (r_state == R_S3)
      rdata_even <= soc_rdata[31:0];
    if (r_state == R_S5)
      rdata_odd <= soc_rdata[31:0];
  end
  
  // Final AXI read response
  assign axi_lite_s_rdata = {rdata_odd, rdata_even};

endmodule

`default_nettype wire
