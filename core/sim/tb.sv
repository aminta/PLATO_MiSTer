// Testbench for plato_video + plato_ddr with a simple DDR3 model.
// Run: iverilog -g2012 -o tb sim/tb.sv rtl/plato_video.sv rtl/plato_ddr.sv && vvp tb
`timescale 1ns/1ps

module tb;

reg clk = 0;
always #12.5 clk = ~clk;   // 40 MHz

reg reset = 1;

wire        DDRAM_BUSY;
wire  [7:0] DDRAM_BURSTCNT;
wire [28:0] DDRAM_ADDR;
reg  [63:0] DDRAM_DOUT;
reg         DDRAM_DOUT_READY;
wire        DDRAM_RD;
wire [63:0] DDRAM_DIN;
wire  [7:0] DDRAM_BE;
wire        DDRAM_WE;

wire        fetch_req, frame_start, lb_we, alive;
wire  [8:0] fetch_row, lb_addr;
wire [47:0] lb_data;
wire HBlank, VBlank, HSync, VSync;
wire [7:0] R, G, B;
reg  [10:0] ps2_key = 0;

plato_video video (
	.clk(clk), .reset(reset), .enable(alive),
	.fetch_req(fetch_req), .fetch_row(fetch_row), .frame_start(frame_start),
	.lb_we(lb_we), .lb_addr(lb_addr), .lb_data(lb_data),
	.HBlank(HBlank), .VBlank(VBlank), .HSync(HSync), .VSync(VSync),
	.R(R), .G(G), .B(B)
);

plato_ddr ddr (
	.clk(clk), .reset(reset),
	.DDRAM_BUSY(DDRAM_BUSY), .DDRAM_BURSTCNT(DDRAM_BURSTCNT),
	.DDRAM_ADDR(DDRAM_ADDR), .DDRAM_DOUT(DDRAM_DOUT),
	.DDRAM_DOUT_READY(DDRAM_DOUT_READY), .DDRAM_RD(DDRAM_RD),
	.DDRAM_DIN(DDRAM_DIN), .DDRAM_BE(DDRAM_BE), .DDRAM_WE(DDRAM_WE),
	.fetch_req(fetch_req), .fetch_row(fetch_row), .frame_start(frame_start),
	.lb_we(lb_we), .lb_addr(lb_addr), .lb_data(lb_data), .alive(alive),
	.ps2_key(ps2_key), .status_word(32'h1234_5678)
);

// ---------------------------------------------------------------------------
// DDR model: random wait states, read latency of 30 cycles
// ---------------------------------------------------------------------------
localparam [28:0] FB_BASE = 29'h0600_0000;
localparam [28:0] CTL     = 29'h0602_0000;

reg [63:0] ctlmem[0:511];

function [31:0] pix(input [8:0] x, input [8:0] y);
	pix = {8'h00, x[7:0], y[7:0], {x[8], y[8], 6'h15}};
endfunction

function [63:0] fbword(input [28:0] a);
	reg [16:0] off;
	reg [8:0] y, x;
	begin
		off = a - FB_BASE;
		y = off[16:8];
		x = {off[7:0], 1'b0};
		fbword = {pix(x + 1'd1, y), pix(x, y)};
	end
endfunction

reg busy_r = 0;
assign DDRAM_BUSY = busy_r;
always @(posedge clk) busy_r <= ($random & 3) == 0;

reg [28:0] rd_addr;
reg  [7:0] rd_left;
integer    rd_delay;
integer    writes = 0;

always @(posedge clk) begin
	DDRAM_DOUT_READY <= 0;
	if (DDRAM_RD && !DDRAM_BUSY) begin
		if (rd_left != 0) begin
			$display("ERROR: overlapping read"); $finish;
		end
		rd_addr  <= DDRAM_ADDR;
		rd_left  <= DDRAM_BURSTCNT;
		rd_delay <= 30;
	end
	else if (rd_left != 0) begin
		if (rd_delay != 0) rd_delay <= rd_delay - 1;
		else if (($random & 7) != 0) begin
			DDRAM_DOUT_READY <= 1;
			DDRAM_DOUT <= (rd_addr >= CTL) ? ctlmem[rd_addr - CTL] : fbword(rd_addr);
			rd_addr <= rd_addr + 1;
			rd_left <= rd_left - 1;
		end
	end
	if (DDRAM_WE && !DDRAM_BUSY) begin
		if (DDRAM_ADDR < CTL || DDRAM_ADDR >= CTL + 512) begin
			$display("ERROR: write outside control block %h", DDRAM_ADDR); $finish;
		end
		ctlmem[DDRAM_ADDR - CTL] <= DDRAM_DIN;
		writes = writes + 1;
	end
end

// ---------------------------------------------------------------------------
// Check the video output
// ---------------------------------------------------------------------------
integer x = 0, y = 0, frame = 0, errors = 0, good = 0;
reg old_de = 0, old_vs = 0;
wire de = ~(HBlank | VBlank);

integer scale = 1;
always @(posedge clk) begin
	old_de <= de;
	old_vs <= VSync;
	if (VSync && !old_vs) begin
		$display("frame %0d: %0d good pixels, %0d errors, alive=%0d", frame, good, errors, alive);
		frame = frame + 1;
		y = 0;
		good = 0;
	end
	if (de) begin
		if (frame >= 1) begin
			if ({R, G, B} != pix(x / scale, y / scale)) begin
				if (errors < 10)
					$display("pixel %0d,%0d = %h expected %h", x, y, {R, G, B}, pix(x / scale, y / scale));
				errors = errors + 1;
			end
			else good = good + 1;
		end
		x = x + 1;
	end
	if (!de && old_de) begin
		if (x != 512 * scale) $display("ERROR: line %0d has %0d pixels", y, x);
		x = 0;
		y = y + 1;
	end
end

integer i;
initial begin
	for (i = 0; i < 512; i = i + 1) ctlmem[i] = 0;
	ctlmem[1] = 64'h0000_0000_504C_4154;     // alive magic
	#1000 reset = 0;

	// key events
	#3000000;
	ps2_key = 11'h61C;  ps2_key[10] = 1;     // 'a' pressed
	#100;
	ps2_key[9] = 0; ps2_key[10] = 0;         // released
	#5000000;
	$display("header %h", ctlmem[0]);
	$display("slot1  %h", ctlmem[32 + 1]);
	$display("slot2  %h", ctlmem[32 + 2]);
	if (ctlmem[0] !== 64'h00000002_12345678) begin $display("ERROR: header"); errors = errors + 1; end
	if (ctlmem[33][63:32] !== 1 || ctlmem[34][63:32] !== 2) begin $display("ERROR: slots"); errors = errors + 1; end

	wait (frame == 3);
	$display("DONE errors=%0d", errors);
	$finish;
end

endmodule
