// [Jump] Client-side movement sampling.
//
// The overlay wants to show how your speed is changing, which needs a history
// of where you were and what you were pressing. Neither is in the server
// snapshot the HUD is handed: that arrives at the server tick rate, lags by
// your ping, and carries no inputs at all. The predicted state does have both,
// and the client computes it every frame - it just never keeps it.
//
// So this file wraps the cgame's Pmove export (cg_main.cpp) and watches. It is
// observation only: Pmove is called unconditionally with an untouched pmove_t,
// nothing is written back, and p_move.cpp is not involved. A stock client's
// prediction and this one stay bit-identical, which is the whole reason a
// stock client can play here.
//
// The awkward part is that prediction is not a stream of new commands. Every
// client frame the engine REPLAYS every unacknowledged command from the last
// server snapshot, then runs the partial pending one. Appending a sample per
// call would count the same movement over and over, and no key on the usercmd
// fixes it - server_frame is shared by several commands at high framerates, and
// an ordinal within it shifts as the acknowledgement boundary moves.
//
// The fix is structural: the wrapper only ever OVERWRITES a scratch struct, so
// after the last call of a pass it holds the predicted "now" state - the same
// value however many times the pass replayed. The ring is appended once per
// rendered frame, from Jump_CG_SampleFrame. Double counting is then impossible
// rather than merely unlikely.

#include "../cg_local.h"
#include "jump_cg_move.h"
#include "jump_stats.h"

#include <cmath>

// The predicted state as of the last Pmove call. Overwritten, never appended.
struct jump_pass_t
{
	bool	 active = false;
	uint64_t client_time = 0;

	float	 velocity[3] = { 0, 0, 0 };
	float	 origin[3] = { 0, 0, 0 };
	float	 view_yaw = 0.f;
	float	 forwardmove = 0.f;
	float	 sidemove = 0.f;
	uint16_t buttons = 0;
	uint8_t	 msec = 0;
	uint16_t pm_flags = 0;
	int		 pm_type = 0;
};

static jump_pass_t jump_pass;

// Read by the wrapper on every Pmove call, so it is a cached bool rather than a
// cvar dereference.
static bool jump_cg_sampling = false;

static jump::move_ring_t	 jump_ring;
static jump::speed_state_t	 jump_speed;
static jump::speed_readout_t jump_readout;

// The previous committed frame's pmove flags, which move_sample_t does not
// carry - only the discontinuity verdict derived from them.
static uint16_t jump_last_pm_flags = 0;
static int		jump_last_pm_type = 0;
static bool		jump_have_last = false;

void Jump_CG_ResetSamples()
{
	// Clearing the flag too, so that a level without jump mode leaves the
	// wrapper doing nothing at all rather than filling a ring nobody reads.
	jump_cg_sampling = false;

	jump_pass = {};
	jump_ring.Clear();
	jump_speed.Reset();
	jump_readout = {};
	jump_last_pm_flags = 0;
	jump_last_pm_type = 0;
	jump_have_last = false;
}

void Jump_CG_Pmove(pmove_t *pm)
{
	const uint64_t now = jump_cg_sampling ? cgi.CL_ClientTime() : 0;

	// Start of a pass. `snapinitial` marks it, but the client-time comparison
	// is checked too: snapinitial's behaviour is verified against the q2repro
	// engine and retail KEX is closed source, so this must not be the only
	// thing standing between us and a scratch that never resets.
	if (jump_cg_sampling && (pm->snapinitial || now != jump_pass.client_time))
	{
		jump_pass = {};
		jump_pass.client_time = now;
	}

	// Unmodified, unconditional, and nothing is written to `pm` either side of
	// it. This line is the contract with a stock client.
	Pmove(pm);

	if (!jump_cg_sampling)
		return;

	jump_pass.active = true;
	jump_pass.client_time = now;

	for (int i = 0; i < 3; i++)
	{
		jump_pass.velocity[i] = pm->s.velocity[i];
		jump_pass.origin[i] = pm->s.origin[i];
	}

	jump_pass.view_yaw = pm->viewangles[YAW];
	jump_pass.forwardmove = pm->cmd.forwardmove;
	jump_pass.sidemove = pm->cmd.sidemove;
	jump_pass.buttons = (uint16_t) pm->cmd.buttons;
	jump_pass.msec = pm->cmd.msec;
	jump_pass.pm_flags = (uint16_t) pm->s.pm_flags;
	jump_pass.pm_type = (int) pm->s.pm_type;
}

// Everything that moves the player without accelerating them. Comparing across
// one of these would report a speed change nobody made, so the sample is
// flagged and the history is cut there.
static bool Jump_CG_Discontinuity(const jump::move_sample_t &sample, uint16_t pm_flags, int pm_type)
{
	if (!jump_have_last)
		return false;

	// Teleport: the freeze flag going up is the edge, not the flag being set.
	if ((pm_flags & PMF_TIME_TELEPORT) && !(jump_last_pm_flags & PMF_TIME_TELEPORT))
		return true;

	// Death, respawn, entering or leaving a chase camera.
	if (pm_type != jump_last_pm_type)
		return true;

	const jump::move_sample_t *prev = jump_ring.Get(1);

	if (!prev)
		return false;

	const uint64_t dt_ms = sample.time_ms > prev->time_ms ? sample.time_ms - prev->time_ms : 0;

	// A gap in time: alt-tab, a load, a paused server. Treat it as a break
	// rather than as motion, or the origin test below reads it as a huge jump.
	if (dt_ms > 500)
		return true;

	const float dx = sample.origin[0] - prev->origin[0];
	const float dy = sample.origin[1] - prev->origin[1];
	const float dz = sample.origin[2] - prev->origin[2];
	const float moved = std::sqrt(dx * dx + dy * dy + dz * dz);

	// The full 3D velocity, not move_sample_t::speed: that one is horizontal,
	// and a long fall would otherwise cover more ground than it allows for and
	// read as a teleport.
	const float was_moving = std::sqrt(prev->velocity[0] * prev->velocity[0] + prev->velocity[1] * prev->velocity[1] +
									   prev->velocity[2] * prev->velocity[2]);

	// Further than physics could have carried them. This is the catch-all that
	// covers `recall`, respawn and trigger_teleport without needing to know
	// about any of them. The 64-unit slack absorbs a step up and the rounding
	// in a snapshot-sourced origin.
	return moved > was_moving * (dt_ms / 1000.f) + 64.f;
}

const jump::speed_readout_t &Jump_CG_SampleFrame(const player_state_t *ps, bool wanted)
{
	jump_cg_sampling = wanted;

	if (!wanted)
	{
		jump_readout = {};
		return jump_readout;
	}

	jump::move_sample_t sample;
	sample.time_ms = cgi.CL_ClientTime();

	// The scratch is only usable if prediction ran for THIS frame. It might not
	// have: cl_predict 0 and PMF_NO_POSITIONAL_PREDICTION skip the path
	// entirely, and a frame where nothing moved runs no Pmove call at all. The
	// snapshot is coarser but never stale, and while chasing it already holds
	// the followed player's velocity.
	const bool fresh = jump_pass.active && jump_pass.client_time == sample.time_ms;

	uint16_t pm_flags;
	int		 pm_type;

	if (fresh)
	{
		for (int i = 0; i < 3; i++)
		{
			sample.velocity[i] = jump_pass.velocity[i];
			sample.origin[i] = jump_pass.origin[i];
		}

		sample.view_yaw = jump_pass.view_yaw;
		sample.forwardmove = jump_pass.forwardmove;
		sample.sidemove = jump_pass.sidemove;
		sample.buttons = jump_pass.buttons;
		sample.msec = jump_pass.msec;
		sample.predicted = true;

		// Demo playback runs pmove once with a zeroed usercmd, which would
		// otherwise read as "no keys pressed" rather than "no keys known".
		sample.inputs_valid = jump_pass.msec != 0;

		pm_flags = jump_pass.pm_flags;
		pm_type = jump_pass.pm_type;
	}
	else
	{
		for (int i = 0; i < 3; i++)
		{
			sample.velocity[i] = ps->pmove.velocity[i];
			sample.origin[i] = ps->pmove.origin[i];
		}

		sample.view_yaw = ps->viewangles[YAW];
		sample.predicted = false;
		sample.inputs_valid = false;

		pm_flags = (uint16_t) ps->pmove.pm_flags;
		pm_type = (int) ps->pmove.pm_type;
	}

	sample.speed = jump::HorizontalSpeed(sample.velocity[0], sample.velocity[1]);
	sample.on_ground = (pm_flags & PMF_ON_GROUND) != 0;

	// Chase cam copies the followed player's velocity but not their pm_flags
	// (jump_chase.cpp), so while frozen the ground state belongs to the viewer
	// and must not be read as the subject's.
	sample.on_ground_valid = pm_type != PM_FREEZE;

	sample.discontinuity = Jump_CG_Discontinuity(sample, pm_flags, pm_type);

	jump_last_pm_flags = pm_flags;
	jump_last_pm_type = pm_type;
	jump_have_last = true;

	jump_ring.Push(sample);
	jump_readout = jump_speed.Update(jump_ring);

	return jump_readout;
}
