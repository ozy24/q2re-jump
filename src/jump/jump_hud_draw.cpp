// [Jump] cgame-side HUD overlay.
//
// This is the client half of the mod, and it is deliberately small. A stock
// client that connects to a jump server never runs it and still gets a complete
// HUD, because the timer, checkpoints, stores, team, personal best and speed
// are all drawn by the server-authored statusbar (Jump_InitStatusbar) using
// only tokens the stock layout interpreter understands.
//
// So this file must NOT repeat anything the statusbar draws. It adds the two
// things a layout script genuinely cannot express: a signed, coloured delta
// against your personal best once a run is banked, and - alongside the
// statusbar's speed number, never instead of it - the best speed of the current
// jump and whether you are gaining or losing it. Those come from the movement
// samples in jump_cg_move.cpp, which are per-frame and predicted; a layout
// script sees neither.
//
// It is off by default, so out of the box everyone - including the host of a
// listen server - sees exactly what a stock client sees. `jump_hud 1` opts in.

#include "../cg_local.h"
#include "jump_logic.h"
#include "jump_stats.h"
#include "jump_cg_move.h"
#include "jump_hud_draw.h"

#include <cstdio>

static cvar_t *jump_hud;
static cvar_t *jump_hud_speed;

// Dimmer than the value it annotates, so the eye lands on the number first.
static constexpr rgba_t jump_dim = { 170, 170, 170, 255 };

void Jump_InitClientCvars()
{
	// On by default. This used to be off, on the principle that the host should
	// see what everyone else sees - but the speedometer changed that calculus:
	// the server draws it with the HUD's number pics, which are 16x24 and
	// dominate the screen, so it is off by default server-side
	// (jump_speedometer) and this overlay is where the speed readout normally
	// lives. `jump_hud 0` still gives the exact stock-client view.
	jump_hud = cgi.cvar("jump_hud", "1", CVAR_ARCHIVE);

	// The speed readout: the number itself when the server is not drawing one,
	// plus the peak and trend a layout script cannot express either way.
	jump_hud_speed = cgi.cvar("jump_hud_speed", "1", CVAR_ARCHIVE);

	// CG_InitScreen runs between levels and on reconnect - the same place the
	// vanilla HUD clears its notify state - so this is where stale samples go.
	Jump_CG_ResetSamples();
}

// Shared by every overlay element. The anchor helpers mirror the arithmetic
// CG_ExecuteLayoutString applies to xv / xr / yb, so an element positioned here
// lands exactly where the equivalent statusbar token would put it - including
// the safe-area inset, which the layout applies to edge anchors only.
struct jump_overlay_ctx_t
{
	const player_state_t		*ps;
	vrect_t						 hud_vrect;
	vrect_t						 hud_safe;
	int32_t						 scale;
	const jump::speed_readout_t *speed;

	int32_t VirtX(float virt) const
	{
		return (int32_t) ((hud_vrect.x + hud_vrect.width / 2.f + (virt - 160.f)) * scale);
	}

	int32_t RightX(float virt) const
	{
		return (int32_t) ((hud_vrect.x + hud_vrect.width + virt) * scale) - hud_safe.x;
	}

	int32_t BottomY(float virt) const
	{
		return (int32_t) ((hud_vrect.y + hud_vrect.height + virt) * scale) - hud_safe.y;
	}
};

// The signed delta against your personal best, once a run is banked. A layout
// script cannot do this: the two times are a stat and a configstring, and there
// is no token that subtracts.
static void Jump_DrawPbDelta(const jump_overlay_ctx_t &ctx)
{
	if (ctx.ps->stats[JUMP_STAT_RUN_STATE] != JUMP_RUN_FINISHED)
		return;

	// PB is a stat_string (jump_stats.h) - the stat holds a configstring
	// index, 0 while there is nothing to compare against yet.
	const int32_t pb_index = ctx.ps->stats[JUMP_STAT_PB_STRING];

	if (!pb_index)
		return; // first completion, nothing to compare against

	long long pb_sec = 0, pb_ms = 0;
	sscanf(cgi.get_configstring(pb_index), "%lld.%lld", &pb_sec, &pb_ms);
	const int64_t pb_total_ms = pb_sec * 1000 + pb_ms;

	const int64_t run_total_ms = ctx.ps->stats[JUMP_STAT_TIME_SEC] * 1000LL +
								 ctx.ps->stats[JUMP_STAT_TIME_HUN_TENS] * 100LL +
								 ctx.ps->stats[JUMP_STAT_TIME_HUN_UNITS] * 10LL + ctx.ps->stats[JUMP_STAT_TIME_THOU];

	const std::string text = jump::FormatDelta(run_total_ms - pb_total_ms);

	// In the right-hand run column, below the checkpoint block.
	cgi.SCR_DrawFontString(text.c_str(), ctx.RightX(-16), (int32_t) (104.f * ctx.scale), ctx.scale,
						   run_total_ms <= pb_total_ms ? rgba_green : rgba_red, true, text_align_t::RIGHT);
}

// The speed readout: peak and trend always, and the number itself when the
// server is not drawing one.
//
// Whether it is comes straight from the stat. Jump_SetStats zeroes
// JUMP_STAT_SPEED when jump_speedometer is off, and the statusbar row is gated
// on that value, so a zero means "no number on screen" - which is exactly the
// question this needs answered, and it needs no cvar the client cannot see.
// The one ambiguity is harmless: the stat is also 0 when you are standing
// still, and the number this would draw instead is then 0 too, which is hidden
// below anyway.
//
// Drawing our own number when the server already has is the case to avoid. Two
// speeds a few units apart on the same screen look like a bug, and they would
// differ: this one is predicted and per-frame, that one is a server snapshot.
static void Jump_DrawSpeedExtras(const jump_overlay_ctx_t &ctx)
{
	const jump::speed_readout_t &speed = *ctx.speed;

	if (!speed.valid)
		return;

	// A free-flying camera's speed is noise. PM_FREEZE is kept, because that is
	// the eyecam case, where the reading is the followed player's.
	const pmtype_t pm_type = ctx.ps->pmove.pm_type;

	if (pm_type == PM_NOCLIP || pm_type == PM_SPECTATOR)
		return;

	// Standing still hides the statusbar's number, so hide its annotations too
	// rather than leave them floating over nothing.
	if (speed.peak < 1.f)
		return;

	// The statusbar's digit row is 24 tall and starts at JUMP_SPEED_DIGITS_YB
	// (shared rather than copied, jump_stats.h, so moving the block moves this
	// with it). Our own number goes in the middle of that row when there is no
	// row, and peak/trend sit one line above either way.
	const bool	  server_drawing = ctx.ps->stats[JUMP_STAT_SPEED] > 0;
	const int32_t line_height = (int32_t) cgi.SCR_FontLineHeight(ctx.scale);
	const int32_t speed_y = ctx.BottomY(JUMP_SPEED_DIGITS_YB + 8.f);
	const int32_t y = speed_y - line_height - 2 * ctx.scale;

	// The current-speed test is separate from the peak one above: peak lingers
	// for a moment after you stop, and a lone "0" sitting there is exactly what
	// the statusbar's own zero gate exists to avoid.
	if (!server_drawing && speed.current >= 1.f)
	{
		// No label: the number is the only thing at this spot, and "Speed"
		// under it is the sort of caption you read once and never again.
		cgi.SCR_DrawFontString(jump::FormatSpeed(speed.current).c_str(), ctx.VirtX(160.f), speed_y, ctx.scale,
							   rgba_white, true, text_align_t::CENTER);
	}

	const std::string peak = "peak " + jump::FormatSpeed(speed.peak);
	const std::string delta = speed.trend ? jump::FormatSpeedDelta(speed.delta) : std::string();

	// Two colours means two draws, so the line has to be centred by hand:
	// measure both parts, then lay them out from the left of the pair. Measured
	// rather than counted because the kfont is proportional and scr_usekfont is
	// a private static of cg_screen.cpp - a character-width estimate would
	// drift under one font or the other.
	const float peak_width = cgi.SCR_MeasureFontString(peak.c_str(), ctx.scale).x;
	const float gap = delta.empty() ? 0.f : 6.f * ctx.scale;
	const float delta_width = delta.empty() ? 0.f : cgi.SCR_MeasureFontString(delta.c_str(), ctx.scale).x;

	const int32_t left = ctx.VirtX(160.f) - (int32_t) ((peak_width + gap + delta_width) / 2.f);

	cgi.SCR_DrawFontString(peak.c_str(), left, y, ctx.scale, jump_dim, true, text_align_t::LEFT);

	if (delta.empty())
		return;

	cgi.SCR_DrawFontString(delta.c_str(), left + (int32_t) (peak_width + gap), y, ctx.scale,
						   speed.trend > 0 ? rgba_green : rgba_red, true, text_align_t::LEFT);
}

void Jump_DrawHud(int32_t isplit, const player_state_t *ps, vrect_t hud_vrect, vrect_t hud_safe, int32_t scale)
{
	// Prediction only ever runs for the primary local player, and the sampler
	// below must commit exactly once per rendered frame.
	if (isplit != 0)
		return;

	if (!ps->stats[JUMP_STAT_ENABLED])
	{
		// Not a jump level - a stock deathmatch map on the same DLL must not
		// accumulate history, and this also stops the Pmove wrapper sampling.
		Jump_CG_ResetSamples();
		return;
	}

	const bool enabled = jump_hud && jump_hud->integer;
	const bool want_speed = enabled && jump_hud_speed && jump_hud_speed->integer;

	// Called before the display gates, and every frame: it owns the sampling
	// flag the Pmove wrapper reads, so skipping it would leave the wrapper
	// collecting samples nobody is going to draw.
	const jump::speed_readout_t &speed = Jump_CG_SampleFrame(ps, want_speed);

	if (!enabled)
		return;

	if (ps->stats[STAT_LAYOUTS] & (LAYOUTS_HIDE_HUD | LAYOUTS_INTERMISSION))
		return;

	const jump_overlay_ctx_t ctx { ps, hud_vrect, hud_safe, scale, &speed };

	if (want_speed)
		Jump_DrawSpeedExtras(ctx);

	Jump_DrawPbDelta(ctx);
}
