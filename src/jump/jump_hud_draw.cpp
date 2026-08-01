// [Jump] cgame-side HUD overlay.
//
// This is the only client-side part of the mod, and it is deliberately tiny.
// A stock client that connects to a jump server never runs it and still gets a
// complete HUD, because the timer, checkpoints, stores, team and personal best
// are all drawn by the server-authored statusbar (Jump_InitStatusbar) using
// only tokens the stock layout interpreter understands.
//
// So this file must NOT repeat anything the statusbar draws. It adds the one
// thing a layout script genuinely cannot express: a signed, coloured delta
// against your personal best once a run is banked.
//
// It is off by default, so out of the box everyone - including the host of a
// listen server - sees exactly what a stock client sees. `jump_hud 1` opts in.

#include "../cg_local.h"
#include "jump_logic.h"
#include "jump_stats.h"
#include "jump_hud_draw.h"

#include <cstdio>

static cvar_t *jump_hud;

void Jump_InitClientCvars()
{
	// Default off: the stock-client view is the one everybody shares, so it is
	// the honest thing to show by default.
	jump_hud = cgi.cvar("jump_hud", "0", CVAR_ARCHIVE);
}

void Jump_DrawHud(const player_state_t *ps, vrect_t hud_vrect, int32_t scale)
{
	if (!ps->stats[JUMP_STAT_ENABLED])
		return;

	if (!jump_hud || !jump_hud->integer)
		return;

	if (ps->stats[STAT_LAYOUTS] & LAYOUTS_HIDE_HUD)
		return;

	if (ps->stats[JUMP_STAT_RUN_STATE] != JUMP_RUN_FINISHED)
		return;

	// PB is a stat_string (jump_stats.h) - the stat holds a configstring
	// index, 0 while there is nothing to compare against yet.
	const int32_t pb_index = ps->stats[JUMP_STAT_PB_STRING];

	if (!pb_index)
		return; // first completion, nothing to compare against

	long long pb_sec = 0, pb_ms = 0;
	sscanf(cgi.get_configstring(pb_index), "%lld.%lld", &pb_sec, &pb_ms);
	const int64_t pb_total_ms = pb_sec * 1000 + pb_ms;

	const int64_t run_total_ms = ps->stats[JUMP_STAT_TIME_SEC] * 1000LL +
								  ps->stats[JUMP_STAT_TIME_HUN_TENS] * 100LL +
								  ps->stats[JUMP_STAT_TIME_HUN_UNITS] * 10LL + ps->stats[JUMP_STAT_TIME_THOU];

	const std::string text = jump::FormatDelta(run_total_ms - pb_total_ms);

	// Directly under the statusbar's PB line.
	cgi.SCR_DrawFontString(text.c_str(), (hud_vrect.width - 16.f) * scale, 104.f * scale, scale,
						   run_total_ms <= pb_total_ms ? rgba_green : rgba_red, true, text_align_t::RIGHT);
}
