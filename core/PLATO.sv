//============================================================================
//  PLATO terminal - hybrid MiSTer core
//
//  The FPGA side outputs the PLATO screen on HDMI and VGA and
//  forwards the keyboard to platod, which runs on the ARM (HPS) and does
//  the networking and PLATO protocol decoding with the PTerm engine.
//  Both sides talk through DDR3 at 0x30000000, see rtl/plato_ddr.sv.
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//============================================================================

module emu
(
	`include "sys/emu_ports.vh"
);

///////// Default values for ports not used in this core /////////

assign ADC_BUS  = 'Z;
assign USER_OUT = '1;
assign {UART_RTS, UART_TXD, UART_DTR} = 0;
assign {SD_SCK, SD_MOSI, SD_CS} = 'Z;
assign {SDRAM_DQ, SDRAM_A, SDRAM_BA, SDRAM_CLK, SDRAM_CKE, SDRAM_DQML, SDRAM_DQMH, SDRAM_nWE, SDRAM_nCAS, SDRAM_nRAS, SDRAM_nCS} = 'Z;

assign VGA_SL = 0;
assign VGA_F1 = 0;
assign VGA_SCALER  = 0;
assign VGA_DISABLE = 0;
assign HDMI_FREEZE = 0;
assign HDMI_BLACKOUT = 0;
assign HDMI_BOB_DEINT = 0;

assign AUDIO_S = 0;
assign AUDIO_L = 0;
assign AUDIO_R = 0;
assign AUDIO_MIX = 0;

assign LED_DISK = 0;
assign LED_POWER = 0;
assign BUTTONS = 0;

//////////////////////////////////////////////////////////////////

wire [1:0] ar = status[122:121];

// The PLATO screen is 512x512 with square pixels.
assign VIDEO_ARX = (!ar) ? 12'd1 : (ar - 1'd1);
assign VIDEO_ARY = (!ar) ? 12'd1 : 12'd0;

`include "build_id.v"
localparam CONF_STR = {
	"PLATO;;",
	"-;",
	"O[1],Connection,Port 5004 (auto),Port 8005 (ASCII);",
	"O[4:2],Colors,Orange,White,Green,Amber,Blue,Paper;",
	"O[5],Numeric keypad,Arrows,Numbers;",
	"O[8],Keyboard layout,US,Italian;",
	"O[9],Sound,On,Off;",
	"-;",
	"O[122:121],Aspect ratio,Original,Full Screen,[ARC1],[ARC2];",
	"-;",
	"R[0],Reset;",
	"v,0;",
	"V,v",`BUILD_DATE
};

wire forced_scandoubler;
wire   [1:0] buttons;
wire [127:0] status;
wire  [10:0] ps2_key;

hps_io #(.CONF_STR(CONF_STR)) hps_io
(
	.clk_sys(clk_sys),
	.HPS_BUS(HPS_BUS),
	.EXT_BUS(),
	.gamma_bus(),

	.forced_scandoubler(forced_scandoubler),

	.buttons(buttons),
	.status(status),
	.status_menumask(16'd0),

	.ps2_key(ps2_key)
);

///////////////////////   CLOCKS   ///////////////////////////////

wire clk_sys;   // 40 MHz: VESA 800x600@60 pixel clock
pll pll
(
	.refclk(CLK_50M),
	.rst(0),
	.outclk_0(clk_sys)
);

wire reset = RESET;

// Reset requests (OSD "Reset" or the user button) are counted and passed
// to platod, which resets the terminal and reconnects when the count changes.
reg  [3:0] reconn_cnt = 0;
reg        old_reconn = 0;
wire       reconn_req = status[0] | buttons[1];

always @(posedge clk_sys) begin
	old_reconn <= reconn_req;
	if (reconn_req && !old_reconn) reconn_cnt <= reconn_cnt + 1'd1;
end

wire [31:0] status_word = {reconn_cnt, status[27:0]};

///////////////////////   VIDEO + DDR3   /////////////////////////

wire        fetch_req, frame_start;
wire  [8:0] fetch_row;
wire        lb_we;
wire  [8:0] lb_addr;
wire [47:0] lb_data;
wire        alive;

wire HBlank, VBlank, HSync, VSync;
wire [7:0] R, G, B;

plato_video video
(
	.clk(clk_sys),
	.reset(reset),
	.enable(alive),
	.fetch_req(fetch_req),
	.fetch_row(fetch_row),
	.frame_start(frame_start),
	.lb_we(lb_we),
	.lb_addr(lb_addr),
	.lb_data(lb_data),
	.HBlank(HBlank),
	.VBlank(VBlank),
	.HSync(HSync),
	.VSync(VSync),
	.R(R),
	.G(G),
	.B(B)
);

assign DDRAM_CLK = clk_sys;

plato_ddr ddr
(
	.clk(clk_sys),
	.reset(reset),
	.DDRAM_BUSY(DDRAM_BUSY),
	.DDRAM_BURSTCNT(DDRAM_BURSTCNT),
	.DDRAM_ADDR(DDRAM_ADDR),
	.DDRAM_DOUT(DDRAM_DOUT),
	.DDRAM_DOUT_READY(DDRAM_DOUT_READY),
	.DDRAM_RD(DDRAM_RD),
	.DDRAM_DIN(DDRAM_DIN),
	.DDRAM_BE(DDRAM_BE),
	.DDRAM_WE(DDRAM_WE),
	.fetch_req(fetch_req),
	.fetch_row(fetch_row),
	.frame_start(frame_start),
	.lb_we(lb_we),
	.lb_addr(lb_addr),
	.lb_data(lb_data),
	.alive(alive),
	.ps2_key(ps2_key),
	.status_word(status_word)
);

assign CLK_VIDEO = clk_sys;
assign CE_PIXEL  = 1;

assign VGA_DE = ~(HBlank | VBlank);
assign VGA_HS = HSync;
assign VGA_VS = VSync;
assign VGA_R  = R;
assign VGA_G  = G;
assign VGA_B  = B;

// LED: on while platod is running
assign LED_USER = alive;

endmodule
