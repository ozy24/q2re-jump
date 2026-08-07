// [Jump] Engine-free logic for the jump mod.
//
// Nothing in this header may include q_std.h, g_local.h or any other engine
// header: it is compiled both into the game DLL and into the host test binary
// (tests/jump_tests.vcxproj). Keep engine types out; the callers convert.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jump
{

// ---------------------------------------------------------------------------
// Time formatting
// ---------------------------------------------------------------------------

// Run times are shown the way both upstream mods show them: raw seconds with
// millisecond precision, e.g. "123.456".
std::string FormatTime(int64_t ms);

// Signed difference against a reference time, e.g. "-1.234" / "+1.234".
std::string FormatDelta(int64_t delta_ms);

// ---------------------------------------------------------------------------
// Store ring buffer
// ---------------------------------------------------------------------------

constexpr int MAX_STORES = 5;

struct store_slot_t
{
	float	origin[3] = { 0, 0, 0 };
	float	angles[3] = { 0, 0, 0 };
	int64_t elapsed_ms = 0; // run time already spent when the store was taken
	int32_t checkpoints = 0;
};

// Fixed-size ring. Push overwrites the oldest slot once full; Get(1) is the
// most recent store, Get(n) walks backwards and clamps to the oldest.
struct store_ring_t
{
	store_slot_t slots[MAX_STORES];
	int			 next = 0;  // write cursor
	int			 count = 0; // number of valid slots, <= MAX_STORES

	void Clear();
	void Push(const store_slot_t &slot);

	// prev == 1 is the most recent store. Returns nullptr when empty.
	// Values above the stack depth clamp to the oldest stored slot.
	const store_slot_t *Get(int prev) const;

	bool Empty() const { return count == 0; }
};

// ---------------------------------------------------------------------------
// Scoring
// ---------------------------------------------------------------------------

constexpr int MAX_HIGHSCORES = 15;

// Placement points, shared by both upstream mods:
// 1st..15th = 25, 20, 16, 13, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1.
// rank is 1-based; anything outside 1..15 scores nothing.
int PointsForRank(int rank);

// ---------------------------------------------------------------------------
// Map records
// ---------------------------------------------------------------------------

// One entry per player: their personal best on a map.
struct record_t
{
	std::string id;   // stable player identity (social id, or name fallback)
	std::string name; // display name as of the record
	int64_t		time_ms = 0;
	std::string date; // ISO-8601, for display only
};

// A map's high score table, kept sorted ascending by time.
struct map_records_t
{
	static constexpr int SCHEMA_VERSION = 1;

	std::string			  map;
	std::vector<record_t> times;

	// Record a completion. Each player holds exactly one entry - their best -
	// so a slower run by an existing player changes nothing.
	// Returns the new 1-based rank, or 0 if the table was not improved.
	int Submit(const record_t &rec);

	// 1-based rank of a player's entry, or 0 when they have none.
	int RankOf(const std::string &id) const;

	// The player's recorded time, or 0 when they have none.
	int64_t TimeOf(const std::string &id) const;

	// Points this player scores on this map, per PointsForRank.
	int PointsOf(const std::string &id) const;

	// Restore the ascending-by-time ordering. Ties keep insertion order, so
	// whoever set an equal time first stays ahead.
	void Sort();
};

// ---------------------------------------------------------------------------
// Active-players board
// ---------------------------------------------------------------------------

// One row of the players page of the scoreboard. Kept engine-free so the
// ordering below is testable on the host; the caller fills it from edict state.
struct player_row_t
{
	std::string name;
	std::string chasing;		  // spectators only: who they are following
	int64_t		session_ms = 0;	  // best ranked run since this map loaded; 0 = none
	int64_t		pb_ms = 0;		  // all-time personal best on this map; 0 = none
	bool		spectator = false;
	bool		practice = false; // practice team rather than ranked
};

// Order the players page.
//
// This is not cosmetic: the layout budget only fits eight or so rows, so the
// order decides who is worth the bytes. Anyone who has posted a time on this
// map comes first, fastest first - that is the session leaderboard, and the
// part nobody should lose to truncation. Everyone still to post one follows,
// ordered by their all-time best so the fast players are visible before they
// have done anything today. Spectators last. Stable within each group, so
// equal rows keep the order they were collected in and the board does not
// reshuffle itself between resends.
void SortPlayerRows(std::vector<player_row_t> &rows);

// ---------------------------------------------------------------------------
// Filenames
// ---------------------------------------------------------------------------

// Reduce an untrusted name (map name, player id) to something safe to use as a
// single path component: lowercased, [a-z0-9._-] preserved, everything else
// replaced with '_'. Leading dots are stripped so "..", ".." can't escape.
// Returns "_" for input that reduces to nothing.
std::string SafeName(const std::string &name);

// True when a token is usable as a map name.
//
// Map names reach the engine through `gamemap "<name>"`, so this is a
// validator rather than a sanitiser: anything that could break out of the
// quoting, walk the filesystem, or overflow a path is rejected outright.
// Mirrors the rules MuffMode applies to the same cvars.
bool IsSafeMapToken(const std::string &token);

// Make untrusted text safe to embed in a quoted layout-string token.
//
// This is a crash guard, not cosmetics: the client parses the layout it is
// sent, and a stray quote or backslash produces a malformed token stream that
// makes its parser raise a fatal error. Quotes, backslashes, control
// characters and high bytes all become spaces, runs of space collapse, and the
// result is trimmed and truncated. Empty input yields "?" so a row never
// collapses into an empty token.
std::string SanitizeLayoutText(const std::string &text, size_t max_len = 20);

// ---------------------------------------------------------------------------
// Map entity keys
// ---------------------------------------------------------------------------

// True when a trigger_push `target` marks it as a checkpoint barrier.
//
// Both upstream mods key this on the target *starting with* "checkpoint", so
// "checkpoint", "checkpoint3" and "checkpointfinal" all match while
// "mycheckpoint" does not. Upstream compares case-sensitively; this is
// deliberately case-insensitive, which can only widen the match and costs
// nothing — every instance in the original corpus is already lowercase.
bool IsCheckpointBarrierTarget(const std::string &target);

// ---------------------------------------------------------------------------
// Mset values
// ---------------------------------------------------------------------------

// Both upstream mods parse mset values with atoi, so "fasttele on" quietly
// means off and "gravity abc" quietly means zero. These return nullopt instead,
// so a typo in a cfg file or an `sv jump_mset` argument gets reported rather
// than silently changing how a map plays.

// Accepts 0/1, on/off, true/false, yes/no, case-insensitively. Any other whole
// number is true, matching atoi's behaviour for values like "2".
std::optional<bool> ParseMsetBool(const std::string &value);

// Whole-token integer: leading and trailing spaces are allowed, trailing
// garbage is not, so "800x" is rejected rather than read as 800.
std::optional<int> ParseMsetInt(const std::string &value);

// ---------------------------------------------------------------------------
// Speedometer
// ---------------------------------------------------------------------------

// The widest value the HUD's `num 4` field can draw. Over-wide values are
// truncated to their LEADING digits by the layout interpreter, so 12345 would
// render as 1234 - a wrong number that looks right. Clamping is a correctness
// requirement, not cosmetics.
constexpr int SPEED_STAT_MAX = 9999;

// Horizontal speed, in Quake units per second.
//
// X and Y are separate arguments so that no call site can reintroduce Z by
// accident. Z is excluded because strafe jumping is a horizontal-acceleration
// skill, because including it would spike the reading on every jump and every
// fall, and because the speed gates in trigger_push / misc_teleporter compare
// against this same XY quantity - a HUD that disagreed with the gate would be
// worse than none. Both upstream mods measure it the same way.
float HorizontalSpeed(float vel_x, float vel_y);

// HorizontalSpeed for the HUD stat: truncated rather than rounded (matching
// both upstreams, so the number agrees with a classic server's), clamped to
// [0, SPEED_STAT_MAX]. Non-finite input yields 0, since casting NaN to int is
// undefined. 0 is meaningful: it is what hides the element.
int SpeedStat(float vel_x, float vel_y);

// "1402" - whole units, same clamp as SpeedStat.
std::string FormatSpeed(float ups);

// "+38" / "-12" - signed whole units, for a gain/loss readout.
std::string FormatSpeedDelta(float ups);

// ---------------------------------------------------------------------------
// Movement sampling (client overlay)
// ---------------------------------------------------------------------------

// One rendered frame's worth of movement state, as observed by the cgame.
//
// The full field set is here from the start even though the speedometer reads
// only a few of them: the strafe meter, key display and per-jump stats that
// follow all take their inputs from this struct, and adding fields later would
// mean revisiting the sampler's replay-safety argument.
struct move_sample_t
{
	uint64_t time_ms = 0;		 // client time when the sample was committed
	float	 speed = 0.f;		 // HorizontalSpeed of `velocity`
	float	 velocity[3] = { 0, 0, 0 };
	float	 origin[3] = { 0, 0, 0 };
	float	 view_yaw = 0.f;
	float	 forwardmove = 0.f;
	float	 sidemove = 0.f;
	uint16_t buttons = 0;
	uint8_t	 msec = 0;

	// The velocity pmove started this command with. Not the same as `velocity`
	// one sample back: several commands can run between two rendered frames.
	// The strafe meter needs the value the acceleration actually saw.
	float vel_before[3] = { 0, 0, 0 };

	// Pitch reaches the move axes divided by three (see MoveAxes), so it cannot
	// be dropped even though the readouts are horizontal. Roll is zero in
	// practice; carried rather than assumed.
	float view_pitch = 0.f;
	float view_roll = 0.f;

	// pm_config.airaccel as of this command. Stored per sample rather than read
	// at draw time because it tracks CS_AIRACCEL and a server can change it
	// mid-map.
	int32_t air_accel = 0;

	uint8_t water_level = 0;

	bool on_ground = false;
	// False when on_ground belongs to somebody else - chase cam copies the
	// followed player's velocity but not their pm_flags, so the ground state is
	// the viewer's. Consumers must not treat it as the subject's.
	bool on_ground_valid = true;

	// Read BEFORE the move: pm_flags after it is the landing state, not the
	// state the movement branch was chosen from.
	bool on_ground_entry = false;
	// pm_time was already running: a land, teleport, waterjump or trick freeze.
	bool timed_move = false;
	// PM_NORMAL. Spectator and noclip take a different path entirely, and dead
	// and grapple change the branch.
	bool pm_normal = true;

	bool ducked = false;
	bool on_ladder = false;
	// This command contained the jump - PM_CheckJump cleared groundentity
	// partway through, so the branch changed under us.
	bool jumped = false;
	// False when the inputs are not real: demo playback runs pmove once with a
	// zeroed usercmd, which would otherwise read as "no keys pressed".
	bool inputs_valid = false;
	// False when the sample came from the last server snapshot rather than the
	// predicted state (cl_predict 0, prediction suppressed, or a frame that ran
	// no pmove at all).
	bool predicted = false;
	// The player was moved by something other than physics - teleport, recall,
	// respawn, or a gap in time. History before this sample is unrelated.
	bool discontinuity = false;
};

// ~1 s of history at 250 fps, ~4 s at 60. Enough for the trend window and a
// typical airtime; the per-jump readout keeps its own summary rather than
// scanning further back, so this never needs to grow.
constexpr int MOVE_SAMPLES = 256;

// Fixed-size ring, same contract as store_ring_t above.
struct move_ring_t
{
	move_sample_t samples[MOVE_SAMPLES];
	int			  next = 0;
	int			  count = 0;

	void Clear();
	void Push(const move_sample_t &sample);

	// prev == 1 is the newest sample. Returns nullptr when empty; values past
	// the history depth clamp to the oldest sample held.
	const move_sample_t *Get(int prev) const;

	// The newest sample at or before (now_ms - age_ms), for comparing against
	// where you were a moment ago. Returns nullptr when the history does not
	// reach that far back, and refuses to look past a discontinuity - comparing
	// across a teleport would report a speed change nobody made.
	const move_sample_t *AtAge(uint64_t now_ms, uint64_t age_ms) const;

	bool Empty() const { return count == 0; }
};

// What the overlay draws: the live speed, the best of the current jump, and
// whether you are gaining or losing.
struct speed_readout_t
{
	float current = 0.f;
	float peak = 0.f;
	float delta = 0.f; // current minus the speed one window ago
	int	  trend = 0;   // -1 losing, 0 inside the deadband, +1 gaining
	bool  valid = false;
};

// Peak resets once the player has been continuously grounded for longer than
// this, so a bunny-hop chain keeps its peak across hops while standing still
// clears it.
constexpr uint64_t SPEED_PEAK_GROUND_MS = 150;

// Fallback for when the ground state is not ours to read (see
// move_sample_t::on_ground_valid): drop the peak if nothing has matched it for
// this long, or a spectator's peak latches at the highest value ever seen.
constexpr uint64_t SPEED_PEAK_DECAY_MS = 1500;

// Tracks the peak and trend across samples. Update() consumes exactly one new
// sample - the newest in the ring - so call it once per committed sample.
struct speed_state_t
{
	float	 peak = 0.f;
	uint64_t peak_time_ms = 0;
	uint64_t grounded_since_ms = 0;
	bool	 grounded = false;

	void Reset();

	speed_readout_t Update(const move_ring_t &ring, uint64_t window_ms = 250, float deadband_ups = 2.f);
};

// ---------------------------------------------------------------------------
// Strafe efficiency
// ---------------------------------------------------------------------------
//
// How much of the acceleration that was available on a frame the player's
// input actually captured. This is the one thing the speed number genuinely
// cannot tell you: it says where you ended up, not whether the input that got
// you there was any good. At 400 ups the difference between a 43 degree strafe
// and a 45 degree one is the difference between taking everything on offer and
// taking none of it, and nothing else on screen shows that.
//
// Everything here mirrors p_move.cpp exactly. Getting it approximately right
// would be worse than not having it, because the meter would confidently tell
// players to do the wrong thing.

// pmove's movement constants, copied rather than included - this layer must
// stay engine-free. None are cvar-driven (p_move.cpp:181-189). PM_AIR_TARGET is
// PM_AirAccelerate's `if (wishspd > 30)` cap.
constexpr float PM_MAXSPEED = 300.f;
constexpr float PM_DUCKSPEED = 100.f;
constexpr float PM_AIR_TARGET = 30.f;

// forward.xy and right.xy as PM_AirMove sees them - which is NOT what the view
// angles say. Before building the move axes, pmove wraps pitch into (-180,180]
// and divides it by three (p_move.cpp:1684-1692), so looking down changes your
// effective strafe direction. At roll 0 - what PM_ClampAngles produces in
// practice - only forward carries the resulting cos(pitch/3); right is
// (sin yaw, -cos yaw) and has no pitch in it at all. Degrees, Quake convention.
void MoveAxes(float pitch_deg, float yaw_deg, float roll_deg, float out_forward_xy[2],
			  float out_right_xy[2]);

// PM_AirMove's wishvel: forward.xy * forwardmove + right.xy * sidemove, with z
// dropped (p_move.cpp:845-848). PM_AddCurrents is deliberately not modelled -
// every frame where it could do anything is excluded before we get here.
void WishVelocity(float pitch_deg, float yaw_deg, float roll_deg, float forwardmove, float sidemove,
				  float out_xy[2]);

// The speed change PM_Accelerate produces, given `along` = velocity . wishdir:
//   a = clamp(target - along, 0, budget), then |v'| = sqrt(s^2 + 2*a*along + a^2).
// Negative when you accelerate into your own velocity, which is a real thing a
// player can do and must not be clamped away here.
//
// Computed in double and rearranged: sqrt(s^2 + d) - s cancels catastrophically
// in float at jump speeds, and the equivalent d / (sqrt(...) + s) does not.
float AccelGain(float speed, float along, float target, float budget);

// The projection that would have gained the most - the denominator of the whole
// meter.
//
// It is NOT simply target - budget. In the region between (target - budget) and
// target the algebra collapses to |v'|^2 = s^2 + target^2 - along^2, a downward
// parabola peaking at along = 0. So when the budget exceeds the target - which
// is exactly what the 30-clamp air model does - the best you can do is strafe
// perpendicular to your own velocity, and clamping the low end to anything
// below zero reports a large speed LOSS as the maximum gain.
float BestAlong(float speed, float target, float budget);

// Below this a frame offered nothing worth measuring. Note that gain_max is
// exactly zero only when target or budget is zero, so the guards in StrafeFrame
// are exhaustive and this is a numerical floor rather than a real case.
constexpr float STRAFE_MIN_GAIN = 1e-4f;

// One command's strafe accounting.
struct strafe_frame_t
{
	float wishspeed = 0.f;	// |wishvel|, clamped to maxspeed
	float target = 0.f;		// what the projection is allowed to reach
	float budget = 0.f;		// accel * wishspeed * dt
	float along = 0.f;		// velocity . wishdir
	float along_best = 0.f; // the projection that would have gained most
	float gain = 0.f;		// horizontal ups gained; may be negative
	float gain_max = 0.f;	// the most any direction could have gained

	// along < along_best: aiming too far across your own velocity. The sign is
	// what tells a player which way to correct, and it is not derivable from
	// the efficiency alone.
	bool over_turning = false;
	bool opportunity = false;
};

// `air_accel` is pm_config.airaccel, i.e. sv_airaccelerate as it arrives on
// CS_AIRACCEL. 0 - the rerelease default - selects PM_Accelerate with accel 1
// and target = wishspeed; non-zero selects PM_AirAccelerate, where the target
// is min(wishspeed, 30) but the budget still uses the UNCLAMPED wishspeed
// (p_move.cpp:612-629). That asymmetry is the whole of strafe jumping.
//
// `dt` is cmd.msec / 1000. Air frames only - see ClassifyStrafeFrame.
strafe_frame_t StrafeFrame(const float vel_before_xy[2], float pitch_deg, float yaw_deg,
						   float roll_deg, float forwardmove, float sidemove, float dt, bool ducked,
						   int air_accel);

enum class strafe_frame_kind_t
{
	excluded,		// the reconstruction would not be exact - say nothing
	no_opportunity, // exact, but there was nothing to capture
	usable
};

// The meter is air-only, and that is a correctness argument rather than a
// simplification. In the air, with no water, no ladder and no timed state,
// PM_Friction's drop is zero and PM_CheckJump returns before touching velocity
// - so the velocity captured at the top of the pmove wrapper IS the velocity
// PM_Accelerate sees, and nothing is approximated. On the ground that stops
// being true, and SURF_SLICK is never surfaced through pmove_t, so a ground
// meter would quietly misreport on every ice brush.
strafe_frame_kind_t ClassifyStrafeFrame(const move_sample_t &sample);

// Memory of the smoothing below, in ms. Roughly one airtime.
constexpr uint64_t STRAFE_TAU_MS = 300;

// Below this there is nothing to be a fraction of.
constexpr float STRAFE_MIN_WEIGHT = 0.05f;

struct strafe_readout_t
{
	float efficiency = 0.f; // 0..1: of what was on offer, how much you took
	float offset = 0.f;		// -1..+1: which way you erred; |offset| == 1 - efficiency
	float frame = 0.f;		// this frame's raw ratio, for tests and debugging
	bool  opportunity = false;
	bool  valid = false;
};

// Smooths in the ratio domain, which is what makes the reading independent of
// frame rate: prediction re-runs the pending partial command every rendered
// frame, so anything counted or summed on this ring would be counted several
// times, but a ratio is not.
//
// A decayed ratio of sums rather than a mean of per-frame ratios, so a frame
// weighs what was actually at stake on it - 0.2 ups available at 1500 ups
// should not count for as much as 7.5 at 400. An exponential decay rather than
// a sliding window because a window steps visibly as samples fall off the back.
struct strafe_state_t
{
	float	 weight = 0.f; // decayed sum of gain_max
	float	 taken = 0.f;  // decayed sum of max(gain, 0)
	float	 signed_loss = 0.f;
	uint64_t last_time_ms = 0;
	bool	 have_last = false;

	void Reset();

	// Fold one command's accounting in. `dt_ms` is the time since the last one,
	// which sets how much the running totals decay - so a gap in sampling fades
	// the reading rather than freezing it.
	//
	// Both halves of the mod come through here: the client builds a frame from
	// its per-rendered-frame samples, the server from each usercmd it is handed.
	// That is why it takes a frame rather than a ring.
	strafe_readout_t Add(const strafe_frame_t &frame, bool usable, uint64_t dt_ms,
						 uint64_t tau_ms = STRAFE_TAU_MS);

	// The client path: build the frame from the newest sample in the ring.
	strafe_readout_t Update(const move_ring_t &ring, uint64_t tau_ms = STRAFE_TAU_MS);
};

// How many cells the statusbar's strafe bar has. The bar is drawn as text
// picked from a set of pre-rendered strings, so this is also how many of those
// strings there are, plus one for empty.
constexpr int STRAFE_BAR_SEGMENTS = 12;

// Which of those strings to show: 0..STRAFE_BAR_SEGMENTS filled cells, or -1
// when there is nothing to show. Fill is EFFICIENCY - full means you are
// taking everything on offer, which is the direction every other bar in every
// other game already trained people to expect.
int StrafeBarLevel(const strafe_readout_t &readout, int segments = STRAFE_BAR_SEGMENTS);

// One of those pre-rendered strings, e.g. "[####--------]". Built once per level
// and parked in configstrings, so showing the bar costs a stat pointing at one
// of them rather than a text write per frame.
std::string StrafeBarString(int level, int segments = STRAFE_BAR_SEGMENTS);

// ---------------------------------------------------------------------------
// jumpers visibility / audio policy (engine-free)
// ---------------------------------------------------------------------------

// Whether `ent` should be drawn for `viewer` under the jumpers/eyecam rules.
// Self is always visible; a first-person followed body is always hidden;
// otherwise other players are hidden when the viewer has jumpers off.
bool PlayerVisibleToViewer(bool show_jumpers, bool eyecam_following_target, bool ent_is_viewer);

// Whether `viewer` should hear a sound originating from `source`.
// Self is always audible; other sources are muted when the viewer has jumpers off.
bool PlayerAudibleToViewer(bool show_jumpers, bool ent_is_viewer);

} // namespace jump
