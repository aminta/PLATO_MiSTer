//============================================================================
//  PLATO terminal core - video output
//
//  VESA 800x600 @ 60 Hz timing (40 MHz pixel clock) with the 512x512 PLATO
//  screen centered.  Only the 512x512 picture is flagged as active (DE), so
//  the MiSTer scaler shows just the PLATO screen on HDMI, while the analog
//  VGA output keeps a standard SVGA mode that any VGA monitor accepts.
//
//  Picture lines are fetched one line ahead from DDR3 into a double line
//  buffer by plato_ddr (fetch_req / fetch_row / lb_* signals).
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//============================================================================

module plato_video
(
	input             clk,          // 40 MHz pixel clock
	input             reset,

	input             enable,       // frame buffer valid (platod running)

	// line fetch request to the DDR reader
	output reg        fetch_req,    // one clock pulse
	output reg  [8:0] fetch_row,    // picture row to fetch (0 = top)
	output reg        frame_start,  // one clock pulse at the start of vblank

	// line buffer write port (from the DDR reader)
	input             lb_we,
	input       [8:0] lb_addr,      // {bank, word[7:0]}, 2 pixels per word
	input      [47:0] lb_data,      // {odd pixel RGB, even pixel RGB}

	output reg        HBlank,
	output reg        VBlank,
	output reg        HSync,
	output reg        VSync,
	output reg  [7:0] R,
	output reg  [7:0] G,
	output reg  [7:0] B
);

localparam H_ACT  = 800, H_FP = 40, H_SYNC = 128, H_BP = 88;
localparam V_ACT  = 600, V_FP = 1,  V_SYNC = 4,   V_BP = 23;
localparam H_TOT  = H_ACT + H_FP + H_SYNC + H_BP;   // 1056
localparam V_TOT  = V_ACT + V_FP + V_SYNC + V_BP;   // 628
localparam X0     = (H_ACT - 512) / 2;              // 144
localparam Y0     = (V_ACT - 512) / 2;              // 44

reg [10:0] hc;
reg  [9:0] vc;

always @(posedge clk) begin
	if (reset) begin
		hc <= 0;
		vc <= 0;
	end
	else begin
		if (hc == H_TOT - 1) begin
			hc <= 0;
			if (vc == V_TOT - 1) vc <= 0;
			else vc <= vc + 1'd1;
		end
		else hc <= hc + 1'd1;
	end
end

// Fetch the next picture line at the start of each line.
wire [9:0] next_line = (vc == V_TOT - 1) ? 10'd0 : vc + 1'd1;
wire [9:0] next_row  = next_line - Y0[9:0];

always @(posedge clk) begin
	fetch_req   <= 0;
	frame_start <= 0;
	if (hc == 0) begin
		if (next_line >= Y0 && next_line < Y0 + 512) begin
			fetch_req <= 1;
			fetch_row <= next_row[8:0];
		end
		if (vc == V_ACT) frame_start <= 1;
	end
end

// Line buffer: 2 banks x 256 words x 48 bits (two pixels per word)
reg [47:0] linebuf[512];

always @(posedge clk) begin
	if (lb_we) linebuf[lb_addr] <= lb_data;
end

// Stage 1: picture coordinates and buffer read
wire [10:0] px  = hc - X0[10:0];
wire  [9:0] py  = vc - Y0[9:0];
wire        pic = (hc >= X0) && (hc < X0 + 512) && (vc >= Y0) && (vc < Y0 + 512);

reg  [47:0] lb_q;
reg         pic1, odd1;
reg         hs1, vs1, hb1, vb1;

always @(posedge clk) begin
	lb_q <= linebuf[{py[0], px[8:1]}];
	pic1 <= pic;
	odd1 <= px[0];
	hb1  <= (hc < X0) || (hc >= X0 + 512);
	vb1  <= (vc < Y0) || (vc >= Y0 + 512);
	hs1  <= (hc >= H_ACT + H_FP) && (hc < H_ACT + H_FP + H_SYNC);
	vs1  <= (vc >= V_ACT + V_FP) && (vc < V_ACT + V_FP + V_SYNC);
end

// Stage 2: pixel select and outputs
always @(posedge clk) begin
	HSync  <= hs1;
	VSync  <= vs1;
	HBlank <= hb1;
	VBlank <= vb1;
	if (!pic1) {R, G, B} <= 24'd0;
	else if (!enable) {R, G, B} <= 24'h000040;  // dark blue: waiting for platod
	else {R, G, B} <= odd1 ? lb_q[47:24] : lb_q[23:0];
end

endmodule
