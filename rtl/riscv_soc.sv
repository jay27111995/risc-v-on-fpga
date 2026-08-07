// RISC-V SoC
// ============================================================================
//
// A minimal RV32I CPU with 6-stage pipeline for high-frequency operation.
//
// Pipeline: IF -> ID -> EX1 -> EX2 -> MEM -> WB
//                       (fwd)  (ALU)
//
// The EX stage is split into EX1 (forwarding mux) and EX2 (ALU) to break
// the critical timing path: forwarding mux -> ALU -> result mux.
//
// Memory Map (active region selected by addr[19:17]):
//   0x0_0000 - 0x0_00FF  Control registers (256 bytes)
//   0x2_0000 - 0x3_FFFF  IMEM - 128KB instruction memory
//   0x4_0000 - 0x4_0FFF  Bus sniffer (4KB)
//   0x5_0000 - 0x5_0FFF  CPU Logger (4KB)
//   0x8_0000 - 0x8_7FFF  DMEM - 32KB data memory
//   0x4xxx  Bus sniffer logs (in axi_core_hw)
//   0x5xxx  CPU logger logs
//
// Address Decoding:
//   addr[19:12] = region select (8 bits)
//   addr[11:2]  = word index within region (ignore addr[1:0] byte offset)
//   addr[7:2]   = used for control reg decode (6 bits = 64 registers max)
//
// Control Registers (0x0xxx):
//   0x00  CTRL    [0] RUN, [1] RESET           (addr[7:2] == 0)
//   0x08  STATUS  [0] RUNNING, [1] HALTED      (addr[7:2] == 2)
//                 HALTED is set when EBREAK executes, cleared on RESET
//   0x10  PC      Current program counter      (addr[7:2] == 4)
//   0x20  CYCLES  Performance counter          (addr[7:2] == 8)
//   0x24  INSTRS  Instructions retired         (addr[7:2] == 9)
//   0x28  STALLS  Stall cycles                 (addr[7:2] == 10)
//   0x2C  BRANCHES Branch count                (addr[7:2] == 11)
//   0x30  BR_TAKEN Branches taken              (addr[7:2] == 12)
//   0x34  LOADS   Load count                   (addr[7:2] == 13)
//   0x38  STORES  Store count                  (addr[7:2] == 14)
//
// CPU Logger Registers (0x5xxx):
//   0x5000  LOG_COUNT   Total transactions logged (RO)
//   0x5004  LOG_CYCLE   Current cycle count (RO)
//   0x5008  LOG_CTRL    Control: [0]=enable (RW), [1]=clear (W), [2]=log_imem (RW)
//                       Default: enable=1, log_imem=0 (DMEM only)
//   0x5010  ENTRY[0]    Newest log entry (3 words: data, addr, timestamp|type)
//   0x5020  ENTRY[1]    Second newest, etc.
//
// Log entry types: 00=IFETCH, 01=DLOAD, 10=DSTORE
//
// ============================================================================

module riscv_soc (
    input  logic        clk,
    input  logic        rst_n,

    // Host interface (32-bit, from bus64to32 adapter)
    input  logic [19:0] addr,
    input  logic [31:0] wdata,
    input  logic        wen,
    input  logic        ren,
    output logic [31:0] rdata
);

    // =========================================================================
    // Control Registers
    // =========================================================================

    logic        ctrl_run;
    logic        ctrl_reset;
    logic        cpu_halted;      // Set when EBREAK executed
    logic [31:0] cpu_pc;

    logic cpu_rst;
    logic cpu_running;

    assign cpu_rst = ~rst_n | ctrl_reset;
    assign cpu_running = ctrl_run & ~ctrl_reset & ~cpu_halted;

    // Forward declaration of EBREAK signal from WB stage
    logic wb_ebreak;

    always_ff @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            ctrl_run <= 1'b0;
            ctrl_reset <= 1'b0;
            cpu_halted <= 1'b0;
        end else begin
            if (ctrl_reset) begin
                ctrl_reset <= 1'b0;
                cpu_halted <= 1'b0;  // Clear halted on reset
            end
            
            // EBREAK halts the CPU
            if (wb_ebreak && cpu_running)
                cpu_halted <= 1'b1;

            if (wen && addr[19:12] == 8'h00 && addr[7:2] == 6'd0) begin
                ctrl_run   <= wdata[0];
                ctrl_reset <= wdata[1];
                // Writing to CTRL clears halted state (allows restart)
                if (wdata[1])  // Reset also clears halted
                    cpu_halted <= 1'b0;
            end
        end
    end

    // =========================================================================
    // Performance Counters
    // =========================================================================
    //
    // Readable at:
    //   0x20 = CYCLES      - Total cycles since last reset
    //   0x24 = INSTRS      - Instructions retired (completed)
    //   0x28 = STALLS      - Stall cycles (load-use hazards)
    //   0x2C = BRANCHES    - Branch instructions executed
    //   0x30 = BR_TAKEN    - Branches actually taken
    //   0x34 = LOADS       - Load instructions
    //   0x38 = STORES      - Store instructions
    //
    // Write any value to 0x20 to clear all counters.

    logic [31:0] perf_cycles;
    logic [31:0] perf_instrs;
    logic [31:0] perf_stalls;
    logic [31:0] perf_branches;
    logic [31:0] perf_br_taken;
    logic [31:0] perf_loads;
    logic [31:0] perf_stores;

    // Forward declarations for counter inputs
    logic wb_valid;
    logic mem_branch, mem_branch_taken, mem_mem_read, mem_mem_write, mem_valid;

    always_ff @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            perf_cycles    <= 32'h0;
            perf_instrs    <= 32'h0;
            perf_stalls    <= 32'h0;
            perf_branches  <= 32'h0;
            perf_br_taken  <= 32'h0;
            perf_loads     <= 32'h0;
            perf_stores    <= 32'h0;
        end else if (cpu_rst || (wen && addr[19:12] == 8'h00 && addr[7:2] == 6'd8)) begin
            // Clear on CPU reset or write to 0x20
            perf_cycles    <= 32'h0;
            perf_instrs    <= 32'h0;
            perf_stalls    <= 32'h0;
            perf_branches  <= 32'h0;
            perf_br_taken  <= 32'h0;
            perf_loads     <= 32'h0;
            perf_stores    <= 32'h0;
        end else if (cpu_running) begin
            perf_cycles <= perf_cycles + 1;

            if (wb_valid)
                perf_instrs <= perf_instrs + 1;

            if (stall)
                perf_stalls <= perf_stalls + 1;

            if (mem_branch && mem_valid)
                perf_branches <= perf_branches + 1;

            if (mem_branch_taken)
                perf_br_taken <= perf_br_taken + 1;

            if (mem_mem_read && mem_valid)
                perf_loads <= perf_loads + 1;

            if (mem_mem_write && mem_valid)
                perf_stores <= perf_stores + 1;
        end
    end

    // =========================================================================
    // IMEM - Instruction Memory (4KB, 32-bit)
    // =========================================================================

    // IMEM: 128KB (32768 × 32-bit) for Zephyr RTOS
    // Host address: 0x20000 - 0x3FFFF (128KB)
    // CPU address: 0x00000 - 0x1FFFF (direct PC mapped to same array)
    (* ramstyle = "M20K" *) logic [31:0] imem [0:32767];
    logic [31:0] imem_host_rdata;

    // Host write: address range 0x2_0000 - 0x3_FFFF (128KB)
    wire is_imem_host = (addr[19:17] == 3'b001);  // 0x20000-0x3FFFF
    wire [14:0] host_imem_idx = addr[16:2];
    always_ff @(posedge clk) begin
        if (wen && is_imem_host)
            imem[host_imem_idx] <= wdata;
    end

    // Host read
    always_ff @(posedge clk) begin
        if (ren && is_imem_host)
            imem_host_rdata <= imem[host_imem_idx];
    end

    // Note: IMEM initialization removed for FPGA synthesis (exceeds 5000 iteration limit)
    // Host will load program before running CPU

    // =========================================================================
    // DMEM - Data Memory (32KB) - Block RAM for Zephyr RTOS
    // Host address: 0x40000 - 0x47FFF (32KB)
    // CPU address: 0x00000 - 0x07FFF (direct)
    // =========================================================================
    //
    // Split into 4 byte-wide RAMs for clean M20K inference with byte enables.

    (* ramstyle = "M20K" *) logic [7:0] dmem_b0 [0:8191];  // Byte 0
    (* ramstyle = "M20K" *) logic [7:0] dmem_b1 [0:8191];  // Byte 1
    (* ramstyle = "M20K" *) logic [7:0] dmem_b2 [0:8191];  // Byte 2
    (* ramstyle = "M20K" *) logic [7:0] dmem_b3 [0:8191];  // Byte 3

    // CPU port signals
    logic [31:0] cpu_dmem_addr;
    logic [31:0] cpu_dmem_wdata;
    logic [31:0] cpu_dmem_rdata;
    logic        cpu_dmem_we;
    logic [3:0]  cpu_dmem_be;  // Byte enables for byte/half stores

    // Address indexing (word-aligned, 32-bit entries) - 13 bits for 8K words
    wire [12:0] cpu_dmem_idx  = cpu_dmem_addr[14:2];
    wire [12:0] host_dmem_idx = addr[14:2];

    // Write enables - DMEM at 0x8_0000 - 0x8_7FFF (32KB)
    wire is_dmem_host = (addr[19:15] == 5'b10000);  // 0x80000-0x87FFF
    wire host_dmem_wen = wen && is_dmem_host;
    wire cpu_dmem_wen  = cpu_dmem_we && cpu_running && !host_dmem_wen;

    // Byte 0
    always_ff @(posedge clk) begin
        if (host_dmem_wen)
            dmem_b0[host_dmem_idx] <= wdata[7:0];
        else if (cpu_dmem_wen && cpu_dmem_be[0])
            dmem_b0[cpu_dmem_idx] <= cpu_dmem_wdata[7:0];
    end

    // Byte 1
    always_ff @(posedge clk) begin
        if (host_dmem_wen)
            dmem_b1[host_dmem_idx] <= wdata[15:8];
        else if (cpu_dmem_wen && cpu_dmem_be[1])
            dmem_b1[cpu_dmem_idx] <= cpu_dmem_wdata[15:8];
    end

    // Byte 2
    always_ff @(posedge clk) begin
        if (host_dmem_wen)
            dmem_b2[host_dmem_idx] <= wdata[23:16];
        else if (cpu_dmem_wen && cpu_dmem_be[2])
            dmem_b2[cpu_dmem_idx] <= cpu_dmem_wdata[23:16];
    end

    // Byte 3
    always_ff @(posedge clk) begin
        if (host_dmem_wen)
            dmem_b3[host_dmem_idx] <= wdata[31:24];
        else if (cpu_dmem_wen && cpu_dmem_be[3])
            dmem_b3[cpu_dmem_idx] <= cpu_dmem_wdata[31:24];
    end

    // CPU read (registered for Block RAM timing)
    always_ff @(posedge clk) begin
        cpu_dmem_rdata <= {dmem_b3[cpu_dmem_idx], dmem_b2[cpu_dmem_idx],
                          dmem_b1[cpu_dmem_idx], dmem_b0[cpu_dmem_idx]};
    end

    // Host read (registered)
    logic [31:0] dmem_host_rdata;
    always_ff @(posedge clk) begin
        if (ren && is_dmem_host)
            dmem_host_rdata <= {dmem_b3[host_dmem_idx], dmem_b2[host_dmem_idx],
                               dmem_b1[host_dmem_idx], dmem_b0[host_dmem_idx]};
    end

    // Note: DMEM initialization removed for FPGA synthesis (exceeds 5000 iteration limit)
    // Block RAM initializes to 0 by default on Agilex

    // =========================================================================
    // Host Read Multiplexer
    // =========================================================================
    // New Memory Map:
    //   0x0_0000 - 0x0_00FF: Control registers
    //   0x1_0000 - 0x2_FFFF: IMEM (128KB)
    //   0x3_0000 - 0x3_7FFF: DMEM (32KB)
    //   0x5_0000: CPU logger

    always_comb begin
        rdata = 32'h0;
        casez (addr[19:12])
            8'h00: begin  // Control registers
                case (addr[7:2])
                    6'd0:  rdata = {30'b0, ctrl_reset, ctrl_run};  // 0x00 CTRL
                    6'd2:  rdata = {30'b0, cpu_halted, cpu_running};  // 0x08 STATUS [0]=running, [1]=halted
                    6'd4:  rdata = cpu_pc;                         // 0x10 PC
                    6'd8:  rdata = perf_cycles;                    // 0x20 CYCLES
                    6'd9:  rdata = perf_instrs;                    // 0x24 INSTRS
                    6'd10: rdata = perf_stalls;                    // 0x28 STALLS
                    6'd11: rdata = perf_branches;                  // 0x2C BRANCHES
                    6'd12: rdata = perf_br_taken;                  // 0x30 BR_TAKEN
                    6'd13: rdata = perf_loads;                     // 0x34 LOADS
                    6'd14: rdata = perf_stores;                    // 0x38 STORES
                    default: rdata = 32'h0;
                endcase
            end
            8'h2?, 8'h3?: rdata = imem_host_rdata;   // IMEM 0x2_0000 - 0x3_FFFF
            8'h8?:        rdata = dmem_host_rdata;   // DMEM 0x8_0000 - 0x8_7FFF
            8'h50:        rdata = cpu_logger_rdata;  // CPU logger 0x5_0000
            default:      rdata = 32'h0;
        endcase
    end

    // =========================================================================
    // Pipeline Hazard Control
    // =========================================================================

    logic stall;
    logic flush;

    // Forward declarations for hazard detection and performance counters
    // 6-stage pipeline: IF -> ID -> EX1 -> EX2 -> MEM -> WB
    //                              (fwd)   (ALU)
    logic        ex1_mem_read, ex1_valid;
    logic        ex2_mem_read, ex2_valid;
    logic [4:0]  ex1_rd, ex2_rd, id_rs1, id_rs2;
    logic        id_valid;
    logic [31:0] mem_branch_target;
    logic [4:0]  mem_rd, ex1_rs1, ex1_rs2;

    // Load-use hazard: stall when a load is followed by dependent instruction
    wire hazard_load_use_ex1 = ex1_mem_read && ex1_valid && (ex1_rd != 5'd0) &&
                               ((ex1_rd == id_rs1) || (ex1_rd == id_rs2)) && id_valid;
    wire hazard_load_use_ex2 = ex2_mem_read && ex2_valid && (ex2_rd != 5'd0) &&
                               ((ex2_rd == id_rs1) || (ex2_rd == id_rs2)) && id_valid;
    wire hazard_load_use_mem = mem_mem_read && mem_valid && (mem_rd != 5'd0) &&
                               ((mem_rd == id_rs1) || (mem_rd == id_rs2)) && id_valid;

    // Load data wait: stall 1 cycle for Block RAM read latency
    logic mem_load_wait;
    wire hazard_load_data = mem_mem_read && mem_valid && mem_load_wait;

    assign stall = hazard_load_use_ex1 || hazard_load_use_ex2 || hazard_load_use_mem || hazard_load_data;
    assign flush = mem_branch_taken;

    // Track when we need to wait for load data
    // Set when a load enters MEM, cleared after 1 cycle
    always_ff @(posedge clk) begin
        if (cpu_rst || flush) begin
            mem_load_wait <= 1'b0;
        end else if (cpu_running) begin
            if (!hazard_load_data) begin
                // New instruction entering MEM - set wait if it's a load
                mem_load_wait <= ex2_mem_read && ex2_valid;
            end else begin
                // Currently waiting for load data - clear after 1 cycle
                mem_load_wait <= 1'b0;
            end
        end
    end

    // =========================================================================
    // Stage 1: IF (Instruction Fetch)
    // =========================================================================

    logic [31:0] if_pc;
    logic [31:0] if_instr;

    wire [31:0] if_pc_next = mem_branch_taken ? mem_branch_target : (if_pc + 32'd4);

    always_ff @(posedge clk) begin
        if (cpu_rst)
            if_pc <= 32'h0;
        else if (cpu_running && !stall)
            if_pc <= if_pc_next;
    end

    assign if_instr = imem[if_pc[16:2]];
    assign cpu_pc = if_pc;

    // =========================================================================
    // IF/ID Pipeline Register
    // =========================================================================

    logic [31:0] id_pc;
    logic [31:0] id_instr;

    always_ff @(posedge clk) begin
        if (cpu_rst || flush) begin
            id_valid <= 1'b0;
            id_instr <= 32'h00000013;
        end else if (cpu_running && !stall) begin
            id_pc    <= if_pc;
            id_instr <= if_instr;
            id_valid <= 1'b1;
        end
    end

    // =========================================================================
    // Stage 2: ID (Instruction Decode)
    // =========================================================================

    logic [4:0]  id_rd;
    logic [31:0] id_imm;
    logic [3:0]  id_alu_op;
    logic        id_reg_write, id_alu_src, id_mem_read, id_mem_write, id_branch;
    logic [2:0]  id_branch_op;
    logic        id_jump, id_jump_reg;
    logic        id_lui, id_auipc;
    logic        id_ebreak;
    logic [2:0]  id_mem_op;

    decoder decoder_inst (
        .instr     (id_instr),
        .rs1       (id_rs1),
        .rs2       (id_rs2),
        .rd        (id_rd),
        .imm       (id_imm),
        .alu_op    (id_alu_op),
        .reg_write (id_reg_write),
        .alu_src   (id_alu_src),
        .mem_read  (id_mem_read),
        .mem_write (id_mem_write),
        .mem_op    (id_mem_op),
        .branch    (id_branch),
        .branch_op (id_branch_op),
        .jump      (id_jump),
        .jump_reg  (id_jump_reg),
        .lui       (id_lui),
        .auipc     (id_auipc),
        .ebreak    (id_ebreak)
    );

    logic [31:0] id_rs1_data, id_rs2_data;

    regfile regfile_inst (
        .clk      (clk),
        .we       (wb_reg_write && cpu_running),
        .rs1_addr (id_rs1),
        .rs2_addr (id_rs2),
        .rd_addr  (wb_rd),
        .rd_data  (wb_rd_data),
        .rs1_data (id_rs1_data),
        .rs2_data (id_rs2_data)
    );

    // =========================================================================
    // ID/EX1 Pipeline Register
    // =========================================================================

    logic [31:0] ex1_pc;
    logic [31:0] ex1_rs1_data, ex1_rs2_data;
    logic [31:0] ex1_imm;
    logic [3:0]  ex1_alu_op;
    logic        ex1_reg_write, ex1_alu_src, ex1_mem_write, ex1_branch;
    logic [2:0]  ex1_branch_op;
    logic        ex1_jump, ex1_jump_reg;
    logic        ex1_lui, ex1_auipc;
    logic        ex1_ebreak;
    logic [2:0]  ex1_mem_op;

    always_ff @(posedge clk) begin
        if (cpu_rst || flush || stall) begin
            ex1_valid     <= 1'b0;
            ex1_reg_write <= 1'b0;
            ex1_mem_read  <= 1'b0;
            ex1_mem_write <= 1'b0;
            ex1_branch    <= 1'b0;
            ex1_jump      <= 1'b0;
            ex1_lui       <= 1'b0;
            ex1_auipc     <= 1'b0;
            ex1_ebreak    <= 1'b0;
        end else if (cpu_running) begin
            ex1_pc        <= id_pc;
            ex1_rs1_data  <= id_rs1_data;
            ex1_rs2_data  <= id_rs2_data;
            ex1_imm       <= id_imm;
            ex1_alu_op    <= id_alu_op;
            ex1_rs1       <= id_rs1;
            ex1_rs2       <= id_rs2;
            ex1_rd        <= id_rd;
            ex1_reg_write <= id_reg_write && id_valid;
            ex1_alu_src   <= id_alu_src;
            ex1_mem_read  <= id_mem_read  && id_valid;
            ex1_mem_write <= id_mem_write && id_valid;
            ex1_mem_op    <= id_mem_op;
            ex1_branch    <= id_branch    && id_valid;
            ex1_branch_op <= id_branch_op;
            ex1_jump      <= id_jump      && id_valid;
            ex1_jump_reg  <= id_jump_reg;
            ex1_lui       <= id_lui       && id_valid;
            ex1_auipc     <= id_auipc     && id_valid;
            ex1_ebreak    <= id_ebreak    && id_valid;
            ex1_valid     <= id_valid;
        end
    end

    // =========================================================================
    // Stage 3: EX1 (Forwarding Mux Selection)
    // =========================================================================
    //
    // This stage selects forwarded operands and registers them.
    // Breaking the forwarding mux from the ALU reduces the critical path.

    logic [31:0] ex2_result;      // Forward declaration
    logic [31:0] mem_alu_result;
    logic        mem_reg_write;
    logic        ex2_reg_write;
    logic        wb_reg_write;
    logic        wb_mem_read;
    logic [4:0]  wb_rd;
    logic [31:0] wb_rd_data;

    // Forwarding from EX2 stage (just computed ALU result)
    wire fwd_ex2_rs1 = ex2_reg_write && !ex2_mem_read && (ex2_rd != 5'd0) && (ex2_rd == ex1_rs1);
    wire fwd_ex2_rs2 = ex2_reg_write && !ex2_mem_read && (ex2_rd != 5'd0) && (ex2_rd == ex1_rs2);

    // Forwarding from MEM stage
    wire fwd_mem_rs1 = mem_reg_write && !mem_mem_read && (mem_rd != 5'd0) && (mem_rd == ex1_rs1) && !fwd_ex2_rs1;
    wire fwd_mem_rs2 = mem_reg_write && !mem_mem_read && (mem_rd != 5'd0) && (mem_rd == ex1_rs2) && !fwd_ex2_rs2;

    // Forwarding from WB stage
    wire fwd_wb_rs1  = wb_reg_write && !wb_mem_read && (wb_rd != 5'd0) && (wb_rd == ex1_rs1) && !fwd_ex2_rs1 && !fwd_mem_rs1;
    wire fwd_wb_rs2  = wb_reg_write && !wb_mem_read && (wb_rd != 5'd0) && (wb_rd == ex1_rs2) && !fwd_ex2_rs2 && !fwd_mem_rs2;

    wire [31:0] ex1_fwd_rs1 = fwd_ex2_rs1 ? ex2_result :
                              fwd_mem_rs1 ? mem_alu_result :
                              fwd_wb_rs1  ? wb_rd_data :
                              ex1_rs1_data;

    wire [31:0] ex1_fwd_rs2 = fwd_ex2_rs2 ? ex2_result :
                              fwd_mem_rs2 ? mem_alu_result :
                              fwd_wb_rs2  ? wb_rd_data :
                              ex1_rs2_data;

    // =========================================================================
    // EX1/EX2 Pipeline Register (registered ALU inputs)
    // =========================================================================

    logic [31:0] ex2_pc;
    logic [31:0] ex2_alu_a, ex2_alu_b;  // Registered ALU inputs
    logic [31:0] ex2_rs2_fwd;           // Forwarded rs2 for stores
    logic [31:0] ex2_imm;
    logic [3:0]  ex2_alu_op;
    logic        ex2_alu_src;
    logic        ex2_mem_write, ex2_branch;
    logic [2:0]  ex2_branch_op;
    logic        ex2_jump, ex2_jump_reg;
    logic        ex2_lui, ex2_auipc;
    logic        ex2_ebreak;
    logic [2:0]  ex2_mem_op;

    always_ff @(posedge clk) begin
        if (cpu_rst || flush) begin
            ex2_valid     <= 1'b0;
            ex2_reg_write <= 1'b0;
            ex2_mem_read  <= 1'b0;
            ex2_mem_write <= 1'b0;
            ex2_branch    <= 1'b0;
            ex2_jump      <= 1'b0;
            ex2_lui       <= 1'b0;
            ex2_auipc     <= 1'b0;
            ex2_ebreak    <= 1'b0;
        end else if (cpu_running && !hazard_load_data) begin
            ex2_pc        <= ex1_pc;
            ex2_alu_a     <= ex1_fwd_rs1;
            ex2_alu_b     <= ex1_alu_src ? ex1_imm : ex1_fwd_rs2;
            ex2_rs2_fwd   <= ex1_fwd_rs2;
            ex2_imm       <= ex1_imm;
            ex2_alu_op    <= ex1_alu_op;
            ex2_rd        <= ex1_rd;
            ex2_reg_write <= ex1_reg_write;
            ex2_mem_read  <= ex1_mem_read;
            ex2_mem_write <= ex1_mem_write;
            ex2_mem_op    <= ex1_mem_op;
            ex2_branch    <= ex1_branch && ex1_valid;
            ex2_branch_op <= ex1_branch_op;
            ex2_jump      <= ex1_jump && ex1_valid;
            ex2_jump_reg  <= ex1_jump_reg;
            ex2_lui       <= ex1_lui;
            ex2_auipc     <= ex1_auipc;
            ex2_ebreak    <= ex1_ebreak;
            ex2_valid     <= ex1_valid;
        end
    end

    // =========================================================================
    // Stage 4: EX2 (ALU Execution)
    // =========================================================================

    logic [31:0] ex2_alu_result;
    logic        ex2_alu_zero;
    logic        ex2_alu_lt;
    logic        ex2_alu_ltu;

    alu alu_inst (
        .a      (ex2_alu_a),
        .b      (ex2_alu_b),
        .op     (ex2_alu_op),
        .result (ex2_alu_result),
        .zero   (ex2_alu_zero),
        .lt     (ex2_alu_lt),
        .ltu    (ex2_alu_ltu)
    );

    // LUI/AUIPC result: imm for LUI, PC+imm for AUIPC
    wire [31:0] ex2_lui_auipc_result = ex2_auipc ? (ex2_pc + ex2_imm) : ex2_imm;

    // Select between ALU result and LUI/AUIPC result
    assign ex2_result = (ex2_lui || ex2_auipc) ? ex2_lui_auipc_result : ex2_alu_result;

    // =========================================================================
    // EX2/MEM Pipeline Register
    // =========================================================================

    logic [31:0] mem_store_data;
    logic        mem_alu_zero;
    logic        mem_alu_lt;
    logic        mem_alu_ltu;
    logic [31:0] mem_pc;
    logic [31:0] mem_imm;
    logic [2:0]  mem_branch_op;
    logic        mem_jump, mem_jump_reg;
    logic        mem_ebreak;
    logic [2:0]  mem_mem_op;

    always_ff @(posedge clk) begin
        if (cpu_rst || flush) begin
            mem_valid     <= 1'b0;
            mem_reg_write <= 1'b0;
            mem_mem_read  <= 1'b0;
            mem_mem_write <= 1'b0;
            mem_branch    <= 1'b0;
            mem_jump      <= 1'b0;
            mem_ebreak    <= 1'b0;
        end else if (cpu_running && !hazard_load_data) begin
            // Normal advance from EX2 to MEM
            mem_alu_result <= ex2_result;
            mem_store_data <= ex2_rs2_fwd;
            mem_rd         <= ex2_rd;
            mem_reg_write  <= ex2_reg_write;
            mem_mem_read   <= ex2_mem_read;
            mem_mem_write  <= ex2_mem_write;
            mem_mem_op     <= ex2_mem_op;
            mem_branch     <= ex2_branch && ex2_valid;
            mem_branch_op  <= ex2_branch_op;
            mem_jump       <= ex2_jump && ex2_valid;
            mem_jump_reg   <= ex2_jump_reg;
            mem_alu_zero   <= ex2_alu_zero;
            mem_alu_lt     <= ex2_alu_lt;
            mem_alu_ltu    <= ex2_alu_ltu;
            mem_pc         <= ex2_pc;
            mem_imm        <= ex2_imm;
            mem_ebreak     <= ex2_ebreak;
            mem_valid      <= ex2_valid;
        end
        // When hazard_load_data: MEM stage holds, waiting for DMEM data
    end

    // =========================================================================
    // Stage 5: MEM (Memory Access)
    // =========================================================================

    // Branch condition based on branch_op (funct3)
    // 000 = BEQ  (branch if equal)
    // 001 = BNE  (branch if not equal)
    // 100 = BLT  (branch if less than, signed)
    // 101 = BGE  (branch if greater or equal, signed)
    // 110 = BLTU (branch if less than, unsigned)
    // 111 = BGEU (branch if greater or equal, unsigned)
    logic branch_condition;
    always_comb begin
        case (mem_branch_op)
            3'b000:  branch_condition = mem_alu_zero;    // BEQ:  rs1 == rs2
            3'b001:  branch_condition = !mem_alu_zero;   // BNE:  rs1 != rs2
            3'b100:  branch_condition = mem_alu_lt;      // BLT:  rs1 < rs2 (signed)
            3'b101:  branch_condition = !mem_alu_lt;     // BGE:  rs1 >= rs2 (signed)
            3'b110:  branch_condition = mem_alu_ltu;     // BLTU: rs1 < rs2 (unsigned)
            3'b111:  branch_condition = !mem_alu_ltu;    // BGEU: rs1 >= rs2 (unsigned)
            default: branch_condition = 1'b0;
        endcase
    end

    // Jump target: JAL uses PC+imm, JALR uses rs1+imm (from ALU result)
    wire [31:0] mem_jump_target = mem_jump_reg ? mem_alu_result : (mem_pc + mem_imm);

    // Branch or jump taken
    assign mem_branch_taken  = ((mem_branch && branch_condition) || mem_jump) && mem_valid;
    assign mem_branch_target = mem_jump ? mem_jump_target : (mem_pc + mem_imm);

    // PC+4 for JAL/JALR to write to rd
    wire [31:0] mem_pc_plus4 = mem_pc + 32'd4;

    // Address low bits for byte/half selection
    wire [1:0] mem_addr_lo = mem_alu_result[1:0];

    // -------------------------------------------------------------------------
    // Store data and byte enables based on mem_op
    // mem_op: 000=SB, 001=SH, 010=SW
    // -------------------------------------------------------------------------
    logic [31:0] store_data_shifted;
    logic [3:0]  store_byte_enable;

    always_comb begin
        store_data_shifted = mem_store_data;
        store_byte_enable = 4'b1111;  // Default: all bytes (SW)

        case (mem_mem_op[1:0])
            2'b00: begin  // SB - store byte
                case (mem_addr_lo)
                    2'b00: begin store_data_shifted = {24'b0, mem_store_data[7:0]};       store_byte_enable = 4'b0001; end
                    2'b01: begin store_data_shifted = {16'b0, mem_store_data[7:0], 8'b0}; store_byte_enable = 4'b0010; end
                    2'b10: begin store_data_shifted = {8'b0, mem_store_data[7:0], 16'b0}; store_byte_enable = 4'b0100; end
                    2'b11: begin store_data_shifted = {mem_store_data[7:0], 24'b0};       store_byte_enable = 4'b1000; end
                endcase
            end
            2'b01: begin  // SH - store halfword
                case (mem_addr_lo[1])
                    1'b0: begin store_data_shifted = {16'b0, mem_store_data[15:0]};       store_byte_enable = 4'b0011; end
                    1'b1: begin store_data_shifted = {mem_store_data[15:0], 16'b0};       store_byte_enable = 4'b1100; end
                endcase
            end
            default: begin  // SW - store word
                store_data_shifted = mem_store_data;
                store_byte_enable = 4'b1111;
            end
        endcase
    end

    assign cpu_dmem_addr  = mem_alu_result;
    assign cpu_dmem_wdata = store_data_shifted;
    assign cpu_dmem_we    = mem_mem_write && mem_valid;
    assign cpu_dmem_be    = store_byte_enable;

    // -------------------------------------------------------------------------
    // Load data selection and sign extension
    // Load data selection moved to WB stage for better timing
    // Pass raw DMEM data through MEM/WB register
    wire [31:0] mem_load_data_raw = cpu_dmem_rdata;

    // =========================================================================
    // MEM/WB Pipeline Register
    // =========================================================================

    logic [31:0] wb_alu_result;
    logic [31:0] wb_load_data_raw;
    logic [2:0]  wb_mem_op;
    logic [1:0]  wb_addr_lo;
    logic        wb_jump;
    logic [31:0] wb_pc_plus4;

    always_ff @(posedge clk) begin
        if (cpu_rst) begin
            wb_valid     <= 1'b0;
            wb_reg_write <= 1'b0;
            wb_mem_read  <= 1'b0;
            wb_jump      <= 1'b0;
            wb_ebreak    <= 1'b0;
        end else if (cpu_running && !hazard_load_data) begin
            wb_alu_result    <= mem_alu_result;
            wb_load_data_raw <= mem_load_data_raw;
            wb_mem_op        <= mem_mem_op;
            wb_addr_lo       <= mem_addr_lo;
            wb_pc_plus4      <= mem_pc_plus4;
            wb_rd            <= mem_rd;
            wb_reg_write     <= mem_reg_write;
            wb_mem_read      <= mem_mem_read;
            wb_jump          <= mem_jump;
            wb_ebreak        <= mem_ebreak;
            wb_valid         <= mem_valid;
        end
    end

    // =========================================================================
    // Stage 6: WB (Write Back)
    // =========================================================================

    // Load byte/halfword selection and sign/zero extension (moved from MEM for timing)
    logic [31:0] wb_load_data;
    always_comb begin
        wb_load_data = wb_load_data_raw;  // Default: full word (LW)

        case (wb_mem_op)
            3'b000: begin  // LB - load byte signed
                case (wb_addr_lo)
                    2'b00: wb_load_data = {{24{wb_load_data_raw[7]}},  wb_load_data_raw[7:0]};
                    2'b01: wb_load_data = {{24{wb_load_data_raw[15]}}, wb_load_data_raw[15:8]};
                    2'b10: wb_load_data = {{24{wb_load_data_raw[23]}}, wb_load_data_raw[23:16]};
                    2'b11: wb_load_data = {{24{wb_load_data_raw[31]}}, wb_load_data_raw[31:24]};
                endcase
            end
            3'b001: begin  // LH - load halfword signed
                case (wb_addr_lo[1])
                    1'b0: wb_load_data = {{16{wb_load_data_raw[15]}}, wb_load_data_raw[15:0]};
                    1'b1: wb_load_data = {{16{wb_load_data_raw[31]}}, wb_load_data_raw[31:16]};
                endcase
            end
            3'b010: begin  // LW - load word
                wb_load_data = wb_load_data_raw;
            end
            3'b100: begin  // LBU - load byte unsigned
                case (wb_addr_lo)
                    2'b00: wb_load_data = {24'b0, wb_load_data_raw[7:0]};
                    2'b01: wb_load_data = {24'b0, wb_load_data_raw[15:8]};
                    2'b10: wb_load_data = {24'b0, wb_load_data_raw[23:16]};
                    2'b11: wb_load_data = {24'b0, wb_load_data_raw[31:24]};
                endcase
            end
            3'b101: begin  // LHU - load halfword unsigned
                case (wb_addr_lo[1])
                    1'b0: wb_load_data = {16'b0, wb_load_data_raw[15:0]};
                    1'b1: wb_load_data = {16'b0, wb_load_data_raw[31:16]};
                endcase
            end
            default: wb_load_data = wb_load_data_raw;
        endcase
    end

    // Select write-back data: PC+4 for jumps, load data for loads, ALU result otherwise
    assign wb_rd_data = wb_jump     ? wb_pc_plus4 :
                        wb_mem_read ? wb_load_data :
                        wb_alu_result;

    // =========================================================================
    // CPU Logger (logs CPU memory accesses for debug)
    // =========================================================================
    //
    // Accessible at addr 0x5xxx:
    //   0x5000 = log_count (RO), 0x5004 = log_cycle (RO)
    //   0x5008 = control: [0]=enable, [1]=clear
    //   0x5010 = entry[0] bits[31:0],  0x5014 = entry[0] bits[63:32], 0x5018 = entry[0] bits[95:64]
    //   0x5020 = entry[1] bits[31:0],  etc.

    // CPU logger control register
    // Control bits: [0]=enable, [1]=clear (auto-clears), [2]=log_imem
    logic cpulog_enable;
    logic cpulog_log_imem;    // When 0, only log DMEM accesses (default)
    logic cpulog_clear_req;   // Request from host write
    logic cpulog_clear;       // Actual clear signal (delayed by 1 cycle)

    always_ff @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            cpulog_enable <= 1'b1;    // Enabled by default
            cpulog_log_imem <= 1'b0;  // DMEM-only by default (avoids filling buffer with fetches)
            cpulog_clear_req <= 1'b0;
            cpulog_clear <= 1'b0;
        end else begin
            // Delay the clear request by one cycle so cpu_logger sees it
            cpulog_clear <= cpulog_clear_req;
            cpulog_clear_req <= 1'b0;  // Auto-clear request

            if (wen && addr[19:12] == 8'h50 && addr[7:0] == 8'h08) begin
                cpulog_enable <= wdata[0];
                cpulog_clear_req <= wdata[1];
                cpulog_log_imem <= wdata[2];
            end
        end
    end

    wire [4:0]  cpu_log_idx = addr[8:4] - 5'd1;  // Entry 0 at 0x5010, entry 1 at 0x5020
    wire [95:0] cpu_log_entry;
    wire [31:0] cpu_log_count;
    wire [31:0] cpu_log_cycle;

    cpu_logger #(
        .LOG_DEPTH(32)
    ) u_cpu_logger (
        .clk        (clk),
        .rst_n      (rst_n),

        // Control
        .log_enable (cpulog_enable),
        .log_clear  (cpulog_clear),
        .log_imem   (cpulog_log_imem),

        // IMEM fetch
        .imem_addr  (if_pc),
        .imem_rdata (if_instr),
        .imem_valid (cpu_running && !stall),

        // DMEM access
        .dmem_addr  (cpu_dmem_addr),
        .dmem_wdata (cpu_dmem_wdata),
        .dmem_rdata (cpu_dmem_rdata),
        .dmem_wen   (cpu_dmem_we),
        .dmem_ren   (mem_mem_read && mem_valid && cpu_running),

        // Log read interface
        .log_idx    (cpu_log_idx),
        .log_entry  (cpu_log_entry),
        .log_count  (cpu_log_count),
        .log_cycle  (cpu_log_cycle)
    );

    // CPU logger read data mux
    logic [31:0] cpu_logger_rdata;
    always_comb begin
        if (addr[7:4] == 4'h0) begin
            // Control registers at 0x5000-0x500F
            case (addr[3:2])
                2'd0: cpu_logger_rdata = cpu_log_count;                                   // 0x5000
                2'd1: cpu_logger_rdata = cpu_log_cycle;                                   // 0x5004
                2'd2: cpu_logger_rdata = {29'b0, cpulog_log_imem, 1'b0, cpulog_enable};   // 0x5008
                default: cpu_logger_rdata = 32'h0;
            endcase
        end else begin
            // Log entries at 0x5010, 0x5020, etc.
            case (addr[3:2])
                2'd0: cpu_logger_rdata = cpu_log_entry[31:0];
                2'd1: cpu_logger_rdata = cpu_log_entry[63:32];
                2'd2: cpu_logger_rdata = cpu_log_entry[95:64];
                default: cpu_logger_rdata = 32'h0;
            endcase
        end
    end

endmodule
