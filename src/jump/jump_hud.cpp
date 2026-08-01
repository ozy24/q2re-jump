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
	const int64_t thousandths = ms % 1000;

	ent->client->ps.stats[JUMP_STAT_ENABLED] = 1;

	ent->client->ps.stats[JUMP_STAT_TIME_SEC] = (int16_t) min<int64_t>(ms / 1000, 9999);
	ent->client->ps.stats[JUMP_STAT_TIME_HUN_TENS] = (int16_t) (thousandths / 100);
	ent->client->ps.stats[JUMP_STAT_TIME_HUN_UNITS] = (int16_t) ((thousandths / 10) % 10);
	ent->client->ps.stats[JUMP_STAT_TIME_THOU] = (int16_t) (thousandths % 10);

	ent->client->ps.stats[JUMP_STAT_RUN_STATE] = (int16_t) jc->state;
	ent->client->ps.stats[JUMP_STAT_STORES] = (int16_t) jc->stores.count;
	ent->client->ps.stats[JUMP_STAT_TEAM_PRACTICE] = jc->team == jump_team_t::practice;
	ent->client->ps.stats[JUMP_STAT_TEAM_RANKED] = jc->team == jump_team_t::ranked;

	// The text itself is refreshed only when pb_time_ms changes
	// (Jump_UpdatePbString); this just points the stat at the player's own
	// slot, gated the same way JUMP_STAT_PB_SEC used to be (0 = no PB yet).
	const ptrdiff_t pb_index = ent->client - game.clients;

	ent->client->ps.stats[JUMP_STAT_PB_STRING] =
		(jc->pb_time_ms > 0 && pb_index >= 0 && pb_index < JUMP_MAX_PB_STRING_CLIENTS)
			? (int16_t) (CONFIG_JUMP_PB_STRING + pb_index)
			: 0;

	ent->client->ps.stats[JUMP_STAT_CHECKPOINTS] = (int16_t) jc->checkpoints;
	ent->client->ps.stats[JUMP_STAT_CHECKPOINT_TOTAL] = (int16_t) Jump_CheckpointTotal();
}

void Jump_UpdatePbString(edict_t *ent)
{
	if (!Jump_Active())
		return;

	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc || jc->pb_time_ms <= 0)
		return;

	const ptrdiff_t index = ent->client - game.clients;

	if (index < 0 || index >= JUMP_MAX_PB_STRING_CLIENTS)
		return;

	gi.configstring((int) (CONFIG_JUMP_PB_STRING + index), jump::FormatTime(jc->pb_time_ms).c_str());
}

bool Jump_InitStatusbar()
{
	if (!Jump_Active())
		return false;

	statusbar_t sb;

	// Right-edge column: a short label above each number block. Offsets are
	// virtual px from the right edge (negative). Text is left-aligned from the
	// cursor, so each label's xr leaves room for its own width.
	//
	//            time
	//         1234.56
	//      Checkpoint
	//             0/1
	//              PB
	//            9.87
	//
	//                        PRACTICE   (bottom right)
	//
	constexpr int right = -8; // last digit column ends here

	// Digits are 24 tall, so a separator drawn in the 8px text font has to sit
	// near the bottom of the row to read as a decimal point rather than a
	// mid-dot.
	constexpr int sep_drop = 15;
	constexpr int label_h = 8;
	constexpr int row_gap = 8;

	// Timer: label, then seconds (4 digits) . tenths hundredths thousandths.
	constexpr int t_thou = right - Jump_NumWidth(1);
	constexpr int t_units = t_thou - Jump_NumWidth(1);
	constexpr int t_tens = t_units - Jump_NumWidth(1);
	constexpr int t_dot = t_tens - 7;
	constexpr int t_secs = t_dot - Jump_NumWidth(4);
	constexpr int t_label_y = 4;
	constexpr int t_y = t_label_y + label_h + 2;

	sb.yt(t_label_y).xr(right - 32).string2("time");
	sb.yt(t_y).xr(t_secs).num(4, JUMP_STAT_TIME_SEC);
	sb.yt(t_y + sep_drop).xr(t_dot).string(".");
	sb.yt(t_y).xr(t_tens).num(1, JUMP_STAT_TIME_HUN_TENS);
	sb.yt(t_y).xr(t_units).num(1, JUMP_STAT_TIME_HUN_UNITS);
	sb.yt(t_y).xr(t_thou).num(1, JUMP_STAT_TIME_THOU);

	// Checkpoints. Old maps go up to 28, so both fields are two digits wide;
	// a one-digit value simply leaves its left cell blank.
	constexpr int cp_total = right - Jump_NumWidth(2);
	constexpr int cp_slash = cp_total - 5;
	constexpr int cp_have = cp_slash - Jump_NumWidth(2);
	constexpr int cp_label_y = t_y + 24 + row_gap;
	constexpr int cp_y = cp_label_y + label_h + 2;

	sb.ifstat(JUMP_STAT_CHECKPOINT_TOTAL)
		.yt(cp_label_y)
		.xr(right - 80)
		.string2("Checkpoint")
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

	// Personal best is a stat_string (arbitrary text), not digit stats, so it
	// can't use the big chunky num font the timer/checkpoints use - see
	// jump_stats.h for why it's a stat_string at all. Small text reads better
	// grouped with other small text than orphaned under the big digit stack,
	// so it sits one row above the team-mode row, sharing its anchor style.
	//
	// PRACTICE (8 chars, xr -76) and RANKED (6 chars, xr -60) both end at the
	// same right edge, xr(-12), since string2 draws at CONCHAR_WIDTH (8) per
	// char: -76+8*8 = -60+6*8 = -12. A stat_string can't match that - it has
	// no fixed width to anchor by - but loc_stat_rstring measures the actual
	// rendered text and draws it ending AT the given x, so it can.
	constexpr int pb_yb = -16 - row_gap - label_h; // one row above the team label row
	constexpr int pb_right = -12;					// matches PRACTICE/RANKED's right edge

	// "PB " (3 chars) plus 8 chars of headroom for the value ("9999.999" is
	// jump::FormatTime's worst case) - overkill for a typical "SS.mmm" run,
	// but only a corner case would ever need it and label/value must not
	// overlap when it does.
	constexpr int pb_label_x = pb_right - (3 + 8) * 8;

	sb.ifstat(JUMP_STAT_PB_STRING)
		.yb(pb_yb)
		.xr(pb_label_x)
		.string2("PB")
		.yb(pb_yb)
		.xr(pb_right)
		.loc_stat_rstring(JUMP_STAT_PB_STRING)
		.endifstat();

	// Team mode lives in the bottom-right corner, away from the run column.
	sb.ifstat(JUMP_STAT_TEAM_PRACTICE).yb(-16).xr(-76).string2("PRACTICE").endifstat();
	sb.ifstat(JUMP_STAT_TEAM_RANKED).yb(-16).xr(-60).string2("RANKED").endifstat();

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
