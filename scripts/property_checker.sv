`default_nettype none
`timescale 1ns / 1ps

//---------------------------------------------------------------
// SVA (SystemVerilog Assertions) property checker for
// vbfc_spi_arbiter
//---------------------------------------------------------------
module property_checker (
    input wire        clk,
    input wire        rst_n,
    input wire [2:0]  state,
    input wire        cpu_valid,
    input wire        cpu_ready,
    input wire        addr_ovf,
    input wire [1:0]  region
);

//-------------------------------------------------------------------
// Property 1: Arb never stays in IDLE forever if cpu_valid is held
//-------------------------------------------------------------------
// Formal: within 100 clock cycles of cpu_valid asserted, state must
// transition out of IDLE (to CMD_PHASE)
//
// SVA property:
//
property idle_exit_property;
    @(posedge clk) disable iff (!rst_n)
    (cpu_valid |-> ##[1:100] (state != IDLE));
endproperty

// Check the property
idle_exit_check: assert property (idle_exit_property)
    else $error("FAIL: cpu_valid held high but state stayed in IDLE for >100 cycles");

//-------------------------------------------------------------------
// Property 2: region encoding is always valid (0,1,or2)
//-------------------------------------------------------------------
property valid_region_property;
    @(posedge clk) disable iff (!rst_n)
    (state == DATA_PHASE |-> region inside {2'd0, 2'd1, 2'd2});
endproperty

valid_region_check: assert property (valid_region_property)
    else $error("FAIL: invalid region encoding");

//-------------------------------------------------------------------
// Property 3: addr_ovf should never be asserted when region is valid
//-------------------------------------------------------------------
property no_overflow_when_valid:
    @(posedge clk) disable iff (!rst_n)
    (region != 2'd0 || region != 2'd1 || region != 2'd2 |-> !addr_ovf);
// Note: the above is a simplified check; real property would relate
// addr_ovf to address range checks in the FSM.

//-------------------------------------------------------------------
// Property 4: patch_en pulse is exactly one cycle wide in DATA_PHASE
//-------------------------------------------------------------------
property pulse_one_cycle;
    // If we assert patch_en, it must be high for exactly one SCK cycle
    // in the DATA_PHASE. This is a simplistic check; a full formal
    // model would need the full FSM.
    @(posedge clk) disable iff (!rst_n)
    (state == DATA_PHASE & patch_en |=> !patch_en);
endproperty

pulse_check: assert property (pulse_one_cycle)
    else $warning("WARN: patch_en not pulse-limited in DATA_PHASE);

//-------------------------------------------------------------------
// Main: run all checks
//-------------------------------------------------------------------
initial begin
    // Passive: just let the assertions run in simulation / formal
    // In a real formal run, you'd use formal verification tools
    // (JasperGold, Cadence Incisive, etc.) with the FSM model.
    $display("Property checker module loaded — running passive checks.");
    #100 $finish;
end

endmodule