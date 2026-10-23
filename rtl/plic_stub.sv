// ============================================================================
// Minimal PLIC (Platform Level Interrupt Controller)
// ============================================================================
//
// A stub PLIC that reports no external interrupts. Accepts all writes, returns
// 0 for all reads. This satisfies Zephyr's PLIC driver without actual external
// interrupt sources.
//
// Standard SiFive PLIC memory map:
//   Base + 0x000000 - 0x000FFF: Priority registers (1 word per source)
//   Base + 0x001000 - 0x00107F: Pending bits (1 bit per source)
//   Base + 0x002000 - 0x1F1FFF: Enable bits (per context)
//   Base + 0x200000 - 0x3FFFFF: Priority threshold + claim/complete
//
// For context 0 (machine mode, hart 0):
//   Base + 0x200000: Priority threshold
//   Base + 0x200004: Claim (read) / Complete (write)
//
// ============================================================================

module plic_stub #(
    parameter BASE_ADDR = 32'h0C000000
)(
    input  logic        clk,
    input  logic        rst_n,

    // CPU interface
    input  logic [31:0] addr,
    input  logic [31:0] wdata,
    input  logic        we,
    input  logic        re,
    output logic [31:0] rdata,
    output logic        hit
);

    // PLIC spans 4MB (0x0C000000 - 0x0FFFFFFF)
    // Check if address is in PLIC range: addr[31:22] matches base[31:22]
    wire addr_match = (addr[31:22] == BASE_ADDR[31:22]);

    assign hit = addr_match && (we || re);

    // Always return 0 - no interrupts pending, no interrupt to claim
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rdata <= 32'h0;
        end else if (re && addr_match) begin
            rdata <= 32'h0;
        end
    end

    // Writes are accepted but ignored (no actual interrupt sources)
    // Mark unused signals to avoid lint warnings
    /* verilator lint_off UNUSEDSIGNAL */
    wire [21:0] _unused_addr = addr[21:0];
    wire [31:0] _unused_wdata = wdata;
    /* verilator lint_on UNUSEDSIGNAL */

endmodule
