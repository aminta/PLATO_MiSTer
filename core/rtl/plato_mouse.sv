//============================================================================
//  PLATO terminal core - mouse as touch panel
//
//  Turns the MiSTer PS/2 mouse packets into an absolute pointer position on
//  the 512x512 PLATO screen (row 0 = top), reports left button changes to
//  platod (which sends PLATO touch input, as PTerm does on mouse up) and
//  provides the arrow bitmap drawn by plato_video.
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//============================================================================

module plato_mouse
(
	input             clk,
	input             reset,
	input      [24:0] ps2_mouse,    // [24] toggle, [23:16] Y, [15:8] X, [7:0] status

	output reg  [8:0] x,            // pointer position, 0..511
	output reg  [8:0] y,            // 0 = top row
	output            moved,        // pointer moved in the last ~5 s

	output reg        evt_stb,      // left button changed
	output     [31:0] evt_data      // EVT_MOUSE: [19] left, [17:9] x, [8:0] y
);

reg        old_stb;
reg        left;
reg [27:0] idle;                    // 5 s at 40 MHz = 200,000,000

assign moved    = (idle != 0);
assign evt_data = {2'b01, 10'd0, left, 1'b0, x, y};

wire signed [10:0] dx = {{2{ps2_mouse[4]}}, ps2_mouse[4], ps2_mouse[15:8]};
wire signed [10:0] dy = {{2{ps2_mouse[5]}}, ps2_mouse[5], ps2_mouse[23:16]};
wire signed [10:0] nx = $signed({2'b00, x}) + dx;
wire signed [10:0] ny = $signed({2'b00, y}) - dy;   // PS/2 Y grows upwards

always @(posedge clk) begin
	evt_stb <= 0;
	if (idle != 0) idle <= idle - 1'd1;

	if (reset) begin
		x <= 256;
		y <= 256;
		left <= 0;
		idle <= 0;
		old_stb <= ps2_mouse[24];
	end
	else begin
		old_stb <= ps2_mouse[24];
		if (old_stb != ps2_mouse[24]) begin
			x <= (nx < 0) ? 9'd0 : (nx > 511) ? 9'd511 : nx[8:0];
			y <= (ny < 0) ? 9'd0 : (ny > 511) ? 9'd511 : ny[8:0];
			if (dx != 0 || dy != 0) idle <= 28'd200_000_000;
			if (ps2_mouse[0] != left) begin
				left <= ps2_mouse[0];
				evt_stb <= 1;
			end
		end
	end
end

endmodule

// Arrow pointer, 12 x 19, hot spot at the top left corner.
// Returns 2'b00 transparent, 2'b01 black outline, 2'b10 white fill.
module plato_arrow
(
	input      [4:0] row,
	input      [3:0] col,
	output     [1:0] pix
);

reg [11:0] ol, fl;

always @(*) begin
	case (row)
		5'd0: begin ol = 12'b100000000000; fl = 12'b000000000000; end
		5'd1: begin ol = 12'b110000000000; fl = 12'b000000000000; end
		5'd2: begin ol = 12'b101000000000; fl = 12'b010000000000; end
		5'd3: begin ol = 12'b100100000000; fl = 12'b011000000000; end
		5'd4: begin ol = 12'b100010000000; fl = 12'b011100000000; end
		5'd5: begin ol = 12'b100001000000; fl = 12'b011110000000; end
		5'd6: begin ol = 12'b100000100000; fl = 12'b011111000000; end
		5'd7: begin ol = 12'b100000010000; fl = 12'b011111100000; end
		5'd8: begin ol = 12'b100000001000; fl = 12'b011111110000; end
		5'd9: begin ol = 12'b100000000100; fl = 12'b011111111000; end
		5'd10: begin ol = 12'b100000000010; fl = 12'b011111111100; end
		5'd11: begin ol = 12'b100000011111; fl = 12'b011111100000; end
		5'd12: begin ol = 12'b100010010000; fl = 12'b011101100000; end
		5'd13: begin ol = 12'b100110010000; fl = 12'b011001100000; end
		5'd14: begin ol = 12'b101001001000; fl = 12'b010000110000; end
		5'd15: begin ol = 12'b110001001000; fl = 12'b000000110000; end
		5'd16: begin ol = 12'b100000100100; fl = 12'b000000011000; end
		5'd17: begin ol = 12'b000000100100; fl = 12'b000000011000; end
		5'd18: begin ol = 12'b000000011000; fl = 12'b000000000000; end
		default: begin ol = 12'd0; fl = 12'd0; end
	endcase
end

assign pix = (col > 11) ? 2'b00 : {fl[11 - col], ol[11 - col]};

endmodule
