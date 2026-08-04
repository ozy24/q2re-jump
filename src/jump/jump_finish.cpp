// [Jump] Finish detection and checkpoints.
//
// Following Q2JumpRefresh: touching a weapon (or a trigger_finish) ends the
// run, key items and cpboxes are checkpoints, and everything else in the map
// is inert.

#include "../g_local.h"
#include "jump_local.h"

// Number of checkpoints in the map, computed lazily on first use so it doesn't
// matter whether entities have finished spawning yet. -1 means "not counted".
static int jump_checkpoint_total_cache = -1;

void Jump_InvalidateCheckpointTotal()
{
	jump_checkpoint_total_cache = -1;
}

bool Jump_IsCheckpointEntity(edict_t *ent)
{
	if (ent->item && (ent->item->flags & IF_KEY))
		return true;

	if (!ent->classname)
		return false;

	return !strncmp(ent->classname, "cpbox_", 6);
}

int Jump_CheckpointTotal()
{
	// An explicit mset always wins: some maps place more checkpoint entities
	// than a run is required to collect.
	if (jump_mset.checkpoint_total_set)
		return jump_mset.checkpoint_total;

	if (jump_checkpoint_total_cache >= 0)
		return jump_checkpoint_total_cache;

	int total = 0;

	for (uint32_t i = 0; i < globals.num_edicts; i++)
	{
		edict_t *e = &g_edicts[i];

		if (!e->inuse)
			continue;

		if (Jump_IsCheckpointEntity(e))
			total++;
	}

	jump_checkpoint_total_cache = total;
	jump_level.checkpoint_total = total;

	return total;
}

bool Jump_TakeCheckpoint(edict_t *ent, edict_t *cp)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return false;

	if (ent->client->resp.spectator)
		return true;

	const int32_t playernum = ent->s.number - 1;

	if (playernum < 0 || playernum >= (int32_t) MAX_CLIENTS)
		return true;

	// Each checkpoint counts once per life; the flag is cleared on respawn.
	if (cp->item_picked_up_by[playernum])
		return true;

	cp->item_picked_up_by[playernum] = true;
	jc->checkpoints++;

	const int total = Jump_CheckpointTotal();

	gi.Client_Print(ent, PRINT_HIGH,
					G_Fmt("You reached checkpoint {}/{} in {} seconds.\n", jc->checkpoints, total,
						  jump::FormatTime(Jump_RunTimeMs(*jc)).c_str())
						.data());

	if (cp->noise_index)
		gi.sound(ent, CHAN_ITEM, cp->noise_index, 1, ATTN_NORM, 0);

	return true;
}

void Jump_ClearCheckpointFlags(edict_t *ent)
{
	const int32_t playernum = ent->s.number - 1;

	if (playernum < 0 || playernum >= (int32_t) MAX_CLIENTS)
		return;

	for (uint32_t i = 0; i < globals.num_edicts; i++)
	{
		edict_t *e = &g_edicts[i];

		if (e->inuse && Jump_IsCheckpointEntity(e))
			e->item_picked_up_by[playernum] = false;
	}
}

// A new personal best or a new map record goes on the HUD banner rather than
// into a print: a print lands in the notify area, where the next line of chat
// scrolls it away, and these are the two results worth everyone seeing.
//
// A record is always a personal best as well, so the two are exclusive - the
// record is the louder of the pair and wins.
//
// rank 1 is the whole test. map_records_t::Submit returns 0 rather than a rank
// when a time does not improve on the player's own entry, so rank 1 can only
// mean the time was banked AND it is now top of the table - there is no need
// to also compare against the previous record, and comparing against it is
// what used to swallow the announcement on a map nobody had finished yet.
static void Jump_AnnounceFinish(edict_t *ent, const std::string &time_str, int rank, bool improved_pb)
{
	const bool new_record = (rank == 1);

	if (!new_record && !improved_pb)
		return;

	// The banner is drawn with a loc_ token, so the client puts the text
	// through Localize - a name is never allowed to lead the line, or one
	// starting with '$' would be read as a localization key. SanitizeLayoutText
	// also caps the length, which keeps the whole line inside a configstring.
	const std::string name = jump::SanitizeLayoutText(Jump_DisplayName(ent));

	if (new_record)
	{
		Jump_Announce(G_Fmt("NEW MAP RECORD - {} - {}", name.c_str(), time_str.c_str()).data(), 6_sec);

		// The one result rare enough to be worth interrupting everyone for.
		// Not the finisher: Jump_Finish has already given them their own
		// centerprint, and there is only one centerprint slot to overwrite.
		for (uint32_t i = 1; i <= game.maxclients; i++)
		{
			edict_t *other = g_edicts + i;

			if (!other->inuse || !other->client || !other->client->pers.connected || other == ent)
				continue;

			gi.LocCenter_Print(other, G_Fmt("NEW MAP RECORD\n{} - {}", name.c_str(), time_str.c_str()).data());
		}
	}
	else
	{
		Jump_Announce(G_Fmt("PERSONAL BEST - {} - {}", name.c_str(), time_str.c_str()).data(), 4_sec);
	}

	// A positioned_sound from the world entity at ATTN_NONE is how the stock
	// CTF code broadcasts a scoring sound: no distance falloff, so everyone
	// hears it at full volume wherever they are. CHAN_RELIABLE because an
	// announcement dropped from the datagram is worse than one that arrives
	// late.
	const int sound = new_record ? jump_level.sound_record : jump_level.sound_pb;

	if (sound)
		gi.positioned_sound(world->s.origin, world, CHAN_AUTO | CHAN_RELIABLE, sound, 1, ATTN_NONE, 0);
}

void Jump_Finish(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc || ent->client->resp.spectator)
		return;

	if (jc->state != jump_run_state_t::running)
		return;

	const int total = Jump_CheckpointTotal();

	if (total > 0 && jc->checkpoints < total)
	{
		// Standing on the finish weapon fires every frame; don't spam.
		if (jc->finish_deny_time <= level.time)
		{
			jc->finish_deny_time = level.time + 3_sec;
			gi.Client_Print(ent, PRINT_HIGH,
							G_Fmt("You need all {} checkpoints to finish ({} taken).\n", total, jc->checkpoints)
								.data());
		}
		return;
	}

	const int64_t time_ms = Jump_RunTimeMs(*jc);

	jc->state = jump_run_state_t::finished;
	jc->last_time_ms = time_ms;

	const std::string time_str = jump::FormatTime(time_ms);

	// Finishing leaves you standing on the pad, as it does upstream; nothing
	// starts a new run until you ask for one.
	gi.LocCenter_Print(ent, G_Fmt("Finished in {}\nkill to run again", time_str.c_str()).data());

	// Practice runs are timed for your own benefit but are never broadcast and
	// never recorded.
	if (jc->team != jump_team_t::ranked)
	{
		gi.Client_Print(ent, PRINT_HIGH,
						G_Fmt("You would have finished in {} seconds (Easy times are not recorded).\n",
							  time_str.c_str())
							.data());
		return;
	}

	// Compare against the stored table before submitting, so the announcement
	// describes the run the player just made rather than the one it became.
	const int64_t previous_pb = Jump_PersonalBest(ent);
	const int64_t previous_record = Jump_MapRecord();

	const int rank = Jump_SubmitTime(ent, time_ms);

	const bool improved_pb = previous_pb == 0 || time_ms < previous_pb;
	jc->pb_time_ms = improved_pb ? time_ms : previous_pb;

	// Best on this map since it loaded, for the players scoreboard. Ranked only,
	// same as the PB above: practice runs can recall from a store, so they are
	// not comparable with anything else on that board.
	if (!jc->session_best_ms || time_ms < jc->session_best_ms)
		jc->session_best_ms = time_ms;

	if (improved_pb)
		Jump_UpdatePbString(ent);

	std::string suffix;

	if (previous_pb == 0 && previous_record == 0)
		suffix = " (1st completion on the map)";
	else if (previous_pb == 0)
		suffix = G_Fmt(" (1st {})", jump::FormatDelta(time_ms - previous_record).c_str()).data();
	else
		suffix = G_Fmt(" (PB {} | 1st {})", jump::FormatDelta(time_ms - previous_pb).c_str(),
					   jump::FormatDelta(time_ms - previous_record).c_str())
					 .data();

	gi.Broadcast_Print(
		PRINT_HIGH,
		G_Fmt("{} finished in {} seconds{}\n", Jump_DisplayName(ent), time_str.c_str(), suffix.c_str()).data());

	// Same test as the announcement below: being first onto an empty table is
	// still holding the map record, and used to say nothing at all.
	if (rank == 1)
		gi.Broadcast_Print(PRINT_CHAT, G_Fmt("{} has set a 1st place!\n", Jump_DisplayName(ent)).data());

	Jump_AnnounceFinish(ent, time_str, rank, improved_pb);

	Jump_Log("%s finished: %lld ms (rank %d)", Jump_DisplayName(ent), (long long) time_ms, rank);
}

// A handful of old maps end the course at a target_changelevel rather than on a
// weapon, so the exit brush is the finish line. Treat reaching it as a finish
// and swallow the level change: a jump server picks its next map by vote or
// rotation, and letting any player force a change from inside a map would break
// that. Finishing here also leaves the player standing where the weapon finish
// would, so "kill to run again" still reads correctly.
//
// This is new behaviour, not a port - neither upstream mod recorded a run on a
// level change; both simply abandoned it.
bool Jump_LevelExit(edict_t *activator)
{
	if (!Jump_Active())
		return false;

	// Timelimit-driven changes and exits fired by anything other than a player
	// leave vanilla alone.
	if (!activator || !activator->client)
		return false;

	// Jump_Finish is a no-op unless a run is in progress, and it owns the
	// checkpoint gate and its own anti-spam, so an idle player walking into the
	// exit gets the same inert behaviour as before.
	Jump_Finish(activator);

	return true;
}

bool Jump_ItemTouch(edict_t *ent, edict_t *other)
{
	if (!Jump_Active())
		return false;

	if (!ent->item || !other->client)
		return false;

	// Keys are checkpoints.
	if (ent->item->flags & IF_KEY)
		return Jump_TakeCheckpoint(other, ent);

	if (ent->item->flags & IF_WEAPON)
	{
		// A map can mark specific weapons as usable (rocket jumping, etc.);
		// those fall through and are picked up normally. Ask for the switch
		// explicitly: G_CheckAutoSwitch's SMART policy only auto-switches in
		// deathmatch when the blaster is out, and jump players hold nothing,
		// so the pickup would otherwise leave them empty-handed.
		if (Jump_IsUsableWeapon(ent->item->id))
		{
			other->client->newweapon = ent->item;
			return false;
		}

		// Otherwise any weapon is the finish line. It is never handed out and
		// never removed, so every player can finish on the same one.
		Jump_Finish(other);
		return true;
	}

	// Ammo, armour, health, powerups: all inert in a jump map.
	return true;
}
