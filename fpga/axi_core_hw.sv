`timescale 1 ps / 1 ps

`default_nettype none

module axi_core_hw(
    input  wire          clk,
    input  wire          rst,

    // AXI Master
    output logic [3:0]    axm_m0_awid,    // AWID
    output logic [63:0]   axm_m0_awaddr,  // AWADDR
    output logic [7:0]    axm_m0_awlen,   // AWLEN
    output wire [2:0]     axm_m0_awprot,  // AWPROT
    output logic          axm_m0_awvalid, // AWVALID
    input  wire           axm_m0_awready, // AWREADY

    output logic [1023:0] axm_m0_wdata,   // WDATA
    output wire           axm_m0_wlast,   // WLAST
    output logic          axm_m0_wvalid,  // WVALID
    input  wire           axm_m0_wready,  // WREADY

    input  wire [3:0]     axm_m0_bid,     // BID
    input  wire           axm_m0_bvalid,  // BVALID
    output logic          axm_m0_bready,  // BREADY

    output logic [3:0]    axm_m0_arid,    // ARID
    output logic [63:0]   axm_m0_araddr,  // ARADDR
    output logic [7:0]    axm_m0_arlen,   // ARLEN
    output wire [2:0]     axm_m0_arprot,  // ARPROT
    output logic          axm_m0_arvalid, // ARVALID
    input  wire           axm_m0_arready, // ARREADY

    input  wire [3:0]     axm_m0_rid,     // RID
    input  wire [1023:0]  axm_m0_rdata,   // RDATA
    input  wire           axm_m0_rlast,   // RLAST
    input  wire           axm_m0_rvalid,  // RVALID
    output wire           axm_m0_rready,  // RREADY

    // AXI Lite Slave
    input  wire [21:0]    axi_lite_s_awaddr,  // AWADDR
    input  wire           axi_lite_s_awvalid, // AWVALID
    output logic          axi_lite_s_awready, // AWREADY

    input  wire [63:0]    axi_lite_s_wdata,   // WDATA
    input  wire [7:0]     axi_lite_s_wstrb,   // WSTRB
    input  wire           axi_lite_s_wvalid,  // WVALID
    output logic          axi_lite_s_wready,  // WREADY

    output logic          axi_lite_s_bvalid,  // BVALID
    input  wire           axi_lite_s_bready,  // BREADY

    input  wire [21:0]    axi_lite_s_araddr,  // ARADDR
    input  wire           axi_lite_s_arvalid, // ARVALID
    output logic          axi_lite_s_arready, // ARREADY

    output logic [63:0]   axi_lite_s_rdata,   // RDATA
    output logic          axi_lite_s_rvalid,  // RVALID
    input  wire           axi_lite_s_rready   // RREADY
  );

  logic [63:0] reg_rdata;

  logic rburst_req_valid;
  logic [3:0] rburst_req_id;
  logic [7:0] rburst_req_len;
  logic [63:0] rburst_req_addr;
  logic [63:0] rburst_req_data;

  logic rburst_rsp_valid;
  logic [3:0] rburst_rsp_id;
  logic [63:0] rburst_rsp_data;

  logic wburst_req_valid;
  logic [3:0] wburst_req_id;
  logic [7:0] wburst_req_len;
  logic [63:0] wburst_req_addr;
  logic [63:0] wburst_req_data;

  logic wburst_rsp_valid;
  logic [3:0] wburst_rsp_id;

  //
  // AXI Master - Read Channel
  //

  assign axm_m0_arprot = 3'b000;

  typedef enum {AXM_R_S0, AXM_R_S1, AXM_R_S2} axm_r_state_t;
  axm_r_state_t next_axm_r_state, axm_r_state;

  always_ff @(posedge clk) begin
    if (rst) axm_r_state <= AXM_R_S0;
    else axm_r_state <= next_axm_r_state;
  end

  always_comb begin
    next_axm_r_state = axm_r_state;
    axm_m0_arvalid = 0;
    case (axm_r_state)
      AXM_R_S0: begin
        if (rburst_req_valid) next_axm_r_state = AXM_R_S1;
      end
      AXM_R_S1: begin
        axm_m0_arvalid = 1;
        if (axm_m0_arready) next_axm_r_state = AXM_R_S2;
      end
      AXM_R_S2: begin
        next_axm_r_state = AXM_R_S0;
      end
    endcase
  end

  always_ff @(posedge clk) begin
    if (next_axm_r_state == AXM_R_S1 && axm_r_state != next_axm_r_state) begin
      axm_m0_arid <= rburst_req_id;
      axm_m0_araddr <= rburst_req_addr;
      axm_m0_arlen <= rburst_req_len;
    end
  end

  // Handle R channel
  assign axm_m0_rready = 1;
  assign rburst_rsp_valid = axm_m0_rready & axm_m0_rvalid & axm_m0_rlast;
  assign rburst_rsp_id = axm_m0_rid;
  assign rburst_rsp_data = axm_m0_rdata[63:0];

  //
  // AXI Master - Write Channel
  //

  logic [7:0] awlen_cntr;

  assign axm_m0_awprot = 3'b000;
  assign axm_m0_wlast = (awlen_cntr == 0);

  typedef enum {AXM_S0, AXM_S1, AXM_S2} axm_state_t;
  axm_state_t next_axm_state, axm_state;

  always_ff @(posedge clk) begin
    if (rst) axm_state <= AXM_S0;
    else axm_state <= next_axm_state;
  end

  // Handle AW and W channels
  always_comb begin
    next_axm_state = axm_state;
    axm_m0_awvalid = 0;
    axm_m0_wvalid = 0;
    case (axm_state)
      AXM_S0: begin
        if (wburst_req_valid) next_axm_state = AXM_S1;
      end
      AXM_S1: begin
        axm_m0_awvalid = 1;
        if (axm_m0_awready) next_axm_state = AXM_S2;
      end
      AXM_S2: begin
        axm_m0_wvalid = 1;
        if (axm_m0_wready && awlen_cntr == 0) next_axm_state = AXM_S0;
      end
    endcase
  end

  always_ff @(posedge clk) begin
    if (next_axm_state == AXM_S1 && axm_state != next_axm_state) begin
      axm_m0_awid <= wburst_req_id;
      axm_m0_awaddr <= wburst_req_addr;
      axm_m0_awlen <= wburst_req_len;
      axm_m0_wdata <= {16{wburst_req_data}}; // Replicate data
    end
  end

  always_ff @(posedge clk) begin
    if (next_axm_state == AXM_S1 && axm_state != next_axm_state) begin
      awlen_cntr <= wburst_req_len;
    end
    else if (axm_state == AXM_S2 && axm_m0_wready) begin
      awlen_cntr <= awlen_cntr - 1;
    end
  end

  // Handle B channel
  assign axm_m0_bready = 1;
  assign wburst_rsp_valid = axm_m0_bready & axm_m0_bvalid;
  assign wburst_rsp_id = axm_m0_bid;

  //
  // AXI Lite Slave - Read Channel
  //

  typedef enum {R_S0, R_S1, R_S2, R_S3, R_S4} r_state_t;
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
      R_S0: begin
        if (axi_lite_s_arvalid) next_r_state = R_S1;
      end
      R_S1: begin
        axi_lite_s_arready = 1;
        next_r_state = R_S2;
      end
      R_S2: begin
        next_r_state = R_S3;
      end
      R_S3: begin
        axi_lite_s_rvalid = 1;
        if (axi_lite_s_rready) begin
          next_r_state = R_S4;
        end
      end
      R_S4: begin
          next_r_state = R_S0;
      end
    endcase
  end

  logic [21:0] bar_raddr;
  always_ff @(posedge clk) begin
    if (next_r_state == R_S2 && r_state != next_r_state) begin
      bar_raddr <= axi_lite_s_araddr[21:0];
    end
  end

  //
  // AXI Lite Slave - Write Channel
  //

  typedef enum {W_S0, W_S1, W_S2, W_S3} w_state_t;
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
      W_S0: begin
        if (axi_lite_s_awvalid & axi_lite_s_wvalid) next_w_state = W_S1;
      end
      W_S1: begin
        axi_lite_s_awready = 1;
        axi_lite_s_wready = 1;
        next_w_state = W_S2;
      end
      W_S2: begin
        axi_lite_s_bvalid = 1;
        if (axi_lite_s_bready) begin
          next_w_state = W_S3;
        end
      end
      W_S3: begin
          next_w_state = W_S0;
      end
    endcase
  end

  logic bar_wen64;
  assign bar_wen64 = next_w_state == W_S2 && w_state != next_w_state && axi_lite_s_wstrb == 8'hff;
  logic [21:0] bar_waddr;
  assign bar_waddr = axi_lite_s_awaddr[21:0];

  // Stub the DMA burst signals (not used by RISC-V SoC)
  assign rburst_req_valid = 0;
  assign rburst_req_id = 0;
  assign rburst_req_addr = 0;
  assign rburst_req_len = 0;
  assign wburst_req_valid = 0;
  assign wburst_req_id = 0;
  assign wburst_req_addr = 0;
  assign wburst_req_data = 0;
  assign wburst_req_len = 0;

  riscv_soc u_soc(
    .clk(clk),
    .clk_en(1'b1),
    .rst_n(~rst),
    .bar_addr(bar_wen64 ? bar_waddr[15:0] : bar_raddr[15:0]),
    .bar_wdata(axi_lite_s_wdata),
    .bar_wen(bar_wen64),
    .bar_rdata(axi_lite_s_rdata)
  );

endmodule
