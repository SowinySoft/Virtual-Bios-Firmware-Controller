`default_nettype none
`timescale 1ns / 1ps

//--------------------------------------------------------------
// Testbench for vbfc_spi_arbiter
//--------------------------------------------------------------
module tb_vbfc_spi_arbiter;
    // Clock & reset
    reg         clk;
    reg         rst_n;

    // CPU interface
    reg         cpu_valid;
    reg  [31:0] cpu_addr;
    reg  [31:0] cpu_wdata;
    reg         cpu_rw;
    wire        cpu_ready;
    wire [31:0] cpu_rdata;

    // SPI interface
    wire        spi_cs_n;
    wire        spi_sck;
    inout       spi_mosi;
    input       spi_miso;

    // Status outputs
    wire [2:0]  state;
    wire        patch_en;
    wire [1:0]  region;
    wire        addr_ovf;

    // Instantiate the arbiter
    vbfc_spi_arbiter uut (
        .clk           (clk),
        .rst_n         (rst_n),
        .cpu_valid     (cpu_valid),
        .cpu_addr      (cpu_addr),
        .cpu_wdata     (cpu_wdata),
        .cpu_rw        (cpu_rw),
        .cpu_ready     (cpu_ready),
        .cpu_rdata     (cpu_rdata),
        .spi_cs_n      (spi_cs_n),
        .spi_sck       (spi_sck),
        .spi_mosi      (spi_mosi),
        .spi_miso      (spi_miso),
        .state         (state),
        .patch_en      (patch_en),
        .region        (region),
        .addr_ovf      (addr_ovf)
    );

    //--------------------------------------------------------------
    // Clock generator: 20 MHz period (50 ns)
    //--------------------------------------------------------------
    initial begin
        clk = 0;
        forever #25 clk = ~clk; // 20 MHz
    end

    //--------------------------------------------------------------
    // Reset: active-low, assert for 5 cycles
    //--------------------------------------------------------------
    initial begin
        rst_n = 0;
        #100 rst_n = 1;
    end

    //--------------------------------------------------------------
    // SPI MISO driver (simple: return fixed byte 0xAA for all reads)
    //--------------------------------------------------------------
    assign spi_miso = 8'hAA; // constant response

    //--------------------------------------------------------------
    // CPU stimulus: drive a single read request after reset
    //--------------------------------------------------------------
    initial begin
        // Initialize all inputs
        cpu_valid   = 0;
        cpu_addr    = 0;
        cpu_wdata   = 0;
        cpu_rw      = 0;

        // Wait for reset release
        wait (rst_n);

        // Wait a bit for stability
        #100;

        // Drive a read request at address 0x0000_1000 (uses patch_idx = 0x000)
        cpu_valid   = 1;
        cpu_addr    = 32'h1000;   // address => patch table index = 0x10
        cpu_rw      = 1;          // read

        // Wait for arbiter to complete
        wait (cpu_ready);
        #50;

        // Disable CPU request
        cpu_valid = 0;

        // Run a bit longer then finish
        #200;
        $stop;
    end

    //--------------------------------------------------------------
    // Monitor: print state transitions and key values
    //--------------------------------------------------------------
    initial begin
        $monitor("time=%0t | state=%b | cs_n=%b | addr=%h | patch_en=%b | region=%b | addr_ovf=%b",
                 $time, state, 1'b0, cpu_addr, patch_en, region, addr_ovf);
    end

    //--------------------------------------------------------------
    // Force MOSI to known value for simulation (since it's inout)
    //--------------------------------------------------------------
    initial begin
        // The arbiter drives MOSO in simulation; force 0 initially
        force spi_mosi = 1'b0;
        // Release after some time
        #1000 release spi_mosi;
    end

endmodule