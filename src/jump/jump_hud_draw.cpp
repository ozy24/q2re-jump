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

void Jump_InitClientCvars()
{
	// Default off: the stock-client view is the one everybody shares, so it is
	// the honest thing to show by default.
	jump_hud = cgi.cvar("jump_hud", "0", CVAR_ARCHIVE);

	// The speed readout: the gain/loss figure, plus the number itself on a
	// server that has turned its own speedometer off.
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

// The speed number, drawn only on a server that has turned its own off.
//
// Two annotations used to live here and both were cut after seeing them in
// play. Peak-of-jump was a high-water mark, so it went stale the moment you
// stopped matching it, and since speed barely changes in flight it mostly
// duplicated the number underneath it. The signed gain/loss figure was honest
// but noisy - a second thing moving next to a number you are trying to read.
//
// What replaces them should answer a question the live number cannot, rather
// than restating it: speed at takeoff against the previous jump, or how much of
// the available acceleration a strafe actually captured. jump::speed_state_t
// still tracks peak and trend because both want the same ground-edge state.
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

	// Nothing to add when the statusbar is already showing a number - that is
	// the normal case, and two speeds a few units apart would look like a bug.
	if (ctx.ps->stats[JUMP_STAT_SPEED] > 0)
		return;

	if (speed.current < 1.f)
		return;

	// Held for the same span as the statusbar's, and for the same reason: this
	// one is sampled per rendered frame, so left alone it would churn faster
	// still. The value shown is a real instantaneous reading, not an average -
	// only the moment it is taken is rationed.
	static int32_t	shown = 0;
	static uint64_t shown_time = 0;

	const uint64_t now = cgi.CL_ClientTime();

	if (now < shown_time || now - shown_time >= (uint64_t) JUMP_SPEED_REFRESH_MS)
	{
		shown = (int32_t) speed.current;
		shown_time = now;
	}

	// In the middle of the row the statusbar's digits would have occupied, whose
	// anchor is shared rather than copied (jump_stats.h) so moving the block
	// moves this with it. No caption, for the same reason that one has none.
	cgi.SCR_DrawFontString(jump::FormatSpeed((float) shown).c_str(), ctx.VirtX(160.f),
						   ctx.BottomY(JUMP_SPEED_DIGITS_YB + 8.f), ctx.scale, rgba_white, true,
						   text_align_t::CENTER);
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
