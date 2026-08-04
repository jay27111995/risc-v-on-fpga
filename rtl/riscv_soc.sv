// RISC-V SoC
// ============================================================================
//
// A minimal RV32I CPU with classic 5-stage pipeline.
//
// Memory Map:
//   0x0000-0x00FF  Control registers
//   0x1000-0x1FFF  IMEM - 4KB instruction memory (32-bit)
//   0x2000-0x3FFF  DMEM - 8KB data memory (32-bit)
//   0x4000-0x4FFF  Bus sniffer logs (in axi_core_hw)
//   0x5000-0x5FFF  CPU logger logs
//
// Control Registers:
//   0x00  CTRL    [0] RUN, [1] RESET
//   0x08  STATUS  [0] RUNNING
//   0x10  PC      Current program counter
//
// CPU Logger Registers (0x5xxx):
//   0x5000  LOG_COUNT   Total transactions logged
//   0x5004  LOG_CYCLE   Current cycle count
//   0x5010  ENTRY[0]    Newest log entry (3 words: data, addr, timestamp|type)
//   0x5020  ENTRY[1]    Second newest, etc.
//
// ============================================================================

module riscv_soc (
    input  logic        clk,
    input  logic        rst_n,
    
    // Host interface (32-bit, from bus64to32 adapter)
    input  logic [15:0] addr,
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
    logic [31:0] cpu_pc;
    
    logic cpu_rst;
    logic cpu_running;
    
    assign cpu_rst = ~rst_n | ctrl_reset;
    assign cpu_running = ctrl_run & ~ctrl_reset;
    
    always_ff @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            ctrl_run <= 1'b0;
            ctrl_reset <= 1'b0;
        end else begin
            if (ctrl_reset)
                ctrl_reset <= 1'b0;
            
            if (wen && addr[15:12] == 4'h0 && addr[7:2] == 6'd0) begin
                ctrl_run   <= wdata[0];
                ctrl_reset <= wdata[1];
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
        end else if (cpu_rst || (wen && addr[15:12] == 4'h0 && addr[7:2] == 6'd8)) begin
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
    
    logic [31:0] imem [0:1023];
    logic [31:0] imem_host_rdata;
    
    always_ff @(posedge clk) begin
        if (wen && addr[15:12] == 4'h1)
            imem[addr[11:2]] <= wdata;
    end
    
    always_ff @(posedge clk) begin
        if (ren && addr[15:12] == 4'h1)
            imem_host_rdata <= imem[addr[11:2]];
    end
    
    initial begin
        for (int i = 0; i < 1024; i++)
            imem[i] = 32'h00000013;  // NOP
    end
    
    // =========================================================================
    // DMEM - Data Memory (8KB, 32-bit)
    // =========================================================================
    
    logic [31:0] dmem [0:2047];
    
    // CPU port signals
    logic [31:0] cpu_dmem_addr;
    logic [31:0] cpu_dmem_wdata;
    logic [31:0] cpu_dmem_rdata;
    logic        cpu_dmem_we;
    logic [3:0]  cpu_dmem_be;  // Byte enables for byte/half stores
    
    // Address indexing (word-aligned, 32-bit entries)
    wire [10:0] cpu_dmem_idx  = cpu_dmem_addr[12:2];
    wire [10:0] host_dmem_idx = addr[12:2];
    
    // Write enables
    wire host_dmem_wen = wen && (addr[15:12] == 4'h2 || addr[15:12] == 4'h3);
    wire cpu_dmem_wen  = cpu_dmem_we && cpu_running && !host_dmem_wen;
    
    // Single write port with byte enables (Quartus requirement)
    always_ff @(posedge clk) begin
        if (host_dmem_wen) begin
            dmem[host_dmem_idx] <= wdata;
        end else if (cpu_dmem_wen) begin
            // Byte-wise write based on byte enables
            if (cpu_dmem_be[0]) dmem[cpu_dmem_idx][7:0]   <= cpu_dmem_wdata[7:0];
            if (cpu_dmem_be[1]) dmem[cpu_dmem_idx][15:8]  <= cpu_dmem_wdata[15:8];
            if (cpu_dmem_be[2]) dmem[cpu_dmem_idx][23:16] <= cpu_dmem_wdata[23:16];
            if (cpu_dmem_be[3]) dmem[cpu_dmem_idx][31:24] <= cpu_dmem_wdata[31:24];
        end
    end
    
    // CPU read (combinational)
    assign cpu_dmem_rdata = dmem[cpu_dmem_idx];
    
    // Host read (registered)
    logic [31:0] dmem_host_rdata;
    always_ff @(posedge clk) begin
        if (ren && (addr[15:12] == 4'h2 || addr[15:12] == 4'h3))
            dmem_host_rdata <= dmem[host_dmem_idx];
    end
    
    initial begin
        for (int i = 0; i < 2048; i++)
            dmem[i] = 32'h0;
    end
    
    // =========================================================================
    // Host Read Multiplexer
    // =========================================================================

    always_comb begin
        rdata = 32'h0;
        case (addr[15:12])
            4'h0: begin
                case (addr[7:2])
                    6'd0:  rdata = {30'b0, ctrl_reset, ctrl_run};  // 0x00 CTRL
                    6'd2:  rdata = {31'b0, cpu_running};           // 0x08 STATUS
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
            4'h1:        rdata = imem_host_rdata;
            4'h2, 4'h3:  rdata = dmem_host_rdata;
            4'h5:        rdata = cpu_logger_rdata;  // CPU logger
            default:     rdata = 32'h0;
        endcase
    end

    // =========================================================================
    // Pipeline Hazard Control
    // =========================================================================
    
    logic stall;
    logic flush;
    
    // Forward declarations for hazard detection and performance counters
    logic        ex_mem_read, ex_valid;
    logic [4:0]  ex_rd, id_rs1, id_rs2;
    logic        id_valid;
    logic [31:0] mem_branch_target;
    logic [4:0]  mem_rd, ex_rs1, ex_rs2;

    // Load-use hazard: stall for 2 cycles when a load is followed by dependent instruction
    // Cycle 1: Load in EX, dependent in ID -> stall
    // Cycle 2: Load in MEM, dependent still in ID -> stall again
    // Cycle 3: Load in WB (writes to regfile), dependent in EX (reads from regfile)
    // This avoids the long combinational path through the load byte/half mux in WB
    wire hazard_load_use_ex  = ex_mem_read && ex_valid && (ex_rd != 5'd0) &&
                               ((ex_rd == id_rs1) || (ex_rd == id_rs2)) && id_valid;
    wire hazard_load_use_mem = mem_mem_read && mem_valid && (mem_rd != 5'd0) &&
                               ((mem_rd == id_rs1) || (mem_rd == id_rs2)) && id_valid;
    
    assign stall = hazard_load_use_ex || hazard_load_use_mem;
    assign flush = mem_branch_taken;

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
    
    assign if_instr = imem[if_pc[11:2]];
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
        .auipc     (id_auipc)
    );
    
    logic [31:0] id_rs1_data, id_rs2_data;
    logic [31:0] wb_rd_data;
    logic        wb_reg_write;
    logic [4:0]  wb_rd;
    
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
    // ID/EX Pipeline Register
    // =========================================================================
    
    logic [31:0] ex_pc;
    logic [31:0] ex_rs1_data, ex_rs2_data;
    logic [31:0] ex_imm;
    logic [3:0]  ex_alu_op;
    logic        ex_reg_write, ex_alu_src, ex_mem_write, ex_branch;
    logic [2:0]  ex_branch_op;
    logic        ex_jump, ex_jump_reg;
    logic        ex_lui, ex_auipc;
    logic [2:0]  ex_mem_op;
    
    always_ff @(posedge clk) begin
        if (cpu_rst || flush || stall) begin
            ex_valid     <= 1'b0;
            ex_reg_write <= 1'b0;
            ex_mem_read  <= 1'b0;
            ex_mem_write <= 1'b0;
            ex_branch    <= 1'b0;
            ex_jump      <= 1'b0;
            ex_lui       <= 1'b0;
            ex_auipc     <= 1'b0;
        end else if (cpu_running) begin
            ex_pc        <= id_pc;
            ex_rs1_data  <= id_rs1_data;
            ex_rs2_data  <= id_rs2_data;
            ex_imm       <= id_imm;
            ex_alu_op    <= id_alu_op;
            ex_rs1       <= id_rs1;
            ex_rs2       <= id_rs2;
            ex_rd        <= id_rd;
            ex_reg_write <= id_reg_write && id_valid;
            ex_alu_src   <= id_alu_src;
            ex_mem_read  <= id_mem_read  && id_valid;
            ex_mem_write <= id_mem_write && id_valid;
            ex_mem_op    <= id_mem_op;
            ex_branch    <= id_branch    && id_valid;
            ex_branch_op <= id_branch_op;
            ex_jump      <= id_jump      && id_valid;
            ex_jump_reg  <= id_jump_reg;
            ex_lui       <= id_lui       && id_valid;
            ex_auipc     <= id_auipc     && id_valid;
            ex_valid     <= id_valid;
        end
    end

    // =========================================================================
    // Stage 3: EX (Execute)
    // =========================================================================
    
    logic [31:0] mem_alu_result;
    logic        mem_reg_write;
    // Forwarding from MEM stage (non-loads only, loads use the stall + regfile path)
    wire fwd_mem_rs1 = mem_reg_write && !mem_mem_read && (mem_rd != 5'd0) && (mem_rd == ex_rs1);
    wire fwd_mem_rs2 = mem_reg_write && !mem_mem_read && (mem_rd != 5'd0) && (mem_rd == ex_rs2);
    // No WB forwarding for loads - we stall 2 cycles so value goes through regfile
    // This avoids the long combinational path through the load byte/half mux
    wire fwd_wb_rs1  = wb_reg_write && !wb_mem_read && (wb_rd != 5'd0) && (wb_rd == ex_rs1) && !fwd_mem_rs1;
    wire fwd_wb_rs2  = wb_reg_write && !wb_mem_read && (wb_rd != 5'd0) && (wb_rd == ex_rs2) && !fwd_mem_rs2;
    
    wire [31:0] ex_fwd_rs1 = fwd_mem_rs1 ? mem_alu_result : fwd_wb_rs1 ? wb_rd_data : ex_rs1_data;
    wire [31:0] ex_fwd_rs2 = fwd_mem_rs2 ? mem_alu_result : fwd_wb_rs2 ? wb_rd_data : ex_rs2_data;
    
    wire [31:0] ex_alu_a = ex_fwd_rs1;
    wire [31:0] ex_alu_b = ex_alu_src ? ex_imm : ex_fwd_rs2;
    
    logic [31:0] ex_alu_result;
    logic        ex_alu_zero;
    logic        ex_alu_lt;    // signed less-than
    logic        ex_alu_ltu;   // unsigned less-than
    
    alu alu_inst (
        .a      (ex_alu_a),
        .b      (ex_alu_b),
        .op     (ex_alu_op),
        .result (ex_alu_result),
        .zero   (ex_alu_zero),
        .lt     (ex_alu_lt),
        .ltu    (ex_alu_ltu)
    );
    
    // LUI/AUIPC result: imm for LUI, PC+imm for AUIPC
    wire [31:0] ex_lui_auipc_result = ex_auipc ? (ex_pc + ex_imm) : ex_imm;
    
    // Select between ALU result and LUI/AUIPC result
    wire [31:0] ex_result = (ex_lui || ex_auipc) ? ex_lui_auipc_result : ex_alu_result;

    // =========================================================================
    // EX/MEM Pipeline Register
    // =========================================================================
    
    logic [31:0] mem_store_data;
    // mem_mem_write, mem_branch declared in forward declarations
    logic        mem_alu_zero;
    logic        mem_alu_lt;   // signed less-than
    logic        mem_alu_ltu;  // unsigned less-than
    logic [31:0] mem_pc;
    logic [31:0] mem_imm;
    logic [2:0]  mem_branch_op;
    logic        mem_jump, mem_jump_reg;
    logic [2:0]  mem_mem_op;
    
    always_ff @(posedge clk) begin
        if (cpu_rst || flush) begin
            mem_valid     <= 1'b0;
            mem_reg_write <= 1'b0;
            mem_mem_read  <= 1'b0;
            mem_mem_write <= 1'b0;
            mem_branch    <= 1'b0;
            mem_jump      <= 1'b0;
        end else if (cpu_running) begin
            mem_alu_result <= ex_result;  // Use ex_result (includes LUI/AUIPC)
            mem_store_data <= ex_fwd_rs2;
            mem_rd         <= ex_rd;
            mem_reg_write  <= ex_reg_write;
            mem_mem_read   <= ex_mem_read;
            mem_mem_write  <= ex_mem_write;
            mem_mem_op     <= ex_mem_op;
            mem_branch     <= ex_branch && ex_valid;
            mem_branch_op  <= ex_branch_op;
            mem_jump       <= ex_jump && ex_valid;
            mem_jump_reg   <= ex_jump_reg;
            mem_alu_zero   <= ex_alu_zero;
            mem_alu_lt     <= ex_alu_lt;
            mem_alu_ltu    <= ex_alu_ltu;
            mem_pc         <= ex_pc;
            mem_imm        <= ex_imm;
            mem_valid      <= ex_valid;
        end
    end

    // =========================================================================
    // Stage 4: MEM (Memory Access)
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
    logic        wb_mem_read;
    logic        wb_jump;
    logic [31:0] wb_pc_plus4;
    // wb_valid declared in forward declarations
    
    always_ff @(posedge clk) begin
        if (cpu_rst) begin
            wb_valid     <= 1'b0;
            wb_reg_write <= 1'b0;
            wb_mem_read  <= 1'b0;
            wb_jump      <= 1'b0;
        end else if (cpu_running) begin
            wb_alu_result    <= mem_alu_result;
            wb_load_data_raw <= mem_load_data_raw;
            wb_mem_op        <= mem_mem_op;
            wb_addr_lo       <= mem_addr_lo;
            wb_pc_plus4      <= mem_pc_plus4;
            wb_rd            <= mem_rd;
            wb_reg_write     <= mem_reg_write;
            wb_mem_read      <= mem_mem_read;
            wb_jump          <= mem_jump;
            wb_valid         <= mem_valid;
        end
    end

    // =========================================================================
    // Stage 5: WB (Write Back)
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
    logic cpulog_enable;
    logic cpulog_clear_req;   // Request from host write
    logic cpulog_clear;       // Actual clear signal (delayed by 1 cycle)
    
    always_ff @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            cpulog_enable <= 1'b1;  // Enabled by default
            cpulog_clear_req <= 1'b0;
            cpulog_clear <= 1'b0;
        end else begin
            // Delay the clear request by one cycle so cpu_logger sees it
            cpulog_clear <= cpulog_clear_req;
            cpulog_clear_req <= 1'b0;  // Auto-clear request
            
            if (wen && addr[15:12] == 4'h5 && addr[7:0] == 8'h08) begin
                cpulog_enable <= wdata[0];
                cpulog_clear_req <= wdata[1];
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
            // Control registers
            case (addr[3:2])
                2'd0: cpu_logger_rdata = cpu_log_count;
                2'd1: cpu_logger_rdata = cpu_log_cycle;
                2'd2: cpu_logger_rdata = {30'b0, 1'b0, cpulog_enable};  // control
                default: cpu_logger_rdata = 32'h0;
            endcase
        end else begin
            // Log entries
            case (addr[3:2])
                2'd0: cpu_logger_rdata = cpu_log_entry[31:0];
                2'd1: cpu_logger_rdata = cpu_log_entry[63:32];
                2'd2: cpu_logger_rdata = cpu_log_entry[95:64];
                default: cpu_logger_rdata = 32'h0;
            endcase
        end
    end

endmodule
