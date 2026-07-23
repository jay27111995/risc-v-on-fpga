// RISC-V SoC with PCIe BAR Interface
// ============================================================================
//
// A minimal RV32I CPU with classic 5-stage pipeline, designed for FPGA at 500MHz.
//
// Pipeline Stages:
//   IF  - Instruction Fetch: Read instruction from IMEM
//   ID  - Instruction Decode: Decode opcode, read register file
//   EX  - Execute: ALU operation, branch decision
//   MEM - Memory: Load/store access to DMEM
//   WB  - Write Back: Write result to register file
//
// Hazard Handling:
//   - Data forwarding from MEM and WB stages to EX stage
//   - Load-use stall: 1 cycle when a load is followed by dependent instruction
//   - Branch penalty: 2 cycles (flush IF/ID and ID/EX on taken branch)
//
// Memory Map (directly mapped to PCIe BAR0):
//   0x0000-0x00FF  Control registers (64-bit aligned)
//   0x1000-0x1FFF  IMEM - 4KB instruction memory (1024 x 32-bit)
//   0x2000-0x3FFF  DMEM - 8KB data memory (2048 x 32-bit)
//
// Control Registers:
//   0x00  CTRL    [0] RUN - enable CPU execution
//                 [1] RESET - software reset (self-clearing)
//   0x08  STATUS  [0] RUNNING - CPU is executing
//   0x10  PC      Current program counter (read-only)
//   0x18  RESULT  Last write-back value (read-only, for debug)
//
// ============================================================================

module riscv_soc (
    input  logic        clk,
    input  logic        rst_n,
    
    // BAR interface (directly from AXI wrapper)
    input  logic [15:0] bar_addr,      // Byte address within BAR
    input  logic [63:0] bar_wdata,     // Write data (64-bit)
    input  logic        bar_wen,       // Write enable
    output logic [63:0] bar_rdata      // Read data (64-bit)
);

    // =========================================================================
    // Control Registers
    // =========================================================================
    
    logic        ctrl_run;          // CPU run enable
    logic        ctrl_reset;        // Software reset (self-clearing)
    logic [31:0] cpu_pc;            // Current PC (for status readback)
    logic [31:0] cpu_result;        // Last WB result (for debug)
    
    // Internal control signals
    logic cpu_rst;                  // Combined reset (hardware OR software)
    logic cpu_running;              // CPU is actively executing
    
    assign cpu_rst = ~rst_n | ctrl_reset;
    assign cpu_running = ctrl_run & ~ctrl_reset;
    
    // Control register write logic
    always_ff @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            ctrl_run <= 1'b0;
            ctrl_reset <= 1'b0;
        end else begin
            // Self-clear the reset bit after one cycle
            if (ctrl_reset)
                ctrl_reset <= 1'b0;
            
            // Write to CTRL register at offset 0x00
            if (bar_wen && bar_addr[15:12] == 4'h0 && bar_addr[7:3] == 5'd0) begin
                ctrl_run   <= bar_wdata[0];
                ctrl_reset <= bar_wdata[1];
            end
        end
    end

    // =========================================================================
    // IMEM - Instruction Memory (4KB, host-writable, CPU-readable)
    // =========================================================================
    
    logic [31:0] imem [0:1023];      // 1024 x 32-bit = 4KB
    logic [31:0] imem_host_rdata;    // Registered read for host access
    
    // Host write port
    always_ff @(posedge clk) begin
        if (bar_wen && bar_addr[15:12] == 4'h1) begin
            imem[bar_addr[11:2]] <= bar_wdata[31:0];
        end
    end
    
    // Host read port (registered for timing)
    always_ff @(posedge clk) begin
        imem_host_rdata <= imem[bar_addr[11:2]];
    end
    
    // Initialize to NOPs (ADDI x0, x0, 0)
    initial begin
        for (int i = 0; i < 1024; i++)
            imem[i] = 32'h00000013;
    end
    
    // =========================================================================
    // DMEM - Data Memory (8KB, shared between host and CPU)
    // =========================================================================
    
    logic [31:0] dmem [0:2047];      // 2048 x 32-bit = 8KB
    logic [31:0] dmem_host_rdata;    // Registered read for host access
    
    // CPU port signals
    logic [31:0] cpu_dmem_addr;      // Address from MEM stage
    logic [31:0] cpu_dmem_wdata;     // Write data from MEM stage
    logic [31:0] cpu_dmem_rdata;     // Read data to MEM stage
    logic        cpu_dmem_we;        // Write enable from MEM stage
    
    // Address indexing
    wire [10:0] host_dmem_idx = bar_addr[12:2];
    wire [10:0] cpu_dmem_idx  = cpu_dmem_addr[12:2];
    
    // Dual-port memory: host has priority over CPU for writes
    always_ff @(posedge clk) begin
        if (bar_wen && bar_addr[15:13] == 3'b001) begin
            // Host write (addresses 0x2000-0x3FFF)
            dmem[host_dmem_idx] <= bar_wdata[31:0];
        end else if (cpu_dmem_we && cpu_running) begin
            // CPU write
            dmem[cpu_dmem_idx] <= cpu_dmem_wdata;
        end
    end
    
    // CPU read port (combinational for same-cycle read in MEM stage)
    assign cpu_dmem_rdata = dmem[cpu_dmem_idx];
    
    // Host read port (registered for timing)
    always_ff @(posedge clk) begin
        dmem_host_rdata <= dmem[host_dmem_idx];
    end
    
    // Initialize to zero
    initial begin
        for (int i = 0; i < 2048; i++)
            dmem[i] = 32'h0;
    end
    
    // =========================================================================
    // BAR Read Multiplexer
    // =========================================================================
    
    always_comb begin
        bar_rdata = 64'h0;
        
        case (bar_addr[15:12])
            4'h0: begin  // Control registers
                case (bar_addr[7:3])
                    5'd0: bar_rdata = {62'b0, ctrl_reset, ctrl_run};  // CTRL
                    5'd1: bar_rdata = {63'b0, cpu_running};           // STATUS
                    5'd2: bar_rdata = {32'b0, cpu_pc};                // PC
                    5'd3: bar_rdata = {32'b0, cpu_result};            // RESULT
                    default: bar_rdata = 64'h0;
                endcase
            end
            4'h1:        bar_rdata = {32'b0, imem_host_rdata};  // IMEM
            4'h2, 4'h3:  bar_rdata = {32'b0, dmem_host_rdata};  // DMEM
            default:     bar_rdata = 64'h0;
        endcase
    end

    // =========================================================================
    // Pipeline Hazard Control
    // =========================================================================
    
    logic stall;    // Stall IF and ID (insert bubble in EX)
    logic flush;    // Flush IF/ID and ID/EX (branch misprediction)
    
    // Forward declarations for hazard detection
    logic        ex_mem_read;
    logic        ex_valid;
    logic [4:0]  ex_rd;
    logic [4:0]  id_rs1, id_rs2;
    logic        ex_branch_taken;

    // Load-use hazard: instruction in EX is a load, and ID needs that register
    assign stall = ex_mem_read && ex_valid && (ex_rd != 5'd0) &&
                   ((ex_rd == id_rs1) || (ex_rd == id_rs2));
    
    // Control hazard: branch taken, flush the two instructions behind it
    assign flush = ex_branch_taken;

    // =========================================================================
    // Stage 1: IF (Instruction Fetch)
    // =========================================================================
    
    logic [31:0] if_pc;             // Current PC
    logic [31:0] if_instr;          // Fetched instruction
    
    // Branch target from EX stage (forward declaration resolved below)
    logic [31:0] ex_branch_target;
    
    // Next PC selection
    wire [31:0] if_pc_next = ex_branch_taken ? ex_branch_target : (if_pc + 32'd4);
    
    // PC register
    always_ff @(posedge clk) begin
        if (cpu_rst) begin
            if_pc <= 32'h0;
        end else if (cpu_running && !stall) begin
            if_pc <= if_pc_next;
        end
    end
    
    // Instruction fetch (combinational IMEM read)
    assign if_instr = imem[if_pc[11:2]];
    
    // Export PC for debug/status
    assign cpu_pc = if_pc;

    // =========================================================================
    // IF/ID Pipeline Register
    // =========================================================================
    
    logic [31:0] id_pc;
    logic [31:0] id_instr;
    logic        id_valid;
    
    always_ff @(posedge clk) begin
        if (cpu_rst || flush) begin
            // Insert bubble (NOP)
            id_valid <= 1'b0;
            id_instr <= 32'h00000013;  // NOP
        end else if (cpu_running && !stall) begin
            id_pc    <= if_pc;
            id_instr <= if_instr;
            id_valid <= 1'b1;
        end
        // When stalled: hold current values (implicit)
    end

    // =========================================================================
    // Stage 2: ID (Instruction Decode + Register Read)
    // =========================================================================
    
    // Decoder outputs
    logic [4:0]  id_rd;
    logic [31:0] id_imm;
    logic [2:0]  id_alu_op;
    logic        id_reg_write;
    logic        id_alu_src;
    logic        id_mem_read;
    logic        id_mem_write;
    logic        id_branch;
    
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
        .branch    (id_branch)
    );
    
    // Register file
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
    logic [2:0]  ex_alu_op;
    logic [4:0]  ex_rs1, ex_rs2;
    logic        ex_reg_write;
    logic        ex_alu_src;
    logic        ex_mem_write;
    logic        ex_branch;
    
    always_ff @(posedge clk) begin
        if (cpu_rst || flush || stall) begin
            // Insert bubble
            ex_valid     <= 1'b0;
            ex_reg_write <= 1'b0;
            ex_mem_read  <= 1'b0;
            ex_mem_write <= 1'b0;
            ex_branch    <= 1'b0;
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
            ex_branch    <= id_branch    && id_valid;
            ex_valid     <= id_valid;
        end
    end

    // =========================================================================
    // Stage 3: EX (Execute)
    // =========================================================================
    
    // --- Data Forwarding Logic ---
    // Forward from MEM stage (has priority - more recent result)
    logic [31:0] mem_alu_result;
    logic        mem_reg_write;
    logic [4:0]  mem_rd;
    
    wire fwd_mem_rs1 = mem_reg_write && (mem_rd != 5'd0) && (mem_rd == ex_rs1);
    wire fwd_mem_rs2 = mem_reg_write && (mem_rd != 5'd0) && (mem_rd == ex_rs2);
    
    // Forward from WB stage (only if MEM isn't forwarding)
    wire fwd_wb_rs1 = wb_reg_write && (wb_rd != 5'd0) && (wb_rd == ex_rs1) && !fwd_mem_rs1;
    wire fwd_wb_rs2 = wb_reg_write && (wb_rd != 5'd0) && (wb_rd == ex_rs2) && !fwd_mem_rs2;
    
    // Forwarding muxes
    wire [31:0] ex_fwd_rs1 = fwd_mem_rs1 ? mem_alu_result :
                            fwd_wb_rs1  ? wb_rd_data     :
                            ex_rs1_data;
    
    wire [31:0] ex_fwd_rs2 = fwd_mem_rs2 ? mem_alu_result :
                            fwd_wb_rs2  ? wb_rd_data     :
                            ex_rs2_data;
    
    // ALU operand selection
    wire [31:0] ex_alu_a = ex_fwd_rs1;
    wire [31:0] ex_alu_b = ex_alu_src ? ex_imm : ex_fwd_rs2;
    
    // ALU instance
    logic [31:0] ex_alu_result;
    logic        ex_alu_zero;
    
    alu alu_inst (
        .a      (ex_alu_a),
        .b      (ex_alu_b),
        .op     (ex_alu_op),
        .result (ex_alu_result),
        .zero   (ex_alu_zero)
    );
    
    // Branch decision (BEQ: branch if rs1 == rs2)
    assign ex_branch_taken  = ex_branch && ex_alu_zero && ex_valid;
    assign ex_branch_target = ex_pc + ex_imm;

    // =========================================================================
    // EX/MEM Pipeline Register
    // =========================================================================
    
    logic [31:0] mem_store_data;    // Data to store (forwarded rs2)
    logic        mem_mem_read;
    logic        mem_mem_write;
    logic        mem_valid;
    
    always_ff @(posedge clk) begin
        if (cpu_rst) begin
            mem_valid     <= 1'b0;
            mem_reg_write <= 1'b0;
            mem_mem_read  <= 1'b0;
            mem_mem_write <= 1'b0;
        end else if (cpu_running) begin
            mem_alu_result <= ex_alu_result;
            mem_store_data <= ex_fwd_rs2;      // Use forwarded value for stores
            mem_rd         <= ex_rd;
            mem_reg_write  <= ex_reg_write;
            mem_mem_read   <= ex_mem_read;
            mem_mem_write  <= ex_mem_write;
            mem_valid      <= ex_valid;
        end
    end

    // =========================================================================
    // Stage 4: MEM (Memory Access)
    // =========================================================================
    
    // Connect to DMEM
    assign cpu_dmem_addr  = mem_alu_result;
    assign cpu_dmem_wdata = mem_store_data;
    assign cpu_dmem_we    = mem_mem_write && mem_valid;
    
    // Memory read data (combinational - available same cycle)
    wire [31:0] mem_load_data = cpu_dmem_rdata;

    // =========================================================================
    // MEM/WB Pipeline Register
    // =========================================================================
    
    logic [31:0] wb_alu_result;
    logic [31:0] wb_load_data;
    logic        wb_mem_read;
    logic        wb_valid;
    
    always_ff @(posedge clk) begin
        if (cpu_rst) begin
            wb_valid     <= 1'b0;
            wb_reg_write <= 1'b0;
        end else if (cpu_running) begin
            wb_alu_result <= mem_alu_result;
            wb_load_data  <= mem_load_data;
            wb_rd         <= mem_rd;
            wb_reg_write  <= mem_reg_write;
            wb_mem_read   <= mem_mem_read;
            wb_valid      <= mem_valid;
        end
    end

    // =========================================================================
    // Stage 5: WB (Write Back)
    // =========================================================================
    
    // Select between ALU result and memory load data
    assign wb_rd_data = wb_mem_read ? wb_load_data : wb_alu_result;
    
    // Debug output: last value written to register file
    assign cpu_result = wb_rd_data;

endmodule
