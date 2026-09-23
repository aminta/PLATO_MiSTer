//============================================================================
//  PLATO terminal core - DDR3 interface
//
//  Shares the DDR3 region at physical 0x30000000 with platod on the ARM
//  (see daemon/src/shared.h for the memory contract):
//
//   * reads frame buffer lines (512 pixels x 32 bit) into the line buffer
//   * reads the "alive" word and the flags written by platod once per frame
//   * writes keyboard events into an event ring and a header word with
//     the OSD status bits and the event sequence number
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//============================================================================

module plato_ddr
(
	input             clk,
	input             reset,

	// DDR3 (Avalon-MM, 64 bit)
	input             DDRAM_BUSY,
	output reg  [7:0] DDRAM_BURSTCNT,
	output reg [28:0] DDRAM_ADDR,
	input      [63:0] DDRAM_DOUT,
	input             DDRAM_DOUT_READY,
	output reg        DDRAM_RD,
	output reg [63:0] DDRAM_DIN,
	output      [7:0] DDRAM_BE,
	output reg        DDRAM_WE,

	// video side
	input             fetch_req,
	input       [8:0] fetch_row,
	input             frame_start,
	output reg        lb_we,
	output reg  [8:0] lb_addr,
	output reg [47:0] lb_data,
	output reg        alive,
	output reg [31:0] flags,        // from platod, see shared.h

	// to the ARM
	input      [10:0] ps2_key,
	input             mouse_stb,
	input      [31:0] mouse_evt,
	input      [31:0] status_word
);

// Word (8 byte) addresses
localparam [28:0] FB_BASE    = 29'h0600_0000;   // 0x30000000
localparam [28:0] CTL_HEADER = 29'h0602_0000;   // 0x30100000
localparam [28:0] CTL_ALIVE  = 29'h0602_0001;   // 0x30100008
localparam [28:0] CTL_RING   = 29'h0602_0020;   // 0x30100100
localparam [31:0] ALIVE_MAGIC = 32'h504C4154;   // "PLAT"

assign DDRAM_BE = 8'hFF;

// ---------------------------------------------------------------------------
// Keyboard event FIFO
// ---------------------------------------------------------------------------

reg [31:0] fifo[16];
reg  [3:0] fifo_wp, fifo_rp;
reg        old_key_stb;
reg        mouse_pending;
reg [31:0] mouse_data;

wire fifo_empty = (fifo_wp == fifo_rp);

always @(posedge clk) begin
	if (reset) begin
		fifo_wp <= 0;
		old_key_stb <= ps2_key[10];
		mouse_pending <= 0;
	end
	else begin
		old_key_stb <= ps2_key[10];
		if (mouse_stb) begin
			mouse_pending <= 1;
			mouse_data <= mouse_evt;
		end
		if (old_key_stb != ps2_key[10]) begin
			if ((fifo_wp + 1'd1) != fifo_rp) begin
				fifo[fifo_wp] <= {2'b00, 19'd0, ps2_key};   // EVT_KEY
				fifo_wp <= fifo_wp + 1'd1;
			end
		end
		else if (mouse_pending && !mouse_stb) begin
			mouse_pending <= 0;
			if ((fifo_wp + 1'd1) != fifo_rp) begin
				fifo[fifo_wp] <= mouse_data;                // EVT_MOUSE
				fifo_wp <= fifo_wp + 1'd1;
			end
		end
	end
end

// ---------------------------------------------------------------------------
// Request scheduler
// ---------------------------------------------------------------------------

localparam S_IDLE = 0, S_RD_REQ = 1, S_RD_DATA = 2, S_WR = 3;

reg  [1:0] state;
reg        fetch_pending;
reg  [8:0] row;          // requested row
reg  [8:0] cur_row;      // row being fetched
reg        half;
reg        alive_pending;
reg        hdr_pending;
reg [31:0] seq;

wire [7:0] next_slot = seq[7:0] + 8'd1;

reg        rd_fetch;     // current read is a frame buffer line (else alive)
reg        rd_busy;
reg  [7:0] rd_cnt;

always @(posedge clk) begin
	lb_we <= 0;

	if (reset) begin
		state <= S_IDLE;
		DDRAM_RD <= 0;
		DDRAM_WE <= 0;
		fetch_pending <= 0;
		alive_pending <= 0;
		hdr_pending <= 1;
		rd_busy <= 0;
		fifo_rp <= 0;
		seq <= 0;
		alive <= 0;
		flags <= 0;
	end
	else begin
		if (fetch_req) begin
			fetch_pending <= 1;
			row <= fetch_row;
		end
		if (frame_start) begin
			alive_pending <= 1;
			hdr_pending <= 1;
		end

		// Read data return
		if (DDRAM_DOUT_READY && rd_busy) begin
			if (rd_fetch) begin
				lb_we   <= 1;
				lb_addr <= {cur_row[0], half, rd_cnt[6:0]};
				lb_data <= {DDRAM_DOUT[55:32], DDRAM_DOUT[23:0]};
			end
			else begin
				alive <= (DDRAM_DOUT[31:0] == ALIVE_MAGIC);
				flags <= DDRAM_DOUT[63:32];
			end
			rd_cnt <= rd_cnt + 1'd1;
			if (rd_cnt == DDRAM_BURSTCNT - 1'd1) rd_busy <= 0;
		end

		case (state)
			S_IDLE:
				if (fetch_pending && !fetch_req) begin
					fetch_pending  <= 0;
					cur_row        <= row;
					half           <= 0;
					rd_fetch       <= 1;
					rd_cnt         <= 0;
					rd_busy        <= 1;
					DDRAM_ADDR     <= FB_BASE + {row, 8'h00};
					DDRAM_BURSTCNT <= 8'd128;
					DDRAM_RD       <= 1;
					state          <= S_RD_REQ;
				end
				else if (alive_pending) begin
					alive_pending  <= 0;
					rd_fetch       <= 0;
					rd_cnt         <= 0;
					rd_busy        <= 1;
					DDRAM_ADDR     <= CTL_ALIVE;
					DDRAM_BURSTCNT <= 8'd1;
					DDRAM_RD       <= 1;
					state          <= S_RD_REQ;
				end
				else if (!fifo_empty) begin
					// event entry: [31:0] event, [63:32] sequence number
					DDRAM_ADDR     <= CTL_RING + next_slot;
					DDRAM_BURSTCNT <= 8'd1;
					DDRAM_DIN      <= {seq + 1'd1, fifo[fifo_rp]};
					DDRAM_WE       <= 1;
					seq            <= seq + 1'd1;
					fifo_rp        <= fifo_rp + 1'd1;
					hdr_pending    <= 1;
					state          <= S_WR;
				end
				else if (hdr_pending) begin
					// header: [31:0] status, [63:32] last sequence number
					hdr_pending    <= 0;
					DDRAM_ADDR     <= CTL_HEADER;
					DDRAM_BURSTCNT <= 8'd1;
					DDRAM_DIN      <= {seq, status_word};
					DDRAM_WE       <= 1;
					state          <= S_WR;
				end

			S_RD_REQ:
				if (!DDRAM_BUSY) begin
					DDRAM_RD <= 0;
					state    <= S_RD_DATA;
				end

			S_RD_DATA:
				if (!rd_busy) begin
					if (rd_fetch && !half) begin
						// second half of the line
						half           <= 1;
						rd_cnt         <= 0;
						rd_busy        <= 1;
						DDRAM_ADDR     <= FB_BASE + {cur_row, 8'h80};
						DDRAM_RD       <= 1;
						state          <= S_RD_REQ;
					end
					else state <= S_IDLE;
				end

			S_WR:
				if (!DDRAM_BUSY) begin
					DDRAM_WE <= 0;
					state    <= S_IDLE;
				end
		endcase
	end
end

endmodule
