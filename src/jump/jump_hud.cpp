// [Jump] Server-side HUD: per-frame stats and the jump statusbar.
//
// Phase 1 keeps everything on the server: the statusbar script can only draw
// whole numbers, so the timer reads as seconds and hundredths in two fields.
// Phase 2 replaces this with a proper cgame-side overlay.

#include "../g_local.h"
#include "jump_local.h"
#include "../g_statusbar.h"

int Jump_CheckpointTotal();

void Jump_SetStats(edict_t *ent)
{
	if (!Jump_Active())
		return;

	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return;

	const int64_t ms = Jump_RunTimeMs(*jc);

	ent->client->ps.stats[JUMP_STAT_ENABLED] = 1;

	ent->client->ps.stats[JUMP_STAT_TIME_SEC] = (int16_t) min<int64_t>(ms / 1000, 32767);
	ent->client->ps.stats[JUMP_STAT_TIME_MS] = (int16_t) ((ms % 1000) / 10);
	ent->client->ps.stats[JUMP_STAT_RUN_STATE] = (int16_t) jc->state;
	ent->client->ps.stats[JUMP_STAT_STORES] = (int16_t) jc->stores.count;
	ent->client->ps.stats[JUMP_STAT_TEAM] = (int16_t) jc->team;
	ent->client->ps.stats[JUMP_STAT_PB_SEC] = (int16_t) min<int64_t>(jc->pb_time_ms / 1000, 32767);
	ent->client->ps.stats[JUMP_STAT_PB_MS] = (int16_t) ((jc->pb_time_ms % 1000) / 10);
	ent->client->ps.stats[JUMP_STAT_CHECKPOINTS] = (int16_t) jc->checkpoints;
	ent->client->ps.stats[JUMP_STAT_CHECKPOINT_TOTAL] = (int16_t) Jump_CheckpointTotal();
	ent->client->ps.stats[JUMP_STAT_TEAM_EASY] = jc->team == jump_team_t::easy;
	ent->client->ps.stats[JUMP_STAT_TEAM_HARD] = jc->team == jump_team_t::hard;
}

bool Jump_InitStatusbar()
{
	if (!Jump_Active())
		return false;

	statusbar_t sb;

	// Run timer, top right: seconds then hundredths.
	sb.yt(8).xr(-96).num(4, JUMP_STAT_TIME_SEC);
	sb.yt(8).xr(-32).num(2, JUMP_STAT_TIME_MS);

	// Checkpoints, under the timer, only while the map uses them.
	sb.ifstat(JUMP_STAT_CHECKPOINT_TOTAL)
		.yt(48)
		.xr(-96)
		.string2("cp")
		.yt(44)
		.xr(-64)
		.num(2, JUMP_STAT_CHECKPOINTS)
		.xr(-16)
		.num(2, JUMP_STAT_CHECKPOINT_TOTAL)
		.endifstat();

	// Team. Written as literal text in two branches because a layout script
	// can test a stat but can't select a string from a value.
	sb.ifstat(JUMP_STAT_TEAM_EASY).yt(72).xr(-56).string2("EASY").endifstat();
	sb.ifstat(JUMP_STAT_TEAM_HARD).yt(72).xr(-56).string2("HARD").endifstat();

	// Personal best on this map, hidden until the player has one.
	sb.ifstat(JUMP_STAT_PB_SEC)
		.yt(88)
		.xr(-96)
		.string2("PB")
		.yt(84)
		.xr(-48)
		.num(3, JUMP_STAT_PB_SEC)
		.xr(-8)
		.num(2, JUMP_STAT_PB_MS)
		.endifstat();

	// Stores held, bottom left.
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

	// Scoreboard toggle (STAT_LAYOUTS) is handled engine-side.
	gi.configstring(CS_STATUSBAR, sb.sb.str().c_str());

	return true;
}
