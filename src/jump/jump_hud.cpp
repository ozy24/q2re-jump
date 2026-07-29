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
	ent->client->ps.stats[JUMP_STAT_TEAM_EASY] = jc->team == jump_team_t::easy;
	ent->client->ps.stats[JUMP_STAT_TEAM_HARD] = jc->team == jump_team_t::hard;

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

	// Everything hangs off the right edge in one column. Offsets are virtual
	// px from that edge, so they are negative and get more negative leftwards.
	//
	//         1234.56      run timer
	//      cp    0/ 1      checkpoints, only when the map uses them
	//            EASY      team
	//      PB    9.87      personal best, only once you have one
	//
	// Right-hand anchor: the last digit column ends 8px in from the edge.
	constexpr int right = -8;

	// Timer: seconds (4 digits) . tens units. Walk leftwards from the anchor.
	constexpr int t_units = right - Jump_NumWidth(1);			  // units digit box
	constexpr int t_tens = t_units - Jump_NumWidth(1);			  // tens digit box
	constexpr int t_dot = t_tens - 6;							  // the "." itself
	constexpr int t_secs = t_dot - Jump_NumWidth(4);				  // seconds box

	sb.yt(8).xr(t_secs).num(4, JUMP_STAT_TIME_SEC);
	sb.yt(16).xr(t_dot).string(".");
	sb.yt(8).xr(t_tens).num(1, JUMP_STAT_TIME_HUN_TENS);
	sb.yt(8).xr(t_units).num(1, JUMP_STAT_TIME_HUN_UNITS);

	// Checkpoints. Totals go to 28 on old maps, so both fields are 2 wide.
	constexpr int cp_total = right - Jump_NumWidth(2);
	constexpr int cp_slash = cp_total - 6;
	constexpr int cp_have = cp_slash - Jump_NumWidth(2);
	constexpr int cp_label = cp_have - 26;

	sb.ifstat(JUMP_STAT_CHECKPOINT_TOTAL)
		.yt(44)
		.xr(cp_label)
		.string2("cp")
		.yt(40)
		.xr(cp_have)
		.num(2, JUMP_STAT_CHECKPOINTS)
		.yt(48)
		.xr(cp_slash)
		.string("/")
		.yt(40)
		.xr(cp_total)
		.num(2, JUMP_STAT_CHECKPOINT_TOTAL)
		.endifstat();

	// Team. Two branches because a layout script can test a stat but cannot
	// pick a string from its value.
	sb.ifstat(JUMP_STAT_TEAM_EASY).yt(72).xr(-40).string2("EASY").endifstat();
	sb.ifstat(JUMP_STAT_TEAM_HARD).yt(72).xr(-40).string2("HARD").endifstat();

	// Personal best, same shape as the timer one row down.
	constexpr int pb_units = right - Jump_NumWidth(1);
	constexpr int pb_tens = pb_units - Jump_NumWidth(1);
	constexpr int pb_dot = pb_tens - 6;
	constexpr int pb_secs = pb_dot - Jump_NumWidth(4);
	constexpr int pb_label = pb_secs - 26;

	sb.ifstat(JUMP_STAT_PB_SEC)
		.yt(92)
		.xr(pb_label)
		.string2("PB")
		.yt(88)
		.xr(pb_secs)
		.num(4, JUMP_STAT_PB_SEC)
		.yt(96)
		.xr(pb_dot)
		.string(".")
		.yt(88)
		.xr(pb_tens)
		.num(1, JUMP_STAT_PB_HUN_TENS)
		.yt(88)
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
