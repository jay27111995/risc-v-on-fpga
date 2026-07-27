`timescale 1 ps / 1 ps

`default_nettype none

module axi_core_hw(
    input  wire          clk,        // 500MHz AXI clock (also used for CPU)
    input  wire          cpu_clk,    // Unused - kept for PCIe system compatibility
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

  // =========================================================================
  // AXI-Lite Slave - Read Channel
  // =========================================================================
  //
  // Read State Machine:
  //   R_S0: Idle, wait for arvalid
  //   R_S1: Assert arready, capture address
  //   R_S2: Wait one cycle for SoC to register read data
  //   R_S3: Assert rvalid, return data
  //   R_S0: Idle, wait for arvalid
  //   R_S1: Accept (arready), capture address
  //   R_S2: Read even word from SoC (bar_ren for addr)
  //   R_S3: Capture even word result
  //   R_S4: Read odd word from SoC (bar_ren for addr+4)
  //   R_S5: Capture odd word result, combine into 64-bit
  //   R_S6: Return combined data (rvalid)
  //   R_S7: Done, return to idle
  //

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
      R_S2: next_r_state = R_S3;  // Read even word
      R_S3: next_r_state = R_S4;  // Capture even, setup odd addr
      R_S4: next_r_state = R_S5;  // Read odd word
      R_S5: next_r_state = R_S6;  // Capture odd, combine
      R_S6: begin
        axi_lite_s_rvalid = 1;
        if (axi_lite_s_rready) next_r_state = R_S7;
      end
      R_S7: next_r_state = R_S0;
    endcase
  end

  // Capture read address when arvalid first seen
  logic [21:0] bar_raddr_base;
  always_ff @(posedge clk) begin
    if (r_state == R_S0 && axi_lite_s_arvalid) begin
      bar_raddr_base <= axi_lite_s_araddr[21:0];
    end
  end
  
  // Read address: even word in R_S2/R_S3, odd word (addr+4) in R_S4/R_S5
  wire [21:0] bar_raddr = ((r_state == R_S4) || (r_state == R_S5)) ? 
                          (bar_raddr_base + 22'd4) : bar_raddr_base;
  
  // Capture registers for two sequential reads
  logic [31:0] rdata_even;
  logic [31:0] rdata_odd;

  // =========================================================================
  // AXI-Lite Slave - Write Channel  
  // =========================================================================
  //
  // Write State Machine - converts 64-bit AXI to two 32-bit SoC writes:
  //   W_S0: Idle, wait for awvalid && wvalid
  //   W_S1: Assert awready/wready, capture address/data
  //   W_S2: Write even word (lower 32 bits) to SoC
  //   W_S3: Write odd word (upper 32 bits) to SoC at addr+4
  //   W_S4: Assert bvalid, wait for bready
  //   W_S5: Done, return to idle
  //

  typedef enum {W_S0, W_S1, W_S2, W_S3, W_S4, W_S5} w_state_t;
  w_state_t next_w_state, w_state;

  always_ff @(posedge clk) begin
    if (rst) w_state <= W_S0;
    else w_state <= next_w_state;
  end

  // Write State Machine - converts 64-bit AXI write to two 32-bit SoC writes
  //   W_S0: Idle, wait for awvalid && wvalid
  //   W_S1: Assert awready/wready, capture address/data
  //   W_S2: Write even word (lower 32 bits) to SoC
  //   W_S3: Write odd word (upper 32 bits) to SoC at addr+4
  //   W_S4: Assert bvalid, wait for bready
  //   W_S5: Done, return to idle

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
      W_S2: next_w_state = W_S3;  // Write even word
      W_S3: next_w_state = W_S4;  // Write odd word
      W_S4: begin
        axi_lite_s_bvalid = 1;
        if (axi_lite_s_bready) next_w_state = W_S5;
      end
      W_S5: next_w_state = W_S0;
    endcase
  end

  // Capture address, data, and strobe validity when transaction accepted (W_S1)
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
  
  // Write address: even word in W_S2, odd word (addr+4) in W_S3
  wire [21:0] bar_waddr = (w_state == W_S3) ? (bar_waddr_base + 22'd4) : bar_waddr_base;
  
  // Write data: lower 32 bits in W_S2, upper 32 bits in W_S3
  wire [63:0] bar_wdata = (w_state == W_S3) ? {32'b0, bar_wdata_captured[63:32]} : {32'b0, bar_wdata_captured[31:0]};
  
  // Write enable: pulse in W_S2 (even) and W_S3 (odd)
  wire bar_wen = ((w_state == W_S2) || (w_state == W_S3)) && bar_wstrb_valid;

  // =========================================================================
  // DMA Burst Interface (unused, directly tied off)
  // =========================================================================
  
  assign rburst_req_valid = 0;
  assign rburst_req_id = 0;
  assign rburst_req_addr = 0;
  assign rburst_req_len = 0;
  assign wburst_req_valid = 0;
  assign wburst_req_id = 0;
  assign wburst_req_addr = 0;
  assign wburst_req_data = 0;
  assign wburst_req_len = 0;

  // =========================================================================
  // Debug Registers - capture last AXI write transaction
  // =========================================================================
  
  logic [21:0] dbg_last_awaddr;
  logic [63:0] dbg_last_wdata;
  logic [7:0]  dbg_last_wstrb;
  logic [31:0] dbg_write_count;
  
  always_ff @(posedge clk) begin
    if (rst) begin
      dbg_last_awaddr <= 22'h0;
      dbg_last_wdata <= 64'h0;
      dbg_last_wstrb <= 8'h0;
      dbg_write_count <= 32'h0;
    end else if (w_state == W_S1) begin
      // Capture raw AXI signals when write is accepted
      dbg_last_awaddr <= axi_lite_s_awaddr[21:0];
      dbg_last_wdata <= axi_lite_s_wdata;
      dbg_last_wstrb <= axi_lite_s_wstrb;
      dbg_write_count <= dbg_write_count + 1;
    end
  end

  // =========================================================================
  // RISC-V SoC Instance
  // =========================================================================
  
  // Read enable: asserted in R_S2 (even word) and R_S4 (odd word)
  wire bar_ren = (r_state == R_S2) || (r_state == R_S4);
  
  // Address mux: use write address during write, read address otherwise
  wire [15:0] bar_addr = bar_wen ? bar_waddr[15:0] : bar_raddr[15:0];
  
  // =========================================================================
  // Test Memory (addresses 0x200-0x2FF)
  // 64 x 32-bit words, write uses bar_wdata[31:0]
  // =========================================================================
  
  logic [31:0] test_mem [0:63];  // 64 x 32-bit = 256 bytes
  logic [31:0] test_mem_rdata;
  
  // Test memory write - same as SoC: uses bar_waddr[7:2] for 32-bit word index
  always_ff @(posedge clk) begin
    if (bar_wen && bar_waddr[15:8] == 8'h02) begin
      test_mem[bar_waddr[7:2]] <= bar_wdata[31:0];
    end
  end
  
  // Test memory read - single 32-bit read per cycle (same as SoC)
  always_ff @(posedge clk) begin
    if (bar_ren && bar_raddr[15:8] == 8'h02) begin
      test_mem_rdata <= test_mem[bar_raddr[7:2]];
    end
  end
  
  // =========================================================================
  // Debug Registers (addresses 0x100-0x12F)
  // =========================================================================
  
  logic [31:0] dbg_rdata;
  always_comb begin
    case (bar_raddr[5:2])
      4'd0: dbg_rdata = dbg_last_awaddr[21:0];           // 0x100: last AWADDR (low)
      4'd1: dbg_rdata = 32'h0;                           // 0x104: (high, unused)
      4'd2: dbg_rdata = dbg_last_wdata[31:0];            // 0x108: last WDATA (low)
      4'd3: dbg_rdata = dbg_last_wdata[63:32];           // 0x10C: last WDATA (high)
      4'd4: dbg_rdata = dbg_write_count;                 // 0x110: write count
      4'd5: dbg_rdata = {24'h0, dbg_last_wstrb};         // 0x114: WSTRB
      4'd6: dbg_rdata = dbg_soc_rdata;                   // 0x118: last SoC read data
      4'd7: dbg_rdata = {16'h0, dbg_soc_raddr};          // 0x11C: last SoC read addr
      4'd8: dbg_rdata = dbg_read_mux;                    // 0x120: last read_data_mux
      default: dbg_rdata = 32'h0;
    endcase
  end
  
  // =========================================================================
  // SoC Instance  
  // =========================================================================
  
  wire [63:0] soc_rdata;
  
  // Address mux for SoC: write address during write, read address otherwise
  wire [15:0] soc_addr = bar_wen ? bar_waddr[15:0] : bar_raddr[15:0];
  
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
  // Read Data Mux - select source based on address
  // =========================================================================
  
  wire [31:0] read_data_mux;
  assign read_data_mux = (bar_raddr[15:8] == 8'h01) ? dbg_rdata :      // 0x100-0x1FF: Debug
                         (bar_raddr[15:8] == 8'h02) ? test_mem_rdata : // 0x200-0x2FF: Test mem
                         soc_rdata[31:0];                               // Everything else: SoC
  
  // Debug: capture what we read from SoC during DMEM reads (0x2000+)
  logic [31:0] dbg_soc_rdata;
  logic [15:0] dbg_soc_raddr;
  logic [31:0] dbg_read_mux;
  
  always_ff @(posedge clk) begin
    // Only capture for DMEM range (0x2000-0x3FFF)
    if (bar_ren && (bar_raddr[15:12] == 4'h2 || bar_raddr[15:12] == 4'h3)) begin
      dbg_soc_rdata <= soc_rdata[31:0];
      dbg_soc_raddr <= bar_raddr[15:0];
      dbg_read_mux <= read_data_mux;
    end
  end
  
  // Update capture logic to use the muxed read data
  always_ff @(posedge clk) begin
    if (r_state == R_S3) begin
      rdata_even <= read_data_mux;
    end
    if (r_state == R_S5) begin
      rdata_odd <= read_data_mux;
    end
  end
  
  // Final AXI read response
  assign axi_lite_s_rdata = {rdata_odd, rdata_even};

endmodule
