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
  logic [19:0] r_addr_reg;

  always_ff @(posedge clk) begin
    if (rst) begin
      r_state <= R_IDLE;
      r_addr_reg <= 20'h0;
    end else begin
      case (r_state)
        R_IDLE: begin
          if (axi_lite_s_arvalid)
            r_state <= R_ADDR;
        end
        R_ADDR: begin
          // Capture address, move to wait
          r_addr_reg <= axi_lite_s_araddr[19:0];
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
  logic [19:0] w_addr_reg;
  logic [63:0] w_data_reg;
  logic        w_strb_valid;

  always_ff @(posedge clk) begin
    if (rst) begin
      w_state <= W_IDLE;
      w_addr_reg <= 20'h0;
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
          w_addr_reg <= axi_lite_s_awaddr[19:0];
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
  wire [19:0] adapter_addr = (w_state == W_WAIT) ? w_addr_reg : r_addr_reg;

  // Adapter signals
  wire        adapter_done;
  wire [63:0] adapter_rdata;
  wire [19:0] soc_addr;
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
  // Bus Sniffer (monitors host transactions to SoC)
  // =========================================================================
  //
  // Sits between bus64to32 and riscv_soc to log all 32-bit transactions.
  // Sniffer log is accessible at addr 0x4xxx (directly from bus64to32 output).
  //
  // Control registers:
  //   0x4000 = log_count (RO)
  //   0x4004 = log_cycle (RO)
  //   0x4008 = control: [0]=enable, [1]=clear (write 1 to clear, auto-clears)

  wire [19:0] sniff_out_addr;
  wire [31:0] sniff_out_wdata;
  wire        sniff_out_wen;
  wire        sniff_out_ren;
  wire [31:0] sniff_in_rdata;

  // Sniffer control register
  logic sniffer_enable;
  logic sniffer_clear_req;   // Request from host write
  logic sniffer_clear;       // Actual clear signal (delayed by 1 cycle)

  always_ff @(posedge clk) begin
    if (rst) begin
      sniffer_enable <= 1'b1;  // Enabled by default
      sniffer_clear_req <= 1'b0;
      sniffer_clear <= 1'b0;
    end else begin
      // Delay the clear request by one cycle so bus_sniffer sees it
      sniffer_clear <= sniffer_clear_req;
      sniffer_clear_req <= 1'b0;  // Auto-clear request

      if (soc_wen && is_sniffer_addr && soc_addr[7:0] == 8'h08) begin
        sniffer_enable <= soc_wdata[0];
        sniffer_clear_req <= soc_wdata[1];
      end
    end
  end

  // Sniffer log read interface
  wire [3:0]   sniffer_log_idx = soc_addr[7:4] - 4'd1;  // Entry 0 at 0x4010, entry 1 at 0x4020
  wire [127:0] sniffer_log_entry;
  wire [31:0]  sniffer_log_count;
  wire [31:0]  sniffer_log_cycle;

  // Sniffer log read data mux (addr 0x4xxx)
  // 0x4000 = log_count, 0x4004 = log_cycle, 0x4008 = control
  // 0x4010 = entry[0] bits[31:0],   0x4014 = entry[0] bits[63:32],
  // 0x4018 = entry[0] bits[95:64],  0x401C = entry[0] bits[127:96]
  // 0x4020 = entry[1] bits[31:0],   etc.
  logic [31:0] sniffer_rdata;
  always_comb begin
    if (soc_addr[7:4] == 4'h0) begin
      // Control registers
      case (soc_addr[3:2])
        2'd0: sniffer_rdata = sniffer_log_count;
        2'd1: sniffer_rdata = sniffer_log_cycle;
        2'd2: sniffer_rdata = {30'b0, 1'b0, sniffer_enable};  // control
        default: sniffer_rdata = 32'h0;
      endcase
    end else begin
      // Log entries (addr[7:4] - 1 = log index, addr[3:2] = word within entry)
      case (soc_addr[3:2])
        2'd0: sniffer_rdata = sniffer_log_entry[31:0];
        2'd1: sniffer_rdata = sniffer_log_entry[63:32];
        2'd2: sniffer_rdata = sniffer_log_entry[95:64];
        2'd3: sniffer_rdata = sniffer_log_entry[127:96];
      endcase
    end
  end

  // Route sniffer addresses (0x4xxx) directly, others go through sniffer to SoC
  wire is_sniffer_addr = (soc_addr[19:12] == 8'h40);

  bus_sniffer #(
    .LOG_DEPTH(32)
  ) u_sniffer (
    .clk       (clk),
    .rst_n     (~rst),

    // Control
    .log_enable (sniffer_enable),
    .log_clear  (sniffer_clear),

    // Upstream (from bus64to32) - only non-sniffer addresses pass through
    .in_addr   (soc_addr),
    .in_wdata  (soc_wdata),
    .in_wen    (soc_wen && !is_sniffer_addr),
    .in_ren    (soc_ren && !is_sniffer_addr),
    .out_rdata (sniff_in_rdata),

    // Downstream (to SoC)
    .out_addr  (sniff_out_addr),
    .out_wdata (sniff_out_wdata),
    .out_wen   (sniff_out_wen),
    .out_ren   (sniff_out_ren),
    .in_rdata  (soc_rdata_raw),

    // Log read interface
    .log_idx   (soc_addr[7:4] - 4'd1),  // Entry 0 at 0x4010, entry 1 at 0x4020, etc.
    .log_entry (sniffer_log_entry),
    .log_count (sniffer_log_count),
    .log_cycle (sniffer_log_cycle)
  );

  // SoC read data before sniffer mux
  wire [31:0] soc_rdata_raw;

  // Final read data mux: sniffer regs or SoC data (via sniffer passthrough)
  assign soc_rdata = is_sniffer_addr ? sniffer_rdata : sniff_in_rdata;

  // =========================================================================
  // RISC-V SoC Instance
  // =========================================================================

  riscv_soc u_soc (
    .clk    (clk),
    .rst_n  (~rst),
    .addr   (sniff_out_addr),
    .wdata  (sniff_out_wdata),
    .wen    (sniff_out_wen),
    .ren    (sniff_out_ren),
    .rdata  (soc_rdata_raw)
  );

endmodule

`default_nettype wire
