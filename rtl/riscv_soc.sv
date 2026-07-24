// RISC-V SoC with PCIe BAR Interface
// ============================================================================
//
// A minimal RV32I CPU with classic 5-stage pipeline, designed for FPGA at 500MHz.
//
// Pipeline Stages:
//   IF  - Instruction Fetch: Read instruction from IMEM
//   ID  - Instruction Decode: Decode opcode, read register file
//   EX  - Execute: ALU operation, compute branch condition
//   MEM - Memory: Load/store access to DMEM, branch resolution
//   WB  - Write Back: Write result to register file
//
// Hazard Handling:
//   - Data forwarding from MEM and WB stages to EX stage
//   - Load-use stall: detected early and registered for clean timing
//   - Branch penalty: 3 cycles (flush IF/ID/EX when branch taken in MEM)
//
// Timing Optimizations for 500MHz:
//   - Branch resolved in MEM stage (not EX) to break ALU→branch→flush path
//   - Stall signal registered (early hazard detection) to break stall→enable path
//   - No forwarding of load data from WB (uses register file bypass + stall)
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
    input  logic        bar_ren,       // Read enable (captures read data)
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
    logic [31:0] imem_host_rdata;    // Registered read for host access (even word)
    logic [31:0] imem_host_rdata_hi; // Registered read for host access (odd word)
    
    // Registered write signals for timing
    logic        imem_wen_r;
    logic [8:0]  imem_waddr_r;       // Pair index (bar_addr[11:3])
    logic [31:0] imem_wdata_lo_r;
    logic [31:0] imem_wdata_hi_r;
    
    // Register write request
    always_ff @(posedge clk) begin
        imem_wen_r     <= bar_wen && (bar_addr[15:12] == 4'h1);
        imem_waddr_r   <= bar_addr[11:3];
        imem_wdata_lo_r <= bar_wdata[31:0];
        imem_wdata_hi_r <= bar_wdata[63:32];
    end
    
    // Host write port - uses registered signals
    always_ff @(posedge clk) begin
        if (imem_wen_r) begin
            imem[{imem_waddr_r, 1'b0}] <= imem_wdata_lo_r;  // Even word
            imem[{imem_waddr_r, 1'b1}] <= imem_wdata_hi_r;  // Odd word
        end
    end
    
    // Host read port - returns 64 bits (both words)
    always_ff @(posedge clk) begin
        if (bar_ren && bar_addr[15:12] == 4'h1) begin
            imem_host_rdata    <= imem[{bar_addr[11:3], 1'b0}];  // Even word
            imem_host_rdata_hi <= imem[{bar_addr[11:3], 1'b1}];  // Odd word
        end
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
    logic [31:0] dmem_host_rdata;    // Registered read for host access (even word)
    logic [31:0] dmem_host_rdata_hi; // Registered read for host access (odd word)
    
    // CPU port signals
    logic [31:0] cpu_dmem_addr;      // Address from MEM stage
    logic [31:0] cpu_dmem_wdata;     // Write data from MEM stage
    logic [31:0] cpu_dmem_rdata;     // Read data to MEM stage
    logic        cpu_dmem_we;        // Write enable from MEM stage
    
    // Address indexing
    wire [10:0] cpu_dmem_idx = cpu_dmem_addr[12:2];
    
    // Registered write signals for timing (host port)
    logic        dmem_host_wen_r;
    logic [9:0]  dmem_host_waddr_r;  // Pair index (bar_addr[12:3])
    logic [31:0] dmem_host_wdata_lo_r;
    logic [31:0] dmem_host_wdata_hi_r;
    
    // Register host write request
    always_ff @(posedge clk) begin
        dmem_host_wen_r     <= bar_wen && (bar_addr[15:13] == 3'b001);
        dmem_host_waddr_r   <= bar_addr[12:3];
        dmem_host_wdata_lo_r <= bar_wdata[31:0];
        dmem_host_wdata_hi_r <= bar_wdata[63:32];
    end
    
    // Dual-port memory: host has priority over CPU for writes
    always_ff @(posedge clk) begin
        if (dmem_host_wen_r) begin
            // Host write (registered) - always 64 bits
            dmem[{dmem_host_waddr_r, 1'b0}] <= dmem_host_wdata_lo_r;  // Even word
            dmem[{dmem_host_waddr_r, 1'b1}] <= dmem_host_wdata_hi_r;  // Odd word
        end else if (cpu_dmem_we && cpu_running) begin
            // CPU write
            dmem[cpu_dmem_idx] <= cpu_dmem_wdata;
        end
    end
    
    // CPU read port (combinational for same-cycle read in MEM stage)
    assign cpu_dmem_rdata = dmem[cpu_dmem_idx];
    
    // Host read port - returns 64 bits (both words)
    always_ff @(posedge clk) begin
        if (bar_ren && bar_addr[15:13] == 3'b001) begin
            dmem_host_rdata    <= dmem[{bar_addr[12:3], 1'b0}];  // Even word
            dmem_host_rdata_hi <= dmem[{bar_addr[12:3], 1'b1}];  // Odd word
        end
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
            4'h1:        bar_rdata = {imem_host_rdata_hi, imem_host_rdata};  // IMEM (64-bit)
            4'h2, 4'h3:  bar_rdata = {dmem_host_rdata_hi, dmem_host_rdata};  // DMEM (64-bit)
            default:     bar_rdata = 64'h0;
        endcase
    end

    // =========================================================================
    // Pipeline Hazard Control (Registered for Timing)
    // =========================================================================
    //
    // Hazards are detected ONE CYCLE EARLY and registered. This breaks the
    // critical path from hazard detection through pipeline enables.
    //
    // Strategy:
    //   - Detect load-use hazards when instruction is in ID (before it enters EX)
    //   - Register the stall signal so it's ready for the next cycle
    //   - May stall one extra cycle in some cases (conservative but correct)
    
    logic stall;        // Registered stall signal
    logic stall_next;   // Combinational next-stall detection
    logic flush;        // Flush IF, ID, and EX (branch taken in MEM)
    
    // Forward declarations for hazard detection
    logic        ex_mem_read;
    logic        ex_valid;
    logic [4:0]  ex_rd;
    logic [4:0]  id_rs1, id_rs2;
    logic        id_valid;
    logic        mem_branch_taken;  // Branch resolved in MEM stage
    logic [31:0] mem_branch_target; // Branch target from MEM stage
    logic        mem_mem_read;
    logic        mem_valid;
    logic [4:0]  mem_rd;
    logic [4:0]  ex_rs1, ex_rs2;

    // --- Early Hazard Detection (combinational, will be registered) ---
    
    // Hazard 1: EX will have a load next cycle, and the instruction in ID needs it
    // Detect: EX has load AND ID has dependent instruction
    wire hazard_ex_load = ex_mem_read && ex_valid && (ex_rd != 5'd0) &&
                          ((ex_rd == id_rs1) || (ex_rd == id_rs2)) && id_valid;
    
    // Hazard 2: MEM will have a load next cycle (currently in EX), and ID's instruction will need it
    // When ID moves to EX, it will need the load result that's currently going EX→MEM
    // This is detected by: EX has load, and ID reads that register
    // (Same condition as hazard_ex_load - they overlap, which is fine)
    
    // Hazard 3: MEM has load NOW, and EX needs it - must stall this cycle
    // This one we detect in real-time because EX already has the wrong data
    wire hazard_mem_load_now = mem_mem_read && mem_valid && (mem_rd != 5'd0) &&
                               ((mem_rd == ex_rs1) || (mem_rd == ex_rs2)) && ex_valid;
    
    // Next cycle's stall prediction (will be registered)
    assign stall_next = hazard_ex_load;
    
    // Register the stall signal for clean timing
    // Also include immediate MEM hazard detection (can't be predicted earlier)
    logic stall_reg;
    always_ff @(posedge clk) begin
        if (cpu_rst || flush) begin
            stall_reg <= 1'b0;
        end else if (cpu_running) begin
            stall_reg <= stall_next;
        end
    end
    
    // Final stall: registered prediction OR immediate MEM hazard
    // The MEM hazard path is shorter (no ALU) so it's acceptable
    assign stall = stall_reg || hazard_mem_load_now;
    
    // Control hazard: branch taken in MEM, flush IF/ID/EX (3 stages)
    assign flush = mem_branch_taken;

    // =========================================================================
    // Stage 1: IF (Instruction Fetch)
    // =========================================================================
    
    logic [31:0] if_pc;             // Current PC
    logic [31:0] if_instr;          // Fetched instruction
    
    // Next PC selection (branch resolved in MEM stage)
    wire [31:0] if_pc_next = mem_branch_taken ? mem_branch_target : (if_pc + 32'd4);
    
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
    // id_valid declared in forward declarations
    
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
    // ex_rs1, ex_rs2 declared in forward declarations
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
    // Forward from MEM and WB stages to resolve data hazards
    logic [31:0] mem_alu_result;
    logic        mem_reg_write;
    // mem_mem_read, mem_rd declared in forward declarations
    
    // Forward from MEM stage (ALU results available)
    wire fwd_mem_rs1 = mem_reg_write && (mem_rd != 5'd0) && (mem_rd == ex_rs1);
    wire fwd_mem_rs2 = mem_reg_write && (mem_rd != 5'd0) && (mem_rd == ex_rs2);
    
    // Forward from WB stage (ALU results only - load data uses stall + regfile bypass)
    wire fwd_wb_rs1 = wb_reg_write && !wb_mem_read && (wb_rd != 5'd0) && (wb_rd == ex_rs1) && !fwd_mem_rs1;
    wire fwd_wb_rs2 = wb_reg_write && !wb_mem_read && (wb_rd != 5'd0) && (wb_rd == ex_rs2) && !fwd_mem_rs2;
    
    // Forwarding muxes
    wire [31:0] ex_fwd_rs1 = fwd_mem_rs1 ? mem_alu_result :
                            fwd_wb_rs1  ? wb_alu_result :
                            ex_rs1_data;
    
    wire [31:0] ex_fwd_rs2 = fwd_mem_rs2 ? mem_alu_result :
                            fwd_wb_rs2  ? wb_alu_result :
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
    
    // Branch info passed to MEM stage for resolution (breaks timing path)

    // =========================================================================
    // EX/MEM Pipeline Register
    // =========================================================================
    
    logic [31:0] mem_store_data;    // Data to store (forwarded rs2)
    logic        mem_mem_write;
    logic        mem_branch;        // Branch instruction in MEM
    logic        mem_alu_zero;      // ALU zero flag for branch comparison
    logic [31:0] mem_pc;            // PC for branch target calculation
    logic [31:0] mem_imm;           // Immediate for branch target
    // mem_mem_read, mem_valid, mem_rd declared in forward declarations
    
    always_ff @(posedge clk) begin
        if (cpu_rst || flush) begin
            mem_valid     <= 1'b0;
            mem_reg_write <= 1'b0;
            mem_mem_read  <= 1'b0;
            mem_mem_write <= 1'b0;
            mem_branch    <= 1'b0;
        end else if (cpu_running) begin
            mem_alu_result <= ex_alu_result;
            mem_store_data <= ex_fwd_rs2;      // Use forwarded value for stores
            mem_rd         <= ex_rd;
            mem_reg_write  <= ex_reg_write;
            mem_mem_read   <= ex_mem_read;
            mem_mem_write  <= ex_mem_write;
            mem_branch     <= ex_branch && ex_valid;
            mem_alu_zero   <= ex_alu_zero;
            mem_pc         <= ex_pc;
            mem_imm        <= ex_imm;
            mem_valid      <= ex_valid;
        end
    end

    // =========================================================================
    // Stage 4: MEM (Memory Access)
    // =========================================================================
    
    // Branch resolution (moved from EX to break timing path)
    assign mem_branch_taken  = mem_branch && mem_alu_zero && mem_valid;
    assign mem_branch_target = mem_pc + mem_imm;
    
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
