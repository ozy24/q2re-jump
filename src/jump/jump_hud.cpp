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

// How long a speedometer reading stays on screen before it is replaced. See
// the note in Jump_SetStats; the cgame overlay holds its own number for the
// same span (JUMP_SPEED_REFRESH_MS, jump_stats.h).
constexpr gtime_t JUMP_SPEED_REFRESH = gtime_t::from_ms(JUMP_SPEED_REFRESH_MS);

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
	// Stores survive a switch to Ranked but cannot be used there, so the counter
	// is reported only on Practice - otherwise the HUD advertises recalls that
	// Jump_CmdRecall will refuse.
	ent->client->ps.stats[JUMP_STAT_STORES] =
		(jc->team == jump_team_t::practice) ? (int16_t) jc->stores.count : 0;
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

	// Horizontal speed, computed here rather than reusing p_view.cpp's
	// file-scope `xyspeed`: that global is also written by
	// target_camera_dummy_think, and the intermission path calls G_SetStats
	// *before* xyspeed is assigned, so it would publish a stale value there.
	// One sqrt per client per frame is cheaper than that coupling.
	//
	// 0 hides the element, which is what a free spectator gets - a flying
	// camera's speed is not "speed" in any jumping sense. A CHASING spectator
	// never reaches this line (G_SetSpectatorStats skips G_SetStats when
	// chase_target is set) and inherits the followed player's value through
	// G_CheckChaseStats' bulk copy, which is exactly the behaviour we want.
	//
	// The jump_speedometer cvar rides the same gate rather than rebuilding the
	// layout: CS_STATUSBAR is a broadcast, so re-emitting it to toggle one row
	// would cost every client the whole bar, whereas a stat is per-player and
	// delta-compressed. It also means the client overlay can tell whether the
	// server is drawing a speed number just by looking at the stat.
	const int16_t speed = (jc->team == jump_team_t::spectator || !jump_speedometer->integer)
							  ? 0
							  : (int16_t) jump::SpeedStat(ent->velocity[0], ent->velocity[1]);

	// Both upstream mods ran on a 10 Hz server, so their speedometers changed
	// ten times a second. The rerelease runs at 40, and four digits changing
	// forty times a second is not a number anyone can read - it just blurs. So
	// hold each reading; the value is still the true instantaneous speed, it is
	// simply sampled at a rate an eye can follow.
	//
	// Appearing and disappearing is exempt: that is the element showing up when
	// you start moving, and waiting a tenth of a second for it would read as a
	// bug rather than as a considered refresh rate.
	if ((speed == 0) != (jc->speed_shown == 0) || level.time - jc->speed_shown_time >= JUMP_SPEED_REFRESH)
	{
		jc->speed_shown = speed;
		jc->speed_shown_time = level.time;
	}

	ent->client->ps.stats[JUMP_STAT_SPEED] = jc->speed_shown;

	// The banner is the same for everyone, which is what makes setting it here
	// enough. G_CheckChaseStats bulk-copies the stats array from the player
	// being chased, so a viewer-specific value would be clobbered - this one
	// survives the copy because it is identical either side of it. A free
	// spectator gets it because G_SetSpectatorStats falls through to G_SetStats
	// when there is no chase target.
	ent->client->ps.stats[JUMP_STAT_ANNOUNCE] =
		jump_level.announce_expire ? (int16_t) CONFIG_JUMP_ANNOUNCE : 0;
}

// ---------------------------------------------------------------------------
// The announcement banner
// ---------------------------------------------------------------------------
//
// A line across the top of the HUD for the two events everyone should see: a
// new personal best and a new map record. It is a statusbar element rather
// than a print because the statusbar is redrawn every frame and cannot scroll
// away, whereas PRINT_HIGH lands in the notify area and the next line of chat
// takes it with it.
//
// The cost is one configstring write per announcement, which the server
// broadcasts to every connected client. That is affordable only because these
// events are rare - the same trade as CONFIG_JUMP_PB_STRING, and the same
// reason neither may be used for anything that changes per frame.

void Jump_Announce(const char *text, gtime_t duration)
{
	if (!Jump_Active() || !text || !*text)
		return;

	// A configstring is CS_MAX_STRING_LENGTH bytes; the callers build from a
	// name they do not control, so clamp rather than trust the format.
	char buf[CS_MAX_STRING_LENGTH];
	Q_strlcpy(buf, text, sizeof(buf));

	gi.configstring(CONFIG_JUMP_ANNOUNCE, buf);
	jump_level.announce_expire = level.time + duration;
}

// Clearing only zeroes the deadline: the stat gate is what hides the row, so
// there is no need to write the configstring again and re-broadcast it.
void Jump_AnnounceFrame()
{
	if (!jump_level.announce_expire || level.time < jump_level.announce_expire)
		return;

	jump_level.announce_expire = 0_ms;
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

// The timelimit the current CS_STATUSBAR was built against. The time_limit
// token carries an absolute server frame, not a live value, so the bar has to
// be rebuilt - not merely re-sent - whenever the deadline moves. -1 means
// "nothing built yet".
static float jump_statusbar_timelimit = -1.0f;

static void Jump_EmitStatusbar()
{
	statusbar_t sb;

	// Right-edge column: a short label above each number block. Offsets are
	// virtual px from the right edge (negative). Text is left-aligned from the
	// cursor, so each label's xr leaves room for its own width.
	//
	//            Time
	//         1234.56
	//      Checkpoint
	//             0/1
	//          Stores
	//               2
	//
	//               [ 298 ]     (speedometer, centred)
	//
	//                              PB   (bottom right, above the team label)
	//                            9.87
	// Time Left: 04:23             PRACTICE
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

	sb.yt(t_label_y).xr(right - 32).string2("Time");
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

	// Stores, the next row down in the same column. It belongs with the
	// checkpoint counter rather than off in a corner: both are things you have
	// collected during this run, and both are counts you glance at rather than
	// read continuously. Same anchoring rule as the labels above - the label's
	// xr leaves room for its own width so it ends flush at `right`.
	constexpr int st_label_y = cp_y + 24 + row_gap;
	constexpr int st_y = st_label_y + label_h + 2;
	constexpr int st_num = right - Jump_NumWidth(2);

	sb.ifstat(JUMP_STAT_STORES)
		.yt(st_label_y)
		.xr(right - 6 * 8)
		.string2("Stores")
		.yt(st_y)
		.xr(st_num)
		.num(2, JUMP_STAT_STORES)
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

	// The announcement banner, centred across the top. Nothing else is drawn
	// there - the run column is anchored to the right edge - and it is well
	// clear of the centre of the screen, so a centerprint underneath it still
	// reads.
	//
	// loc_stat_cstring2 is the only centred stat_string variant statusbar_t
	// offers; the 2 is the bright alt colour, which suits a banner. Note the
	// loc_ prefix means the client runs the text through Localize, so callers
	// must not start the string with a player name - a name beginning with '$'
	// would be taken for a localization key.
	constexpr int announce_y = 24;

	sb.ifstat(JUMP_STAT_ANNOUNCE).yt(announce_y).xv(0).loc_stat_cstring2(JUMP_STAT_ANNOUNCE).endifstat();

	// Team mode in the bottom-right corner, under PB. PRACTICE (8 chars, xr -76)
	// and RANKED (6 chars, xr -60) both end at the same right edge, xr(-12),
	// since string2 draws at CONCHAR_WIDTH (8) per character.
	constexpr int team_yb = -16;

	sb.ifstat(JUMP_STAT_TEAM_PRACTICE).yb(team_yb).xr(-76).string2("PRACTICE").endifstat();
	sb.ifstat(JUMP_STAT_TEAM_RANKED).yb(team_yb).xr(-60).string2("RANKED").endifstat();

	// Speedometer: centred, one block up from the bottom edge, where a
	// first-person player is already looking. Both upstream mods put it at
	// xv 200 / yb -32, hard against the bottom right - but that is a corner you
	// have to look away from the map to read, and speed is the one number you
	// want while you are mid-jump rather than after it. q2re-map-trainer draws
	// its speedometer centred at 80 above the bottom for that reason; these are
	// its anchors.
	//
	// The column is empty here. Our own bottom-edge elements are all pinned
	// left or right (stores xl 8, team xr, PB and time remaining xr), and the
	// pickup / FOLLOWING / SPECTATOR lines are at xv 0-26, well left of centre.
	//
	// `num` right-aligns inside a fixed box and never pads, so a centred BOX
	// does not give a centred NUMBER. speed_digits_x centres the three-digit
	// case instead, which is what a run actually reads; a four-digit boost
	// grows one cell leftwards from there, which is what right-alignment does
	// anyway. The label sits directly under the digits, as it did before.
	//
	// Gated on the value, so 0 hides the whole element while you are standing
	// still. That gate is also the only way this could ever be varied per
	// player: a CS_STATUSBAR write broadcasts to everyone, so the layout itself
	// is the same for all of them.
	// No caption: a four-digit number that climbs when you move is not something
	// anyone needs told, and a permanent word under it is one more thing on a
	// screen you are trying to read a map through.
	constexpr int speed_digits_yb = JUMP_SPEED_DIGITS_YB; // shared with the cgame overlay
	constexpr int speed_digits_x = 160 - (Jump_NumWidth(4) - 3 * 16) - (3 * 16) / 2; // centres 3 digits

	sb.ifstat(JUMP_STAT_SPEED).yb(speed_digits_yb).xv(speed_digits_x).num(4, JUMP_STAT_SPEED).endifstat();

	// Map time remaining, bottom left - the corner nothing else uses now, and
	// the right place for the one clock here that is not about this run. This
	// is the stock time_limit token: it takes an absolute server frame and the
	// client does the counting itself, so it costs no stat slot and no
	// per-frame server work - the reason it beats a pair of `num` fields. The
	// cost is that the frame number is a snapshot, which is what
	// Jump_StatusbarFrame below exists to refresh.
	//
	// statusbar_t has no method for it, so emit the raw token rather than
	// growing the upstream wrapper. Same expression as the scoreboard
	// (jump_scoreboard.cpp) so the two can never disagree.
	//
	// The awkward part: the token draws its own label ("$g_score_time" ->
	// "Time Left: MM:SS") ending AT the cursor, and measures the width itself.
	// A right-edge anchor is therefore exact, but a left-edge one has to put
	// the cursor where the text should END, which means estimating a width the
	// server cannot know - it depends on the client's font and language. The
	// estimate below is the English string at CONCHAR_WIDTH, which is what the
	// stock font gives; a narrower font just leaves a wider left margin, and
	// only a much longer translation would push the text off the left edge.
	if (timelimit && timelimit->value > 0)
	{
		const int32_t end_frame =
			(int32_t) (gi.ServerFrame() +
					   ((gtime_t::from_min(timelimit->value) - level.time)).milliseconds() / gi.frame_time_ms);

		constexpr int time_left_chars = 16; // "Time Left: 04:23"

		sb.yb(team_yb).xl(8 + time_left_chars * 8);
		sb.sb << "time_limit " << end_frame << ' ';
	}

	// Spectator chase identity — same anchors as stock DM / MuffMode, above
	// the pickup line. STAT_CHASE points at CONFIG_CTF_PLAYER_NAME (see
	// G_SetSpectatorStats); xv(80) clears the longer "FOLLOWING" label.
	sb.ifstat(STAT_CHASE).xv(0).yb(-68).string("FOLLOWING").xv(80).stat_string(STAT_CHASE).endifstat();
	sb.ifstat(STAT_SPECTATOR).xv(0).yb(-58).string2("SPECTATOR").endifstat();

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

	jump_statusbar_timelimit = timelimit ? timelimit->value : 0.0f;
}

bool Jump_InitStatusbar()
{
	if (!Jump_Active())
		return false;

	Jump_EmitStatusbar();

	return true;
}

// The countdown's end frame is baked into the layout, so a vote extension
// (Jump_PassVote) or a console `timelimit` change leaves it stale. Comparing
// the cvar is enough: end_frame is invariant while the limit is, since
// gi.ServerFrame() and level.time advance in lockstep. Change-gated because a
// CS_STATUSBAR write broadcasts the whole bar to every connected client.
void Jump_StatusbarFrame()
{
	if (!Jump_Active())
		return;

	const float current = timelimit ? timelimit->value : 0.0f;

	if (current == jump_statusbar_timelimit)
		return;

	Jump_EmitStatusbar();
}
