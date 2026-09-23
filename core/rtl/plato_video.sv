//============================================================================
//  PLATO terminal core - video output
//
//  Two VESA modes from a single 108 MHz clock:
//
//   mode 0: 1280x1024 @ 60 Hz (108 MHz, 64.0 kHz), PLATO screen doubled
//           to 1024x1024 and centered: pixel perfect 2x, fills the height
//   mode 1:  800x600  @ 56 Hz ( 36 MHz = 108/3, 35.2 kHz), PLATO screen
//           512x512 1:1, centered
//
//  Only the PLATO picture is flagged as active (DE), so the MiSTer scaler
//  shows just the PLATO screen on HDMI, while the analog VGA output keeps a
//  standard mode that VGA monitors accept.  The mode changes only at the
//  end of a frame.
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
	input             clk,          // 108 MHz
	input             reset,

	input             mode_sel,     // 0 = 1280x1024 2x, 1 = 800x600 1x
	input             enable,       // frame buffer valid (platod running)

	output reg        ce_pix,

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

// Timing of the current mode (latched at the end of each frame)
reg        mode = 0;

// VESA 1280x1024@60: H 1280+48+112+248, V 1024+1+3+38, +/+
// VESA  800x600@56:  H  800+24+72+128,  V  600+1+2+22,  +/+
wire [10:0] H_ACT = mode ? 11'd800  : 11'd1280;
wire [10:0] H_SS  = mode ? 11'd824  : 11'd1328;   // sync start
wire [10:0] H_SE  = mode ? 11'd896  : 11'd1440;   // sync end
wire [10:0] H_TOT = mode ? 11'd1024 : 11'd1688;
wire [10:0] V_ACT = mode ? 11'd600  : 11'd1024;
wire [10:0] V_SS  = mode ? 11'd601  : 11'd1025;
wire [10:0] V_SE  = mode ? 11'd603  : 11'd1028;
wire [10:0] V_TOT = mode ? 11'd625  : 11'd1066;
wire [10:0] X0    = mode ? 11'd144  : 11'd128;    // picture origin
wire [10:0] Y0    = mode ? 11'd44   : 11'd0;
wire        dbl   = !mode;                        // 2x scaling

// Pixel clock enable: every clock (108 MHz) or every third (36 MHz)
reg [1:0] div;
always @(posedge clk) begin
	div <= (div == 2) ? 2'd0 : div + 1'd1;
	ce_pix <= !mode || (div == 0);
end

reg [10:0] hc;
reg [10:0] vc;

always @(posedge clk) begin
	if (reset) begin
		hc <= 0;
		vc <= 0;
	end
	else if (ce_pix) begin
		if (hc == H_TOT - 1'd1) begin
			hc <= 0;
			if (vc == V_TOT - 1'd1) begin
				vc <= 0;
				mode <= mode_sel;
			end
			else vc <= vc + 1'd1;
		end
		else hc <= hc + 1'd1;
	end
end

// Picture size on screen and picture row of a screen line
wire [10:0] PW = dbl ? 11'd1024 : 11'd512;
wire [10:0] line_rel_next = ((vc == V_TOT - 1'd1) ? 11'd0 : vc + 1'd1) - Y0;
wire        next_in_pic  = (((vc == V_TOT - 1'd1) ? 11'd0 : vc + 1'd1) >= Y0) && (line_rel_next < PW);
wire  [8:0] next_row     = dbl ? line_rel_next[9:1] : line_rel_next[8:0];

// Fetch the picture row of the next line at the start of each line.  In 2x
// mode each row is shown on two lines, so it is fetched only once.
always @(posedge clk) begin
	fetch_req   <= 0;
	frame_start <= 0;
	if (ce_pix && hc == 0) begin
		if (next_in_pic && (!dbl || !line_rel_next[0])) begin
			fetch_req <= 1;
			fetch_row <= next_row;
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
wire [10:0] hrel = hc - X0;
wire [10:0] vrel = vc - Y0;
wire        hpic = (hc >= X0) && (hrel < PW);
wire        vpic = (vc >= Y0) && (vrel < PW);
wire  [8:0] px   = dbl ? hrel[9:1] : hrel[8:0];
wire  [8:0] py   = dbl ? vrel[9:1] : vrel[8:0];

reg  [47:0] lb_q;
reg         pic1, odd1;
reg         hs1, vs1, hb1, vb1;

always @(posedge clk) begin
	if (ce_pix) begin
		lb_q <= linebuf[{py[0], px[8:1]}];
		pic1 <= hpic & vpic;
		odd1 <= px[0];
		hb1  <= ~hpic;
		vb1  <= ~vpic;
		hs1  <= (hc >= H_SS) && (hc < H_SE);
		vs1  <= (vc >= V_SS) && (vc < V_SE);
	end
end

// Stage 2: pixel select and outputs
always @(posedge clk) begin
	if (ce_pix) begin
		HSync  <= hs1;
		VSync  <= vs1;
		HBlank <= hb1;
		VBlank <= vb1;
		if (!pic1) {R, G, B} <= 24'd0;
		else if (!enable) {R, G, B} <= 24'h000040;  // dark blue: waiting for platod
		else {R, G, B} <= odd1 ? lb_q[47:24] : lb_q[23:0];
	end
end

endmodule
