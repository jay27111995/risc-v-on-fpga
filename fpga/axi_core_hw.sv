`timescale 1 ps / 1 ps

`default_nettype none

// AXI Core for RISC-V SoC
// Connects PCIe BAR (via AXI-Lite) to RISC-V SoC
// AXI Master ports are stubbed (no DMA needed)

module axi_core_hw(
    input  wire          clk,
    input  wire          rst,

    // AXI Master - stubbed (not used for RISC-V SoC)
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

    // AXI Lite Slave - BAR access
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
  // Stub AXI Master (not used)
  // =========================================================================
  
  assign axm_m0_awprot = 3'b000;
  assign axm_m0_arprot = 3'b000;
  assign axm_m0_wlast = 1'b1;
  assign axm_m0_rready = 1'b1;
  
  always_comb begin
    axm_m0_awid = '0;
    axm_m0_awaddr = '0;
    axm_m0_awlen = '0;
    axm_m0_awvalid = 0;
    axm_m0_wdata = '0;
    axm_m0_wvalid = 0;
    axm_m0_bready = 1;
    axm_m0_arid = '0;
    axm_m0_araddr = '0;
    axm_m0_arlen = '0;
    axm_m0_arvalid = 0;
  end

  // =========================================================================
  // AXI Lite Slave - Read Channel
  // =========================================================================

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

  logic [15:0] bar_raddr;
  always_ff @(posedge clk) begin
    if (next_r_state == R_S2 && r_state != next_r_state) begin
      bar_raddr <= axi_lite_s_araddr[15:0];
    end
  end

  // =========================================================================
  // AXI Lite Slave - Write Channel
  // =========================================================================

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

  logic bar_wen;
  assign bar_wen = next_w_state == W_S2 && w_state != next_w_state && axi_lite_s_wstrb == 8'hff;
  
  logic [15:0] bar_waddr;
  assign bar_waddr = axi_lite_s_awaddr[15:0];

  // =========================================================================
  // Clock divider for RISC-V SoC (500MHz / 4 = 125MHz)
  // This helps meet timing while keeping AXI at full speed
  // =========================================================================
  
  logic [1:0] clk_div_cnt;
  logic cpu_clk_en;  // Clock enable for CPU (pulses every 4 cycles)
  
  always_ff @(posedge clk) begin
    if (rst) begin
      clk_div_cnt <= 0;
    end else begin
      clk_div_cnt <= clk_div_cnt + 1;
    end
  end
  
  assign cpu_clk_en = (clk_div_cnt == 2'b00);

  // =========================================================================
  // RISC-V SoC
  // =========================================================================

  riscv_soc u_soc(
    .clk(clk),
    .clk_en(cpu_clk_en),
    .rst_n(~rst),
    .bar_addr(bar_wen ? bar_waddr : bar_raddr),
    .bar_wdata(axi_lite_s_wdata),
    .bar_wen(bar_wen),
    .bar_rdata(axi_lite_s_rdata)
  );

endmodule
