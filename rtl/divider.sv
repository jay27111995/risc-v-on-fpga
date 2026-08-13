// Multi-cycle Divider
// ============================================================================
// 32-bit divider using restoring division algorithm.
// Takes 32 cycles to complete after start.
//
// Handles:
//   - Signed and unsigned division
//   - Division by zero (returns -1 / dividend per RISC-V spec)
//   - Signed overflow (-2^31 / -1 returns -2^31)
//
// Interface:
//   - Assert start with operands for 1 cycle
//   - Wait until done pulses high
//   - Results valid when done=1 and held until next start
// ============================================================================

module divider (
    input  logic        clk,
    input  logic        rst_n,

    // Control
    input  logic        start,      // Start division (1 cycle pulse)
    input  logic        is_signed,  // Signed operation?
    output logic        busy,       // Division in progress
    output logic        done,       // Result ready (1 cycle pulse)

    // Operands (sampled on start)
    input  logic [31:0] dividend,
    input  logic [31:0] divisor,

    // Results (valid from done until next start)
    output logic [31:0] quotient,
    output logic [31:0] remainder
);

    // State machine
    typedef enum logic [1:0] {
        S_IDLE,
        S_RUNNING,
        S_DONE
    } state_t;

    state_t state, state_next;

    // Working registers
    logic [5:0]  count;          // Iteration counter (0-31)
    logic [31:0] quot;           // Quotient being built
    logic [31:0] rem;            // Remainder (partial dividend)
    logic [31:0] divisor_reg;    // Stored divisor (absolute value)
    logic [31:0] dividend_reg;   // Stored dividend for shift-in
    logic        neg_quot;       // Negate quotient at end?
    logic        neg_rem;        // Negate remainder at end?

    assign busy = (state == S_RUNNING);

    // Absolute value helpers
    wire [31:0] abs_dividend = (is_signed && dividend[31]) ? (~dividend + 1'b1) : dividend;
    wire [31:0] abs_divisor  = (is_signed && divisor[31])  ? (~divisor  + 1'b1) : divisor;

    // State machine - combinational
    always_comb begin
        state_next = state;
        case (state)
            S_IDLE:    if (start) state_next = S_RUNNING;
            S_RUNNING: if (count == 6'd31) state_next = S_DONE;
            S_DONE:    state_next = S_IDLE;
            default:   state_next = S_IDLE;
        endcase
    end

    // Main sequential logic
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state        <= S_IDLE;
            done         <= 1'b0;
            count        <= 6'd0;
            quot         <= 32'd0;
            rem          <= 32'd0;
            divisor_reg  <= 32'd0;
            dividend_reg <= 32'd0;
            neg_quot     <= 1'b0;
            neg_rem      <= 1'b0;
            quotient     <= 32'd0;
            remainder    <= 32'd0;
        end else begin
            state <= state_next;
            done  <= 1'b0;

            case (state)
                S_IDLE: begin
                    if (start) begin
                        // Check special cases
                        if (divisor == 32'd0) begin
                            // Divide by zero: return -1 and dividend
                            quotient  <= 32'hFFFFFFFF;
                            remainder <= dividend;
                            done      <= 1'b1;
                            state     <= S_IDLE;  // Skip directly to idle
                        end else if (is_signed && (dividend == 32'h80000000) && (divisor == 32'hFFFFFFFF)) begin
                            // Signed overflow: -2^31 / -1 = -2^31, remainder = 0
                            quotient  <= 32'h80000000;
                            remainder <= 32'd0;
                            done      <= 1'b1;
                            state     <= S_IDLE;
                        end else begin
                            // Normal division - initialize
                            count        <= 6'd0;
                            quot         <= 32'd0;
                            rem          <= 32'd0;
                            divisor_reg  <= abs_divisor;
                            dividend_reg <= abs_dividend;
                            // For signed: quotient negative if signs differ
                            neg_quot <= is_signed && (dividend[31] ^ divisor[31]);
                            // Remainder has same sign as dividend
                            neg_rem  <= is_signed && dividend[31];
                        end
                    end
                end

                S_RUNNING: begin
                    // Restoring division algorithm:
                    // 1. Shift remainder left, bringing in next bit from dividend
                    // 2. Try to subtract divisor from remainder
                    // 3. If result >= 0: keep it, quotient bit = 1
                    //    If result < 0:  restore (don't keep), quotient bit = 0

                    // The bit we're bringing in is the MSB of what's left of dividend
                    // We process from MSB to LSB
                    logic [31:0] rem_shifted;
                    logic [32:0] sub_result;
                    logic        sub_ok;

                    // Shift remainder left and bring in next dividend bit
                    rem_shifted = {rem[30:0], dividend_reg[31 - count[4:0]]};

                    // Try to subtract divisor
                    sub_result = {1'b0, rem_shifted} - {1'b0, divisor_reg};
                    sub_ok = !sub_result[32];  // rem >= divisor

                    if (sub_ok) begin
                        // Subtraction succeeded - keep result
                        rem <= sub_result[31:0];
                        quot <= {quot[30:0], 1'b1};
                    end else begin
                        // Subtraction failed - restore (keep shifted value)
                        rem <= rem_shifted;
                        quot <= {quot[30:0], 1'b0};
                    end

                    count <= count + 1'b1;

                    // Check if this is the last iteration
                    if (count == 6'd31) begin
                        // Apply sign correction to final values
                        logic [31:0] final_quot;
                        logic [31:0] final_rem;

                        if (sub_ok) begin
                            final_quot = {quot[30:0], 1'b1};
                            final_rem  = sub_result[31:0];
                        end else begin
                            final_quot = {quot[30:0], 1'b0};
                            final_rem  = rem_shifted;
                        end

                        quotient  <= neg_quot ? (~final_quot + 1'b1) : final_quot;
                        remainder <= neg_rem  ? (~final_rem  + 1'b1) : final_rem;
                        done      <= 1'b1;
                    end
                end

                S_DONE: begin
                    // Results remain valid, just transition back to idle
                end

                default: begin
                    // Should never reach here
                end
            endcase
        end
    end

endmodule
