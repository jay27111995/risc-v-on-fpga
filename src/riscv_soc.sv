// RISC-V SoC with PCIe BAR interface
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
// Usage:
//   1. Host writes program to 0x1000+ (IMEM)
//   2. Host writes data to 0x2000+ (DMEM) 
//   3. Host sets CTRL[1]=1 (RESET), then CTRL[0]=1 (RUN)
//   4. CPU executes program
//   5. Host reads results from DMEM
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
    
    // CPU read from IMEM
    logic [31:0] cpu_instr;
    assign cpu_instr = imem[cpu_pc[11:2]];
    
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
    // Host uses BAR offset 0x2000+, CPU uses address directly
    logic [10:0] host_dmem_idx;
    logic [10:0] cpu_dmem_idx;
    
    assign host_dmem_idx = bar_addr[12:2];  // 0x2000 → bit 13 is 1, we use [12:2]
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
    
    // CPU read from DMEM (combinational)
    assign cpu_dmem_rdata = cpu_dmem_re ? dmem[cpu_dmem_idx] : 32'b0;
    
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
    // RISC-V CPU Core (Single-cycle with registered outputs for timing)
    // =========================================================================
    
    // CPU internal signals
    logic [31:0] cpu_rs1_data, cpu_rs2_data;
    logic [31:0] cpu_imm;
    logic [31:0] cpu_alu_result;
    logic        cpu_alu_zero;
    logic [31:0] cpu_alu_b;
    logic [31:0] cpu_rd_data;
    
    // Decoder outputs
    logic [4:0]  cpu_rs1, cpu_rs2, cpu_rd;
    logic [2:0]  cpu_alu_op;
    logic        cpu_reg_write;
    logic        cpu_alu_src;
    logic        cpu_mem_read;
    logic        cpu_mem_write;
    logic        cpu_branch;
    
    // Branch logic
    logic        cpu_take_branch;
    logic [31:0] cpu_branch_addr;
    
    assign cpu_take_branch = cpu_branch & cpu_alu_zero;
    assign cpu_branch_addr = cpu_pc + cpu_imm;
    
    // Program Counter
    always_ff @(posedge clk) begin
        if (cpu_rst) begin
            cpu_pc <= 32'b0;
        end else if (cpu_running) begin
            if (cpu_take_branch)
                cpu_pc <= cpu_branch_addr;
            else
                cpu_pc <= cpu_pc + 4;
        end
    end
    
    // Decoder
    decoder decoder_inst (
        .instr(cpu_instr),
        .rs1(cpu_rs1),
        .rs2(cpu_rs2),
        .rd(cpu_rd),
        .imm(cpu_imm),
        .alu_op(cpu_alu_op),
        .reg_write(cpu_reg_write),
        .alu_src(cpu_alu_src),
        .mem_read(cpu_mem_read),
        .mem_write(cpu_mem_write),
        .branch(cpu_branch)
    );
    
    // Register File
    regfile regfile_inst (
        .clk(clk),
        .we(cpu_reg_write & cpu_running),
        .rs1_addr(cpu_rs1),
        .rs2_addr(cpu_rs2),
        .rd_addr(cpu_rd),
        .rd_data(cpu_rd_data),
        .rs1_data(cpu_rs1_data),
        .rs2_data(cpu_rs2_data)
    );
    
    // ALU input mux
    assign cpu_alu_b = cpu_alu_src ? cpu_imm : cpu_rs2_data;
    
    // ALU
    alu alu_inst (
        .a(cpu_rs1_data),
        .b(cpu_alu_b),
        .op(cpu_alu_op),
        .result(cpu_alu_result),
        .zero(cpu_alu_zero)
    );
    
    // DMEM interface
    assign cpu_dmem_addr = cpu_alu_result;
    assign cpu_dmem_wdata = cpu_rs2_data;
    assign cpu_dmem_we = cpu_mem_write;
    assign cpu_dmem_re = cpu_mem_read;
    
    // Write-back mux
    assign cpu_rd_data = cpu_mem_read ? cpu_dmem_rdata : cpu_alu_result;
    
    // Debug output
    assign cpu_result = cpu_rd_data;

endmodule
