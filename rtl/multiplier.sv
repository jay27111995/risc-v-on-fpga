// Pipelined Multiplier
// ============================================================================
// 32x32 -> 64-bit multiplier with 2-cycle latency.
// Registers inputs on cycle 1, result available on cycle 2.
//
// Supports all RV32M multiply variants:
//   - MUL:    signed × signed, lower 32 bits
//   - MULH:   signed × signed, upper 32 bits
//   - MULHSU: signed × unsigned, upper 32 bits
//   - MULHU:  unsigned × unsigned, upper 32 bits
// ============================================================================

module multiplier (
    input  logic        clk,
    input  logic        rst_n,

    // Control
    input  logic        start,      // Start multiply (1 cycle pulse)
    input  logic [1:0]  mul_op,     // 00=MUL, 01=MULH, 10=MULHSU, 11=MULHU
    output logic        busy,       // Multiply in progress
    output logic        done,       // Result ready (1 cycle pulse)

    // Operands (active when start=1)
    input  logic [31:0] a,
    input  logic [31:0] b,

    // Result (valid when done=1, active until next start)
    output logic [31:0] result
);

    // Pipeline registers
    logic        stage1_valid;
    logic [1:0]  stage1_op;
    logic [32:0] stage1_a;   // 33-bit for signed extension
    logic [32:0] stage1_b;   // 33-bit for signed extension

    // Multiply result
    logic signed [65:0] mul_result;  // 66-bit to handle signed 33x33

    assign busy = stage1_valid;

    // Stage 1: Register inputs with appropriate sign extension
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            stage1_valid <= 1'b0;
            stage1_op    <= 2'b00;
            stage1_a     <= 33'd0;
            stage1_b     <= 33'd0;
            done         <= 1'b0;
            result       <= 32'd0;
        end else begin
            done <= 1'b0;

            if (start && !stage1_valid) begin
                stage1_valid <= 1'b1;
                stage1_op    <= mul_op;

                // Sign extend based on operation
                // MUL/MULH: both signed
                // MULHSU: a signed, b unsigned
                // MULHU: both unsigned
                case (mul_op)
                    2'b00,  // MUL
                    2'b01:  // MULH
                    begin
                        stage1_a <= {a[31], a};  // Sign extend a
                        stage1_b <= {b[31], b};  // Sign extend b
                    end
                    2'b10:  // MULHSU
                    begin
                        stage1_a <= {a[31], a};  // Sign extend a
                        stage1_b <= {1'b0, b};   // Zero extend b
                    end
                    2'b11:  // MULHU
                    begin
                        stage1_a <= {1'b0, a};   // Zero extend a
                        stage1_b <= {1'b0, b};   // Zero extend b
                    end
                endcase
            end else if (stage1_valid) begin
                // Stage 2: Compute and output result
                stage1_valid <= 1'b0;
                done <= 1'b1;

                // Select lower or upper 32 bits based on operation
                if (stage1_op == 2'b00) begin
                    // MUL: lower 32 bits
                    result <= mul_result[31:0];
                end else begin
                    // MULH, MULHSU, MULHU: upper 32 bits
                    result <= mul_result[63:32];
                end
            end
        end
    end

    // Combinational multiply (will be inferred as DSP)
    // Using registered inputs for better timing
    assign mul_result = $signed(stage1_a) * $signed(stage1_b);

endmodule
