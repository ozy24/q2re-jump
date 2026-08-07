// [Jump] cgame-side HUD overlay.
//
// This is the client half of the mod, and it draws the performance HUD.
//
// The division of labour is deliberate. The server-authored statusbar
// (Jump_InitStatusbar) carries everything needed to PLAY - timer, checkpoints,
// stores, team, personal best, time remaining - because a stock client can be
// shown that and nothing else, and nobody should need a download to race. This
// file carries everything about playing BETTER, and it lives here because most
// of it could not live anywhere else: a strafe meter needs the usercmd, a key
// display needs button state, a per-jump readout needs the ground edge at frame
// resolution. None of that reaches a layout script. The speedometer alone could
// have gone server-side, but splitting one performance readout away from the
// rest would leave the HUD half in one place and half in the other.
//
// So: a stock client gets a complete, playable HUD. Downloading this DLL is
// what adds the tooling, and the tooling is all here.

#include "../cg_local.h"
#include "jump_logic.h"
#include "jump_stats.h"
#include "jump_cg_move.h"
#include "jump_hud_draw.h"

#include <cstdio>

// Centred, one block up from the bottom edge, where a first-person player is
// already looking. Both upstream mods put their speedometer in the bottom-right
// corner, which is a glance away from the map at exactly the moment you want the
// number; q2re-map-trainer centres it, and this follows that.
//
// Layout units up from the bottom, matching the statusbar's yb anchors so this
// sits in the same coordinate space as everything the server draws.
static constexpr float JUMP_SPEED_YB = -96.f;

// How long a reading stays up before it is replaced, in ms. This is the
// rerelease's server frame; without it the number would churn at the render
// rate, which can be several hundred times a second.
static constexpr uint64_t JUMP_SPEED_REFRESH_MS = 25;

// The strafe bar, directly under the number. Same virtual units as the layout,
// and the same 4-high bar the stock health meters use.
static constexpr float JUMP_STRAFE_W = 80.f;
static constexpr float JUMP_STRAFE_H = 4.f;
static constexpr float JUMP_STRAFE_GAP = 3.f;

static cvar_t *jump_hud;
static cvar_t *jump_hud_speed;
static cvar_t *jump_hud_speed_hz;
static cvar_t *jump_hud_strafe;
static cvar_t *jump_hud_strafe_tau;

void Jump_InitClientCvars()
{
	// On by default. Installing this DLL is itself the opt-in: the server's
	// statusbar already carries everything a run needs, so the only reason to
	// have the file is the tooling in here. `jump_hud 0` gives the exact
	// stock-client view, which is worth having for checking what players see.
	jump_hud = cgi.cvar("jump_hud", "1", CVAR_ARCHIVE);

	jump_hud_speed = cgi.cvar("jump_hud_speed", "1", CVAR_ARCHIVE);

	// How often the speed reading is replaced, in Hz. 40 is the rerelease's
	// server frame rate. Both upstream mods ran on a 10 Hz server and so
	// changed theirs ten times a second by construction rather than by choice,
	// and four digits are markedly easier to read at that rate: 10 gives you
	// it. This is a refresh rate, not smoothing - the value shown is always an
	// exact instantaneous speed, only sampled less often.
	jump_hud_speed_hz = cgi.cvar("jump_hud_speed_hz", "40", CVAR_ARCHIVE);

	// The strafe meter: 0 off, 1 a plain 0-100% bar, 2 centre-anchored. 2 is
	// the default because a bar that only shows magnitude tells you that you
	// are losing speed but not which way to correct, and the penalty is
	// lopsided - a shallow strafe still captures most of what is available
	// while a fractionally steep one captures none of it.
	jump_hud_strafe = cgi.cvar("jump_hud_strafe", "2", CVAR_ARCHIVE);

	// How long the reading remembers, in ms. Roughly one airtime. The raw
	// per-frame value is unreadable; this is what makes it a meter rather than
	// a flicker.
	jump_hud_strafe_tau = cgi.cvar("jump_hud_strafe_tau", "300", CVAR_ARCHIVE);

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
	const player_state_t		 *ps;
	vrect_t						  hud_vrect;
	vrect_t						  hud_safe;
	int32_t						  scale;
	const jump::speed_readout_t	 *speed;
	const jump::strafe_readout_t *strafe;

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

// The speedometer.
//
// Two annotations used to live beside it and both were cut after seeing them in
// play. Peak-of-jump was a high-water mark, so it went stale the moment you
// stopped matching it, and since speed barely changes in flight it mostly
// duplicated the number itself. The signed gain/loss figure was honest but
// noisy - a second thing moving next to a number you are trying to read.
//
// What replaces them has to answer a question the live number cannot, rather
// than restating it: speed at takeoff against the previous jump, or how much of
// the available acceleration a strafe actually captured. jump::speed_state_t
// still tracks peak and trend, because both of those want the same ground-edge
// state it already computes.
static void Jump_DrawSpeedometer(const jump_overlay_ctx_t &ctx)
{
	const jump::speed_readout_t &speed = *ctx.speed;

	if (!speed.valid)
		return;

	// A free-flying camera's speed is noise. PM_FREEZE is kept, because that is
	// the eyecam case, where the reading is the followed player's.
	const pmtype_t pm_type = ctx.ps->pmove.pm_type;

	if (pm_type == PM_NOCLIP || pm_type == PM_SPECTATOR)
		return;

	// Hidden when you are standing still, as both upstream mods hide theirs.
	if (speed.current < 1.f)
		return;

	// The reading is held rather than redrawn every frame - see
	// jump_hud_speed_hz. The value is a real instantaneous speed, never an
	// average; only the moment it is taken is rationed.
	const int	   hz = jump_hud_speed_hz ? jump_hud_speed_hz->integer : 0;
	const uint64_t interval = hz > 0 ? 1000 / (uint64_t) hz : JUMP_SPEED_REFRESH_MS;

	static int32_t	shown = 0;
	static uint64_t shown_time = 0;

	const uint64_t now = cgi.CL_ClientTime();

	// The clock runs backwards across a map change, so treat that as due.
	if (now < shown_time || now - shown_time >= interval)
	{
		shown = (int32_t) speed.current;
		shown_time = now;
	}

	// No caption. A four-digit number that climbs as you move is not something
	// anyone needs told, and the word would be one more thing on a screen you
	// are reading a map through.
	cgi.SCR_DrawFontString(jump::FormatSpeed((float) shown).c_str(), ctx.VirtX(160.f), ctx.BottomY(JUMP_SPEED_YB),
						   ctx.scale, rgba_white, true, text_align_t::CENTER);
}

// Green through amber to red as `loss` (0..1) grows. Interpolated rather than
// banded on purpose: a colour that flips at a threshold flickers on the
// boundary, which is the same failure that got the peak and trend figures cut.
static rgba_t Jump_StrafeColour(float loss)
{
	if (loss < 0.f)
		loss = 0.f;
	if (loss > 1.f)
		loss = 1.f;

	const float t = loss < 0.5f ? loss * 2.f : (loss - 0.5f) * 2.f;

	const rgba_t from = loss < 0.5f ? rgba_t { 80, 220, 90, 255 } : rgba_t { 235, 200, 60, 255 };
	const rgba_t to = loss < 0.5f ? rgba_t { 235, 200, 60, 255 } : rgba_t { 235, 70, 60, 255 };

	return { (uint8_t) (from.r + (to.r - from.r) * t), (uint8_t) (from.g + (to.g - from.g) * t),
			 (uint8_t) (from.b + (to.b - from.b) * t), 255 };
}

// How much of the acceleration that was available you actually took.
//
// This is the one readout the speed number cannot stand in for: it tells you
// where you ended up, not whether the input that got you there was any good.
// The maths is jump::StrafeFrame, mirroring PM_AirAccelerate exactly - see
// jump_logic.h for why an approximation would be worse than nothing.
//
// Air only. On the ground the answer is always "all of it", and the
// reconstruction would stop being exact anyway.
static void Jump_DrawStrafeMeter(const jump_overlay_ctx_t &ctx)
{
	const pmtype_t pm_type = ctx.ps->pmove.pm_type;

	if (pm_type == PM_NOCLIP || pm_type == PM_SPECTATOR)
		return;

	// Tied to the number above it: if that is hidden, this has nothing to
	// annotate.
	if (!ctx.speed->valid || ctx.speed->current < 1.f)
		return;

	const jump::strafe_readout_t &strafe = *ctx.strafe;

	const float bw = JUMP_STRAFE_W * ctx.scale;
	const float bh = JUMP_STRAFE_H * ctx.scale;
	const float bx = (float) ctx.VirtX(160.f) - bw * 0.5f;
	const float by = (float) ctx.BottomY(JUMP_SPEED_YB) + cgi.SCR_FontLineHeight(ctx.scale) +
					 JUMP_STRAFE_GAP * ctx.scale;

	const int px = ctx.scale < 1 ? 1 : ctx.scale;
	const int ix = (int) bx, iy = (int) by, iw = (int) bw, ih = (int) bh;

	// Border, then the empty track over it - the same two-rectangle idiom the
	// stock health bars use. "_white" is the engine's solid-fill primitive; it
	// needs no asset and nothing to precache.
	cgi.SCR_DrawColorPic(ix - px, iy - px, iw + 2 * px, ih + 2 * px, "_white", rgba_black);
	cgi.SCR_DrawColorPic(ix, iy, iw, ih, "_white", { 40, 40, 40, 200 });

	const bool centred = jump_hud_strafe->integer >= 2;

	// The reference mark: the centre when the bar is signed, the 90% target
	// when it is not. Static either way - a moving marker beside a moving bar
	// is two things to read.
	const int tick = centred ? ix + iw / 2 - px / 2 : ix + (int) (bw * 0.9f);
	cgi.SCR_DrawColorPic(tick, iy, px, ih, "_white", { 200, 200, 200, 160 });

	// No fill when there is nothing to be a fraction of. An empty track reads
	// as "no signal"; a full red bar would read as "you are doing it wrong",
	// and those are different claims. Not hidden entirely, because a hop chain
	// crosses the ground twice a second and an element that blinks at 2 Hz is
	// worse than one that sits still.
	if (!strafe.valid)
		return;

	const float loss = 1.f - strafe.efficiency;
	const rgba_t colour = Jump_StrafeColour(loss);

	if (centred)
	{
		// Fills outward from the centre. Which side says which way to correct;
		// how far says how much is being lost, since |offset| is 1 - efficiency
		// by construction.
		const int half = iw / 2;
		const int len = (int) (strafe.offset * half);

		if (len >= 0)
			cgi.SCR_DrawColorPic(ix + half, iy, len, ih, "_white", colour);
		else
			cgi.SCR_DrawColorPic(ix + half + len, iy, -len, ih, "_white", colour);
	}
	else
	{
		cgi.SCR_DrawColorPic(ix, iy, (int) (bw * strafe.efficiency), ih, "_white", colour);
	}
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
	const bool want_strafe = enabled && jump_hud_strafe && jump_hud_strafe->integer;

	// Clamped rather than trusted: a typo here would otherwise either freeze
	// the reading or blank it.
	if (jump_hud_strafe_tau)
	{
		int tau = jump_hud_strafe_tau->integer;

		if (tau < 50)
			tau = 50;
		if (tau > 2000)
			tau = 2000;

		Jump_CG_SetStrafeTau((uint64_t) tau);
	}

	// Called before the display gates, and every frame: it owns the sampling
	// flag the Pmove wrapper reads, so skipping it would leave the wrapper
	// collecting samples nobody is going to draw. It must be the OR of every
	// element that reads the ring, or a new element silently reads an empty one
	// the moment somebody turns the speedometer off.
	const jump::speed_readout_t &speed = Jump_CG_SampleFrame(ps, want_speed || want_strafe);

	if (!enabled)
		return;

	if (ps->stats[STAT_LAYOUTS] & (LAYOUTS_HIDE_HUD | LAYOUTS_INTERMISSION))
		return;

	const jump_overlay_ctx_t ctx { ps, hud_vrect, hud_safe, scale, &speed, &Jump_CG_StrafeReadout() };

	if (want_speed)
		Jump_DrawSpeedometer(ctx);

	if (want_strafe)
		Jump_DrawStrafeMeter(ctx);

	Jump_DrawPbDelta(ctx);
}
