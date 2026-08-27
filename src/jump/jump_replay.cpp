// [Jump] Replay recording, solo playback, and the live raceline ghost.
//
// Recording is sampled from Jump_ClientThink, the same pre-pmove call site
// the strafe meter and takeoff tracker already use, bucketed to a fixed
// 40 Hz by elapsed run time regardless of the server's actual tick rate. A
// run is saved only when it beats the player's own personal best.
//
// Playback ("replay") freezes the player's own body and drives it through
// the saved frames - an adaptation of Jump_UpdateEyecam's pattern
// (jump_chase.cpp), except the source is an interpolated replay frame
// instead of a live edict.
//
// The raceline ("race") is the classic mods' "race spark" - a short BFG-
// laser-style trail following the player's own ghost while they play live -
// rebuilt as persistent RF_BEAM entities (the target_laser pattern,
// g_target.cpp) instead of re-broadcasting a temp-entity every server frame.
//
// The raceline is on by default and arms itself (Jump_AutoArmRace) from the two
// moments where a ghost becomes available: joining Ranked, and finishing a new
// personal best. `race off` is the opt-out, and it is session state rather than
// per-map state - see race_auto in jump_local.h and the warning above
// Jump_InitLevel's reset loop.

#include "../g_local.h"
#include "jump_local.h"

#include <cmath>
#include <filesystem>
#include <system_error>

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

// Lazily loads this player's own PB replay for the current map into
// jc.loaded_replay, reused until Jump_SaveReplay invalidates it. Returns
// false when there is nothing saved (or the file is unreadable/corrupt).
static bool Jump_LoadReplayIfNeeded(edict_t *ent, jump_client_t &jc)
{
	if (jc.loaded_replay_valid)
		return true;

	std::string bytes;

	if (!Jump_ReadFile(Jump_ReplayPath(jump_level.mapname, Jump_PlayerId(ent)), bytes))
		return false;

	if (!jump::DecodeReplay(bytes, jc.loaded_replay))
		return false;

	jc.loaded_replay_valid = true;
	return true;
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

void Jump_TrackReplay(edict_t *ent, usercmd_t *ucmd, jump_client_t &jc)
{
	// Practice runs are never saved (matching Jump_Finish's own gate), and
	// there is nothing to record outside an active run.
	if (jc.team != jump_team_t::ranked || jc.state != jump_run_state_t::running)
		return;

	jump::replay_frame_t frame;

	frame.origin_ticks[0] = (int32_t) std::lround(ent->s.origin[0] * 8.f);
	frame.origin_ticks[1] = (int32_t) std::lround(ent->s.origin[1] * 8.f);
	frame.origin_ticks[2] = (int32_t) std::lround(ent->s.origin[2] * 8.f);

	frame.velocity[0] = (int32_t) std::lround(ent->velocity[0]);
	frame.velocity[1] = (int32_t) std::lround(ent->velocity[1]);
	frame.velocity[2] = (int32_t) std::lround(ent->velocity[2]);

	// The same expression Jump_TrackStrafe builds pmove's move axes from -
	// the command's raw angles plus the accumulated delta. Converting to the
	// standard 16-bit tick (65536 per circle) wraps correctly on its own:
	// converting a negative or over-range int32 to uint16_t is a modulo
	// reduction, not undefined behaviour.
	const vec3_t angles = ucmd->angles + ent->client->ps.pmove.delta_angles;

	frame.yaw_ticks = (uint16_t) (int32_t) std::lround(angles[YAW] * (65536.f / 360.f));
	frame.pitch_ticks = (uint16_t) (int32_t) std::lround(angles[PITCH] * (65536.f / 360.f));

	frame.buttons =
		(uint8_t) (ucmd->buttons & (BUTTON_ATTACK | BUTTON_USE | BUTTON_HOLSTER | BUTTON_JUMP | BUTTON_CROUCH));

	jc.replay_rec.Sample(Jump_RunTimeMs(jc), frame);
}

void Jump_SaveReplay(edict_t *ent, const jump::replay_recorder_t &rec)
{
	// An overflowed buffer stopped recording partway through and would
	// misrepresent the run; an empty one has nothing to save (a run shorter
	// than one 40 Hz frame does not exist in practice, but costs nothing to
	// guard). Either way Jump_Finish has already banked this as the new PB,
	// so leaving an OLDER, slower replay file sitting under that PB would
	// mean `replay`/`race` shows a run that no longer matches the time the
	// HUD reports - delete it instead, so the next load correctly reports
	// nothing saved rather than something wrong.
	const std::filesystem::path path = Jump_ReplayPath(jump_level.mapname, Jump_PlayerId(ent));

	if (rec.overflowed || rec.frames.empty())
	{
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}
	else
	{
		jump::replay_t replay;
		replay.sample_hz = jump::REPLAY_HZ;
		replay.frames = rec.frames;

		Jump_WriteFileAtomic(path, jump::EncodeReplay(replay));
	}

	// Invalidate the cache rather than replace it in place, so a race ghost
	// already loaded this session picks up the run that was just saved (or
	// the fact that nothing was) on its next load rather than continuing to
	// race a stale one.
	jump_client_t *jc = Jump_ClientData(ent);

	if (jc)
		jc->loaded_replay_valid = false;
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

static void Jump_ApplyReplaySample(edict_t *ent, const jump::replay_sample_t &sample, bool zero_velocity)
{
	gclient_t *client = ent->client;

	client->ps.pmove.origin = { sample.origin[0], sample.origin[1], sample.origin[2] };
	client->ps.pmove.velocity =
		zero_velocity ? vec3_t {} : vec3_t { sample.velocity[0], sample.velocity[1], sample.velocity[2] };

	client->ps.viewangles = { sample.pitch, sample.yaw, 0.f };
	client->v_angle = client->ps.viewangles;
	AngleVectors(client->v_angle, client->v_forward, nullptr, nullptr);

	// Pmove clears viewheight to 0 at the top of every call and PM_FREEZE
	// returns before PM_SetDimensions ever restores it (p_move.cpp), so
	// without this a replaying player's own ClientThink re-zeroes it every
	// frame - the camera would render from ground level for the whole
	// replay instead of the normal eye height. Matches PM_SetDimensions'
	// own values; the recorded buttons byte is the closest thing to a
	// ducked flag this format carries.
	client->ps.pmove.viewheight = (sample.buttons & BUTTON_CROUCH) ? -2 : 22;
	ent->viewheight = client->ps.pmove.viewheight;

	ent->s.origin = client->ps.pmove.origin;
	ent->s.old_origin = ent->s.origin;

	// ClientEndServerFrame unconditionally re-copies ps.pmove.velocity from
	// ent->velocity every frame (p_view.cpp, "update the pmove values" for a
	// pushed/kicked player) - without this, that stomps the value just set
	// above back to whatever ent->velocity last held, and the recorded
	// velocity the speedometer is supposed to show during playback silently
	// reads zero.
	ent->velocity = client->ps.pmove.velocity;

	// Outside the normal post-Pmove copy-back, same reason Jump_UpdateEyecam
	// links by hand.
	gi.linkentity(ent);
}

// Ends playback and puts the player back where they were standing when
// `replay` started - unlike Jump_CancelReplay (below), which only clears the
// state and leaves positioning to whatever the caller is about to do next
// (a respawn, a team switch). This is for the two exits where nothing else
// is about to reposition the player: reaching the end of the recording, and
// an explicit `replay stop`. Without the restore, the player is left
// wherever the ghost's last driven position put them - an unlimited free
// teleport to anywhere on their own PB path, and on a map with no
// checkpoints, a way to bank a near-instant fake time right after the ghost
// reaches the finish.
static void Jump_ReturnFromPlayback(edict_t *ent, jump_client_t &jc);

void Jump_CmdReplay(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!Jump_Active() || !jc)
		return;

	if (ent->client->resp.spectator || ent->client->chase_target || jc->team == jump_team_t::spectator)
	{
		gi.Client_Print(ent, PRINT_HIGH, "You cannot replay while spectating.\n");
		return;
	}

	if (ent->deadflag)
	{
		gi.Client_Print(ent, PRINT_HIGH, "You cannot replay while dead.\n");
		return;
	}

	// Re-entry has to be refused, not just restarted: replay_return_origin
	// is about to be re-captured from ent->s.origin below, which by this
	// point Jump_ApplyReplaySample has been overwriting with the GHOST's
	// position every frame - typing `replay` again mid-playback would quietly
	// replace the real pre-replay position with wherever the ghost currently
	// is, turning `replay stop`/natural end into a free teleport to any point
	// on your own PB path.
	if (jc->replay_mode != jump_replay_mode_t::none)
	{
		gi.Client_Print(ent, PRINT_HIGH, "Already replaying. Use \"replay stop\" first.\n");
		return;
	}

	if (jc->state == jump_run_state_t::running)
	{
		gi.Client_Print(ent, PRINT_HIGH, "Finish or restart your run before replaying.\n");
		return;
	}

	if (!Jump_LoadReplayIfNeeded(ent, *jc))
	{
		gi.Client_Print(ent, PRINT_HIGH, "No replay saved for you on this map yet.\n");
		return;
	}

	jump::replay_sample_t first;

	if (!jump::ReplaySampleAt(jc->loaded_replay, 0, first))
	{
		gi.Client_Print(ent, PRINT_HIGH, "That replay is empty.\n");
		return;
	}

	// Captured before the first sample overwrites it - this is what
	// Jump_ReturnFromPlayback restores on the way back out.
	jc->replay_return_origin = ent->s.origin;
	jc->replay_return_angles = ent->client->v_angle;

	// The grapple is Practice-only and firing it doesn't start a run, so a
	// player can be mid-pull when they type `replay` - CTF_GRAPPLE_STATE_PULL
	// otherwise outranks the replay branch in ClientThink's pm_type chain
	// (p_client.cpp), leaving Pmove still dragging the body toward the hook
	// every frame in between Jump_ReplayFrame's ticks. Same precedent
	// Jump_MovePlayer already sets for every other teleport in this mod.
	CTFPlayerResetGrapple(ent);

	Jump_ApplyReplaySample(ent, first, /* zero_velocity */ true);

	gclient_t *client = ent->client;

	client->ps.pmove.pm_type = PM_FREEZE;
	client->ps.pmove.pm_time = 0;
	client->ps.pmove.pm_flags &= ~PMF_TIME_TELEPORT;

	jc->replay_mode = jump_replay_mode_t::playback;
	jc->replay_start_ms = Jump_NowMs();

	gi.Client_Print(ent, PRINT_HIGH, "Replaying your personal best. Type \"replay stop\" to end early.\n");
}

void Jump_CmdReplayStop(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc || jc->replay_mode == jump_replay_mode_t::none)
	{
		gi.Client_Print(ent, PRINT_HIGH, "You are not replaying.\n");
		return;
	}

	Jump_ReturnFromPlayback(ent, *jc);
	gi.Client_Print(ent, PRINT_HIGH, "Replay stopped.\n");
}

void Jump_CancelReplay(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc || jc->replay_mode == jump_replay_mode_t::none)
		return;

	jc->replay_mode = jump_replay_mode_t::none;
	jc->replay_start_ms = 0;

	// A frozen replayer sends no meaningful input by design, so
	// Jump_TrackInput never refreshes this while playback runs - without
	// stamping it here, the idle-kick sweep's exemption (jump_vote.cpp) just
	// preserves whatever idle time had already accrued before `replay`, and
	// a player who was close to the timeout can get swept to spectator on
	// the very first Jump_IdleFrame after a long replay ends.
	jc->last_input_time = level.time;

	// Belt and braces: the p_client.cpp pm_type hook already stops forcing
	// PM_FREEZE the instant replay_mode reads none, but this restores normal
	// movement immediately rather than one ClientThink call later.
	//
	// Deliberately does NOT restore replay_return_origin/angles - every
	// caller of this function (a respawn, a team switch) is about to
	// reposition the player itself, and doing it here too would just be a
	// wasted teleport at best and a fight over the final position at worst.
	// See Jump_ReturnFromPlayback for the exits that do need the restore.
	if (ent->client)
		ent->client->ps.pmove.pm_type = PM_NORMAL;
}

static void Jump_ReturnFromPlayback(edict_t *ent, jump_client_t &jc)
{
	const vec3_t origin = jc.replay_return_origin;
	const vec3_t angles = jc.replay_return_angles;

	Jump_CancelReplay(ent);

	if (ent->client)
	{
		// z_offset 0: this restores the EXACT position the player was
		// standing at before `replay` started, not a "step off a marker"
		// landing. Jump_MovePlayer's default +10 nudge exists for
		// store/recall, which is harmless there (Ranked refuses store and
		// turns recall into a restart) - but replay is reachable on Ranked,
		// so leaving the default in would let `replay`/`replay stop` mashed
		// in a bind bank a free, repeatable altitude climb every cycle.
		Jump_MovePlayer(ent, origin, angles, 0.f);

		// Jump_MovePlayer doesn't touch this, and Pmove clears it to 0 every
		// call while PM_FREEZE was still in effect for this frame - without
		// restoring it here the exit frame renders from ground level for one
		// frame before the next real ClientThink fixes it. PMF_DUCKED itself
		// is untouched by the whole replay (Pmove returns before
		// PM_CheckDuck ever runs under PM_FREEZE), so it still reflects
		// whatever the player actually was when `replay` started - read it
		// rather than assuming standing, or a player who was crouched gets a
		// wrong one-frame height pop on exit.
		const bool ducked = (ent->client->ps.pmove.pm_flags & PMF_DUCKED) != 0;

		ent->client->ps.pmove.viewheight = ducked ? -2 : 22;
		ent->viewheight = ent->client->ps.pmove.viewheight;
	}
}

bool Jump_ReplayModeActive(edict_t *ent)
{
	const jump_client_t *jc = Jump_ClientData(ent);
	return jc && jc->replay_mode == jump_replay_mode_t::playback;
}

// Advances one client already in playback. Called from Jump_ReplayFrame,
// once per server frame for every client, rather than from the replaying
// player's own ClientThink - which decouples playback smoothness from their
// packet rate, exactly the property that makes 40 Hz playback smooth at
// whatever the real server tick is.
static void Jump_AdvancePlayback(edict_t *ent, jump_client_t &jc)
{
	// Defensive: the stock spectator_respawn path (userinfo-driven, e.g. a
	// "spectator 1" set by the client) calls PutClientInServer through its
	// spectator branch, which returns before Jump_ClientSpawn ever runs - so
	// Jump_ClearRunState's Jump_CancelReplay never fires there, unlike every
	// route this mod controls itself (team command, menu, respawn). Without
	// this check a player who reaches that path mid-replay would keep having
	// their free-fly spectator camera yanked onto the ghost path every frame.
	// Jump_CancelReplay only, not Jump_ReturnFromPlayback - the engine has
	// already repositioned them as a spectator, and teleporting back to
	// replay_return_origin here would just fight that.
	if (ent->client && (ent->client->resp.spectator || ent->client->chase_target))
	{
		Jump_CancelReplay(ent);
		return;
	}

	if (!jc.loaded_replay_valid)
	{
		Jump_ReturnFromPlayback(ent, jc);
		return;
	}

	const int64_t elapsed = Jump_NowMs() - jc.replay_start_ms;

	jump::replay_sample_t sample;

	if (!jump::ReplaySampleAt(jc.loaded_replay, elapsed, sample))
	{
		// Natural end: hand control back rather than requiring "replay stop".
		Jump_ReturnFromPlayback(ent, jc);
		gi.LocCenter_Print(ent, "Replay finished.");
		return;
	}

	Jump_ApplyReplaySample(ent, sample, /* zero_velocity */ false);
}

// ---------------------------------------------------------------------------
// Raceline
// ---------------------------------------------------------------------------

// Consecutive segments trail the ghost's position at staggered offsets
// resampled from the already-loaded replay - no history buffer of its own,
// since the replay itself is the history. 12 segments * 30ms is the same
// ~360ms total window as the original 6 * 60ms, just twice the resolution.
constexpr int64_t JUMP_RACE_TRAIL_STEP_MS = 30;

static void Jump_HideRaceBeam(jump_client_t &jc)
{
	for (edict_t *seg : jc.race_beam)
	{
		if (seg && !(seg->svflags & SVF_NOCLIENT))
		{
			seg->svflags |= SVF_NOCLIENT;
			gi.linkentity(seg);
		}
	}
}

static void Jump_EnsureRaceBeamSpawned(edict_t *ent, jump_client_t &jc)
{
	for (edict_t *&seg : jc.race_beam)
	{
		if (seg)
			continue;

		seg = G_Spawn();

		seg->classname = "jump_race_beam";
		seg->owner = ent;
		seg->movetype = MOVETYPE_NONE;
		seg->solid = SOLID_NOT;
		seg->clipmask = CONTENTS_NONE;
		// SVF_INSTANCED so Jump_EntityVisibility (jump_chase.cpp) can show it
		// only to its owner; SVF_NOCLIENT starts it hidden until the first
		// eligible Jump_ReplayFrame tick positions it.
		seg->svflags |= SVF_INSTANCED | SVF_NOCLIENT;
		// Plain RF_BEAM, not RF_BEAM_LIGHTNING - target_laser's actual default
		// (g_target.cpp) and what the classic mods' race spark itself draws
		// (TE_BFG_LASER renders as RF_BEAM, not lightning). An earlier build
		// of this file used RF_BEAM_LIGHTNING by mistake (copied from the BFG
		// laser bolt's recipe despite the header comment's claim to follow
		// target_laser), which likely rendered as a jittering animated bolt
		// instead of a straight line - reported in testing as the trail not
		// tracking the player's position and appearing to cut through walls
		// beyond what a plain straight chord over a corner would explain.
		seg->s.renderfx |= RF_BEAM;
		seg->s.modelindex = MODELINDEX_WORLD; // must be non-zero
		seg->s.frame = 4;					   // beam width, target_laser's default scale
		seg->s.skinnum = 0xd0d1d2d3;		   // green, same family as SPAWNFLAG_LASER_GREEN

		gi.linkentity(seg);
	}
}

void Jump_FreeRaceTrail(jump_client_t &jc)
{
	jc.race_armed = false;

	for (edict_t *&seg : jc.race_beam)
	{
		if (seg)
		{
			G_FreeEdict(seg);
			seg = nullptr;
		}
	}
}

// Repositions every segment from the loaded replay at `elapsed_ms` minus a
// staggered offset, so the trail always reflects where the ghost currently
// is (and recently was) rather than where a live run happens to be.
static void Jump_PositionRaceBeam(jump_client_t &jc, const jump::replay_t &replay, int64_t elapsed_ms)
{
	for (int i = 0; i < JUMP_RACE_TRAIL_SEGMENTS; i++)
	{
		edict_t *seg = jc.race_beam[i];

		if (!seg)
			continue;

		const int64_t t0 = elapsed_ms - (int64_t) i * JUMP_RACE_TRAIL_STEP_MS;
		const int64_t t1 = elapsed_ms - (int64_t) (i + 1) * JUMP_RACE_TRAIL_STEP_MS;

		jump::replay_sample_t s0, s1;

		const bool ok = t0 >= 0 && t1 >= 0 && jump::ReplaySampleAt(replay, t0, s0) &&
						 jump::ReplaySampleAt(replay, t1, s1);

		if (!ok)
		{
			if (!(seg->svflags & SVF_NOCLIENT))
			{
				seg->svflags |= SVF_NOCLIENT;
				gi.linkentity(seg);
			}
			continue;
		}

		seg->svflags &= ~SVF_NOCLIENT;
		seg->s.origin = { s0.origin[0], s0.origin[1], s0.origin[2] };
		seg->s.old_origin = { s1.origin[0], s1.origin[1], s1.origin[2] };
		gi.linkentity(seg);
	}
}

// Shared by `race` and the auto-arm below, because the two reach it in
// different run states and the wrong half of it reads as a lie. The auto-arm
// usually fires with no run in progress - from Jump_ClientSpawn (idle) and from
// Jump_Finish (finished) - where an unconditional "Racing your ghost" would
// announce a ghost that will not appear until the player crosses the start
// line. It can still land mid-run, from `race` typed during one or from the
// Options row, which is what the running branch is for.
static const char *Jump_RaceArmedMessage(const jump_client_t &jc)
{
	return jc.state == jump_run_state_t::running
			   ? "Racing your ghost - type \"race off\" to hide it.\n"
			   : "Ghost armed, starts with your next run - type \"race off\" to hide it.\n";
}

// The ghost is on by default, so most players never type `race` at all - this
// is what arms it for them, from the two moments where the thing to race
// against has just become available: joining Ranked on a map they already have
// a saved run on, and finishing a new personal best.
//
// Checking there is something to race first, and arming only if there is, is
// what keeps it quiet for everyone else. Arming blind would hand a player with
// no saved run to Jump_ReplayFrame's reload path, which announces the ghost
// being hidden - reasonable for a run whose replay was dropped, and noise for
// somebody who has simply never finished the map.
void Jump_AutoArmRace(edict_t *ent, jump_client_t &jc)
{
	// race_armed is the re-entry guard as much as a state check. Jump_ClientSpawn
	// runs on every respawn rather than just the join, and Jump_ClearRunState
	// leaves an armed ghost armed across a death by design - so without this the
	// message would repeat every time the player died for the rest of the map.
	// It does repeat on a team switch, which clears race_armed by way of
	// Jump_FreeRaceTrail; that is wanted, since coming back to Ranked is exactly
	// when you want telling whether the ghost came back with you.
	if (!Jump_Active() || !jc.race_auto || jc.team != jump_team_t::ranked || jc.race_armed)
		return;

	// No banked time means no saved run, because the two are written together
	// (Jump_Finish saves the replay inside the same improved_pb branch that
	// banks the PB). Checking it first is what keeps this free for the player
	// grinding a map they have never finished: `kill` on Ranked is a respawn,
	// so without this every attempt would open and fail to read a file that is
	// not there. Jump_ClientSpawn seeds pb_time_ms immediately before calling
	// in here, so it is current on the join as well as at the finish line.
	if (!jc.pb_time_ms)
		return;

	if (!Jump_LoadReplayIfNeeded(ent, jc))
		return;

	jc.race_armed = true;

	gi.Client_Print(ent, PRINT_HIGH, Jump_RaceArmedMessage(jc));
}

void Jump_CmdRace(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!Jump_Active() || !jc)
		return;

	// Above both gates below, because this is the line where the player says
	// they want the ghost and everything under it is only about whether one can
	// be shown to them *right now*. Recording it any later would strand them:
	// `race off` has no team gate and no file check, so it is reachable from
	// Practice and on a map with nothing saved - an opt-in that only takes
	// effect on the success path would leave someone who opted out on Practice,
	// or on a map they have never finished, still opted out afterwards, with no
	// hint that the `race` they just typed did not stick.
	jc->race_auto = true;

	if (jc->team != jump_team_t::ranked)
	{
		gi.Client_Print(ent, PRINT_HIGH,
						 "Racing your ghost is only available on Ranked - it will arm when you join.\n");
		return;
	}

	if (!Jump_LoadReplayIfNeeded(ent, *jc))
	{
		gi.Client_Print(ent, PRINT_HIGH, "No replay saved for you on this map yet.\n");
		return;
	}

	jc->race_armed = true;

	gi.Client_Print(ent, PRINT_HIGH, Jump_RaceArmedMessage(*jc));
}

void Jump_CmdRaceOff(edict_t *ent)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!Jump_Active() || !jc)
		return;

	// The opt-out itself, not just a dismissal: without clearing this the
	// auto-arm in Jump_ClientSpawn would put the ghost straight back on the next
	// respawn. Session state deliberately - Jump_InitLevel leaves it alone, so
	// it survives a map change and only a reconnect restores the default.
	jc->race_auto = false;

	Jump_FreeRaceTrail(*jc);
	gi.Client_Print(ent, PRINT_HIGH, "Ghost racing off for this session - type \"race\" to bring it back.\n");
}

// ---------------------------------------------------------------------------
// Per-server-frame driver
// ---------------------------------------------------------------------------

void Jump_ReplayFrame()
{
	if (!Jump_Active())
		return;

	for (uint32_t i = 1; i <= game.maxclients; i++)
	{
		edict_t *ent = g_edicts + i;

		if (!ent->inuse || !ent->client || !ent->client->pers.connected)
			continue;

		jump_client_t *jc = Jump_ClientData(ent);

		if (!jc)
			continue;

		// An intermission is a real, ordinary state within an active jump
		// level, and nothing about ending the map should keep dragging a
		// replaying player's camera through their ghost path onto the
		// end-of-unit screen everyone else is looking at - but this has to
		// stay inside the loop rather than a blanket early-return, or a
		// racer's beam trail freezes visible (to them) instead of hiding.
		if (level.intermissiontime)
		{
			Jump_HideRaceBeam(*jc);
			continue;
		}

		if (jc->replay_mode == jump_replay_mode_t::playback)
			Jump_AdvancePlayback(ent, *jc);

		// The spectator/chase test is the same defence Jump_AdvancePlayback
		// carries above: the stock userinfo-driven spectator_respawn path
		// returns from PutClientInServer before the Jump_ClientSpawn hook, so
		// jc->team and jc->state are left reading ranked/running for someone
		// who is now flying around as a spectator. Their own beam segments
		// would keep tracing the ghost path in front of them. That hole is
		// older than the auto-arm, but it used to need the player to have
		// typed `race`; every Ranked player is armed now, so it is worth
		// closing rather than noting.
		const bool wants_race = jc->race_armed && jc->team == jump_team_t::ranked &&
								 jc->state == jump_run_state_t::running &&
								 !ent->client->resp.spectator && !ent->client->chase_target;

		// A new PB invalidates the cache (Jump_SaveReplay) so an armed racer
		// picks up their latest run rather than continuing to race a stale
		// one - without this reload here, nothing else ever re-triggers the
		// load and the ghost just silently vanishes for the rest of the
		// session. No-op (cheap) once loaded_replay_valid is true again; on
		// genuine failure (the file vanished or won't parse) this disarms
		// racing outright rather than retrying the read every server frame
		// for the rest of the run.
		if (wants_race && !jc->loaded_replay_valid && !Jump_LoadReplayIfNeeded(ent, *jc))
		{
			// Says "hidden", not "turned off": this disarms the trail but
			// deliberately leaves race_auto alone, so Options still reads Auto
			// and the ghost comes back by itself on the next personal best.
			// The reachable cause is no longer an exotic one - a run over
			// REPLAY_MAX_FRAMES banks the PB but has its replay deleted rather
			// than saved (Jump_SaveReplay), and with the ghost on by default
			// that lands on players who never asked for racing at all.
			Jump_FreeRaceTrail(*jc);
			gi.Client_Print(ent, PRINT_HIGH, "No replay to race on this map - ghost hidden for now.\n");
		}

		// Spawned here rather than at arm time. Now that the ghost arms itself,
		// "armed" is the resting state of every Ranked player rather than
		// something a handful of people asked for, and there is no reason to
		// hold twelve edicts each for a lobby stood at the spawn point.
		// Idempotent, so calling it every frame of a run costs nothing after
		// the first.
		if (wants_race && jc->loaded_replay_valid)
		{
			Jump_EnsureRaceBeamSpawned(ent, *jc);
			Jump_PositionRaceBeam(*jc, jc->loaded_replay, Jump_RunTimeMs(*jc));
		}
		else
			Jump_HideRaceBeam(*jc);
	}
}
