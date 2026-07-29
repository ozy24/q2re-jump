// [Jump] Server-side HUD: per-frame stats and the jump statusbar.
//
// This is the HUD everyone gets, including players on a stock client, so it
// uses only layout tokens the vanilla interpreter understands.
//
// Laying it out means working with the `num` token's quirks (cg_screen.cpp,
// CG_DrawField):
//   - a `num W S` field box starts at the cursor and is (2 + 16*W) virtual px
//     wide, with the digits RIGHT-aligned inside it;
//   - short values are not padded, they just leave the left of the box blank;
//   - over-wide values are truncated to their LEADING digits, so 1234 in a
//     width-3 field draws "123" - fields must be sized for the worst case;
//   - the digit font has 0-9 and minus only. No period, colon or slash, so
//     separators are drawn as small text between fields.
// The cursor is never advanced by drawing, so every element positions itself.

#include "../g_local.h"
#include "jump_local.h"
#include "../g_statusbar.h"

int Jump_CheckpointTotal();

// Field box width in virtual px for a `num` of the given digit count.
static constexpr int Jump_NumWidth(int digits)
{
	return 2 + 16 * digits;
}

void Jump_SetStats(edict_t *ent)
{
	if (!Jump_Active())
		return;

	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return;

	const int64_t ms = Jump_RunTimeMs(*jc);
	const int64_t hundredths = (ms % 1000) / 10;

	const int64_t pb_ms = jc->pb_time_ms;
	const int64_t pb_hundredths = (pb_ms % 1000) / 10;

	ent->client->ps.stats[JUMP_STAT_ENABLED] = 1;

	ent->client->ps.stats[JUMP_STAT_TIME_SEC] = (int16_t) min<int64_t>(ms / 1000, 9999);
	ent->client->ps.stats[JUMP_STAT_TIME_HUN_TENS] = (int16_t) (hundredths / 10);
	ent->client->ps.stats[JUMP_STAT_TIME_HUN_UNITS] = (int16_t) (hundredths % 10);

	ent->client->ps.stats[JUMP_STAT_RUN_STATE] = (int16_t) jc->state;
	ent->client->ps.stats[JUMP_STAT_STORES] = (int16_t) jc->stores.count;
	ent->client->ps.stats[JUMP_STAT_TEAM_PRACTICE] = jc->team == jump_team_t::practice;
	ent->client->ps.stats[JUMP_STAT_TEAM_RANKED] = jc->team == jump_team_t::ranked;

	ent->client->ps.stats[JUMP_STAT_PB_SEC] = (int16_t) min<int64_t>(pb_ms / 1000, 9999);
	ent->client->ps.stats[JUMP_STAT_PB_HUN_TENS] = (int16_t) (pb_hundredths / 10);
	ent->client->ps.stats[JUMP_STAT_PB_HUN_UNITS] = (int16_t) (pb_hundredths % 10);

	ent->client->ps.stats[JUMP_STAT_CHECKPOINTS] = (int16_t) jc->checkpoints;
	ent->client->ps.stats[JUMP_STAT_CHECKPOINT_TOTAL] = (int16_t) Jump_CheckpointTotal();
}

bool Jump_InitStatusbar()
{
	if (!Jump_Active())
		return false;

	statusbar_t sb;

	// A two-column grid hanging off the right edge: labels share one column so
	// they line up vertically, numbers all right-align to the same anchor.
	// Offsets are virtual px from the right edge, so they run negative.
	//
	//         1234.56      run timer
	//   cp       0/1       checkpoints, only when the map uses them
	//        PRACTICE      team
	//   PB       9.87      personal best, only once you have one
	//
	constexpr int right = -8;	  // last digit column ends here
	constexpr int label = -152;	  // shared left column for "cp" and "PB"

	// Digits are 24 tall, so a separator drawn in the 8px text font has to sit
	// near the bottom of the row to read as a decimal point rather than a
	// mid-dot.
	constexpr int sep_drop = 15;

	// Timer: seconds (4 digits) . tens units, walking left from the anchor.
	constexpr int t_units = right - Jump_NumWidth(1);
	constexpr int t_tens = t_units - Jump_NumWidth(1);
	constexpr int t_dot = t_tens - 7;
	constexpr int t_secs = t_dot - Jump_NumWidth(4);
	constexpr int t_y = 8;

	sb.yt(t_y).xr(t_secs).num(4, JUMP_STAT_TIME_SEC);
	sb.yt(t_y + sep_drop).xr(t_dot).string(".");
	sb.yt(t_y).xr(t_tens).num(1, JUMP_STAT_TIME_HUN_TENS);
	sb.yt(t_y).xr(t_units).num(1, JUMP_STAT_TIME_HUN_UNITS);

	// Checkpoints. Old maps go up to 28, so both fields are two digits wide;
	// a one-digit value simply leaves its left cell blank.
	constexpr int cp_total = right - Jump_NumWidth(2);
	constexpr int cp_slash = cp_total - 5;
	constexpr int cp_have = cp_slash - Jump_NumWidth(2);
	constexpr int cp_y = 40;

	sb.ifstat(JUMP_STAT_CHECKPOINT_TOTAL)
		.yt(cp_y + 4)
		.xr(label)
		.string2("cp")
		.yt(cp_y)
		.xr(cp_have)
		.num(2, JUMP_STAT_CHECKPOINTS)
		.yt(cp_y + 6)
		.xr(cp_slash)
		.string("/")
		.yt(cp_y)
		.xr(cp_total)
		.num(2, JUMP_STAT_CHECKPOINT_TOTAL)
		.endifstat();

	// Team. Two branches because a layout script can test a stat but cannot
	// pick a string from its value.
	// Text is left-aligned from the cursor, so the x has to leave room for the
	// longest label rather than sitting at the shared right anchor.
	sb.ifstat(JUMP_STAT_TEAM_PRACTICE).yt(72).xr(-76).string2("PRACTICE").endifstat();
	sb.ifstat(JUMP_STAT_TEAM_RANKED).yt(72).xr(-60).string2("RANKED").endifstat();

	// Personal best, same shape as the timer one row down.
	constexpr int pb_units = right - Jump_NumWidth(1);
	constexpr int pb_tens = pb_units - Jump_NumWidth(1);
	constexpr int pb_dot = pb_tens - 7;
	constexpr int pb_secs = pb_dot - Jump_NumWidth(4);
	constexpr int pb_y = 88;

	sb.ifstat(JUMP_STAT_PB_SEC)
		.yt(pb_y + 4)
		.xr(label)
		.string2("PB")
		.yt(pb_y)
		.xr(pb_secs)
		.num(4, JUMP_STAT_PB_SEC)
		.yt(pb_y + sep_drop)
		.xr(pb_dot)
		.string(".")
		.yt(pb_y)
		.xr(pb_tens)
		.num(1, JUMP_STAT_PB_HUN_TENS)
		.yt(pb_y)
		.xr(pb_units)
		.num(1, JUMP_STAT_PB_HUN_UNITS)
		.endifstat();

	// Stores held, bottom left and out of the way of the run column.
	sb.ifstat(JUMP_STAT_STORES).yb(-28).xl(8).string2("stores").yb(-32).xl(64).num(2, JUMP_STAT_STORES).endifstat();

	// Keep the pickup / centerprint line the rest of the game expects.
	sb.yb(-50);
	sb.ifstat(STAT_PICKUP_ICON)
		.xv(0)
		.pic(STAT_PICKUP_ICON)
		.xv(26)
		.yb(-42)
		.loc_stat_string(STAT_PICKUP_STRING)
		.yb(-50)
		.endifstat();

	gi.configstring(CS_STATUSBAR, sb.sb.str().c_str());

	return true;
}
