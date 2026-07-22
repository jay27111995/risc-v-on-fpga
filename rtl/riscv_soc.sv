// RISC-V SoC with PCIe BAR interface
//
// 2-Stage Pipeline:
//   Stage 1 (IF/ID): Fetch instruction, Decode, Read registers
//   Stage 2 (EX/WB): Execute ALU, Memory access, Write back
//
// Memory Map (BAR0):
//   0x0000-0x00FF: Control registers
//   0x1000-0x1FFF: IMEM (4KB, 1024 instructions)
//   0x2000-0x3FFF: DMEM (8KB, shared with host)
//
// Control Registers:
//   0x00: CTRL    [0] RUN, [1] RESET (self-clearing)
//   0x08: STATUS  [0] RUNNING, [1] HALTED
//   0x10: PC      Current program counter (read-only)
//   0x18: RESULT  CPU result output (read-only)
//
module riscv_soc (
    input  logic        clk,
    input  logic        clk_en,        // Clock enable (for slower CPU clock)
    input  logic        rst_n,
    
    // BAR interface (directly from axi_core)
    input  logic [15:0] bar_addr,      // byte address
    input  logic [63:0] bar_wdata,     // write data
    input  logic        bar_wen,       // write enable
    output logic [63:0] bar_rdata      // read data
);

    // =========================================================================
    // Control Registers
    // =========================================================================
    
    logic        ctrl_run;
    logic        ctrl_reset;
    logic        cpu_running;
    logic [31:0] cpu_pc;
    logic [31:0] cpu_result;
    
    // Directly derive reset from ctrl_reset
    logic cpu_rst;
    assign cpu_rst = ~rst_n | ctrl_reset;
    
    // CPU running status - only run when clk_en is high
    assign cpu_running = ctrl_run & ~ctrl_reset & clk_en;
    
    // =========================================================================
    // IMEM - Instruction Memory (host writable, CPU readable)
    // 1024 x 32-bit = 4KB
    // =========================================================================
    
    logic [31:0] imem [0:1023];
    logic [31:0] imem_rdata;
    
    // Host write to IMEM (64-bit writes, lower 32 bits used)
    always_ff @(posedge clk) begin
        if (bar_wen && bar_addr[15:12] == 4'h1) begin
            imem[bar_addr[11:2]] <= bar_wdata[31:0];
        end
    end
    
    // CPU read from IMEM (registered for timing)
    logic [31:0] cpu_instr_fetched;
    always_ff @(posedge clk) begin
        cpu_instr_fetched <= imem[cpu_pc[11:2]];
    end
    
    // Host read from IMEM
    always_ff @(posedge clk) begin
        imem_rdata <= imem[bar_addr[11:2]];
    end
    
    // Initialize IMEM with NOPs
    initial begin
        for (int i = 0; i < 1024; i++)
            imem[i] = 32'h00000013;  // NOP
    end
    
    // =========================================================================
    // DMEM - Data Memory (shared between host and CPU)
    // 2048 x 32-bit = 8KB
    // =========================================================================
    
    logic [31:0] dmem [0:2047];
    logic [31:0] dmem_host_rdata;
    
    // CPU DMEM interface
    logic [31:0] cpu_dmem_addr;
    logic [31:0] cpu_dmem_wdata;
    logic [31:0] cpu_dmem_rdata;
    logic        cpu_dmem_we;
    logic        cpu_dmem_re;
    
    // Host write to DMEM (64-bit writes, lower 32 bits used)
    // CPU write to DMEM
    logic [10:0] host_dmem_idx;
    logic [10:0] cpu_dmem_idx;
    
    assign host_dmem_idx = bar_addr[12:2];
    assign cpu_dmem_idx = cpu_dmem_addr[12:2];
    
    always_ff @(posedge clk) begin
        if (bar_wen && bar_addr[15:13] == 3'b001) begin
            // Host write to DMEM (0x2000-0x3FFF)
            dmem[host_dmem_idx] <= bar_wdata[31:0];
        end else if (cpu_dmem_we && cpu_running) begin
            // CPU write
            dmem[cpu_dmem_idx] <= cpu_dmem_wdata;
        end
    end
    
    // CPU read from DMEM (combinational for now)
    assign cpu_dmem_rdata = dmem[cpu_dmem_idx];
    
    // Host read from DMEM
    always_ff @(posedge clk) begin
        dmem_host_rdata <= dmem[host_dmem_idx];
    end
    
    // Initialize DMEM to zero
    initial begin
        for (int i = 0; i < 2048; i++)
            dmem[i] = 32'b0;
    end
    
    // =========================================================================
    // Control Register Logic
    // =========================================================================
    
    always_ff @(posedge clk or negedge rst_n) begin
        if (~rst_n) begin
            ctrl_run <= 0;
            ctrl_reset <= 0;
        end else begin
            // Self-clearing reset
            if (ctrl_reset)
                ctrl_reset <= 0;
            
            // Host write to control registers
            if (bar_wen && bar_addr[15:12] == 4'h0) begin
                if (bar_addr[7:3] == 5'd0) begin  // CTRL register
                    ctrl_run <= bar_wdata[0];
                    ctrl_reset <= bar_wdata[1];
                end
            end
        end
    end
    
    // =========================================================================
    // BAR Read Mux
    // =========================================================================
    
    always_comb begin
        bar_rdata = 64'b0;  // default
        case (bar_addr[15:12])
            4'h0: begin  // Control registers
                case (bar_addr[7:3])
                    5'd0: bar_rdata = {62'b0, ctrl_reset, ctrl_run};  // CTRL
                    5'd1: bar_rdata = {62'b0, 1'b0, cpu_running};     // STATUS
                    5'd2: bar_rdata = {32'b0, cpu_pc};                // PC
                    5'd3: bar_rdata = {32'b0, cpu_result};            // RESULT
                    default: bar_rdata = 64'b0;
                endcase
            end
            4'h1: bar_rdata = {32'b0, imem_rdata};  // IMEM
            4'h2, 4'h3: bar_rdata = {32'b0, dmem_host_rdata};  // DMEM
            default: bar_rdata = 64'b0;
        endcase
    end
    
    // =========================================================================
    // 2-Stage Pipeline RISC-V CPU
    // 
    // Stage 1 (IF/ID): Fetch, Decode, Register Read
    // Stage 2 (EX/WB): Execute, Memory, Write Back
    //
    // Pipeline register sits between Stage 1 and Stage 2
    // =========================================================================
    
    // ---------- Stage 1: Fetch + Decode ----------
    
    // Decoder outputs (Stage 1 - combinational from fetched instruction)
    logic [4:0]  s1_rs1, s1_rs2, s1_rd;
    logic [31:0] s1_imm;
    logic [2:0]  s1_alu_op;
    logic        s1_reg_write;
    logic        s1_alu_src;
    logic        s1_mem_read;
    logic        s1_mem_write;
    logic        s1_branch;
    
    // Decoder instance
    decoder decoder_inst (
        .instr(cpu_instr_fetched),
        .rs1(s1_rs1),
        .rs2(s1_rs2),
        .rd(s1_rd),
        .imm(s1_imm),
        .alu_op(s1_alu_op),
        .reg_write(s1_reg_write),
        .alu_src(s1_alu_src),
        .mem_read(s1_mem_read),
        .mem_write(s1_mem_write),
        .branch(s1_branch)
    );
    
    // Register File read (Stage 1)
    logic [31:0] s1_rs1_data, s1_rs2_data;
    logic [31:0] s2_rd_data;  // Write data from Stage 2
    logic        s2_reg_write; // Write enable from Stage 2
    logic [4:0]  s2_rd;        // Dest register from Stage 2
    
    regfile regfile_inst (
        .clk(clk),
        .we(s2_reg_write & cpu_running),
        .rs1_addr(s1_rs1),
        .rs2_addr(s1_rs2),
        .rd_addr(s2_rd),
        .rd_data(s2_rd_data),
        .rs1_data(s1_rs1_data),
        .rs2_data(s1_rs2_data)
    );
    
    // Stage 1 PC (for branch calculation)
    logic [31:0] s1_pc;
    
    // ---------- Pipeline Register (IF/ID → EX/WB) ----------
    
    logic [31:0] s2_pc;
    logic [31:0] s2_rs1_data, s2_rs2_data;
    logic [31:0] s2_imm;
    logic [2:0]  s2_alu_op;
    logic        s2_alu_src;
    logic        s2_mem_read;
    logic        s2_mem_write;
    logic        s2_branch;
    logic        s2_valid;  // Pipeline valid bit
    
    // Data forwarding: if Stage 2 writes to a reg that Stage 1 reads, forward it
    logic [31:0] s1_rs1_fwd, s1_rs2_fwd;
    logic        fwd_rs1, fwd_rs2;
    
    assign fwd_rs1 = s2_reg_write && (s2_rd != 0) && (s2_rd == s1_rs1);
    assign fwd_rs2 = s2_reg_write && (s2_rd != 0) && (s2_rd == s1_rs2);
    assign s1_rs1_fwd = fwd_rs1 ? s2_rd_data : s1_rs1_data;
    assign s1_rs2_fwd = fwd_rs2 ? s2_rd_data : s1_rs2_data;
    
    // Pipeline register update
    always_ff @(posedge clk) begin
        if (cpu_rst) begin
            s2_valid <= 0;
            s2_reg_write <= 0;
            s2_mem_write <= 0;
            s2_branch <= 0;
        end else if (cpu_running) begin
            // Pass values to Stage 2
            s2_pc <= s1_pc;
            s2_rs1_data <= s1_rs1_fwd;
            s2_rs2_data <= s1_rs2_fwd;
            s2_imm <= s1_imm;
            s2_alu_op <= s1_alu_op;
            s2_rd <= s1_rd;
            s2_reg_write <= s1_reg_write;
            s2_alu_src <= s1_alu_src;
            s2_mem_read <= s1_mem_read;
            s2_mem_write <= s1_mem_write;
            s2_branch <= s1_branch;
            s2_valid <= 1;
        end
    end
    
    // ---------- Stage 2: Execute + Write Back ----------
    
    // ALU input mux
    logic [31:0] s2_alu_b;
    assign s2_alu_b = s2_alu_src ? s2_imm : s2_rs2_data;
    
    // ALU
    logic [31:0] s2_alu_result;
    logic        s2_alu_zero;
    
    alu alu_inst (
        .a(s2_rs1_data),
        .b(s2_alu_b),
        .op(s2_alu_op),
        .result(s2_alu_result),
        .zero(s2_alu_zero)
    );
    
    // Branch logic
    logic        s2_take_branch;
    logic [31:0] s2_branch_addr;
    
    assign s2_take_branch = s2_branch & s2_alu_zero & s2_valid;
    assign s2_branch_addr = s2_pc + s2_imm;
    
    // DMEM interface
    assign cpu_dmem_addr = s2_alu_result;
    assign cpu_dmem_wdata = s2_rs2_data;
    assign cpu_dmem_we = s2_mem_write & s2_valid;
    assign cpu_dmem_re = s2_mem_read & s2_valid;
    
    // Write-back mux
    assign s2_rd_data = s2_mem_read ? cpu_dmem_rdata : s2_alu_result;
    
    // ---------- Program Counter ----------
    
    always_ff @(posedge clk) begin
        if (cpu_rst) begin
            cpu_pc <= 32'b0;
            s1_pc <= 32'b0;
        end else if (cpu_running) begin
            // Save current PC for Stage 1 (used by branches in Stage 2)
            s1_pc <= cpu_pc;
            
            // Next PC logic
            if (s2_take_branch) begin
                cpu_pc <= s2_branch_addr;
            end else begin
                cpu_pc <= cpu_pc + 4;
            end
        end
    end
    
    // Debug output
    assign cpu_result = s2_rd_data;

endmodule
