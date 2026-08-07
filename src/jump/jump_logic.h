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

	bool on_ground = false;
	// False when on_ground belongs to somebody else - chase cam copies the
	// followed player's velocity but not their pm_flags, so the ground state is
	// the viewer's. Consumers must not treat it as the subject's.
	bool on_ground_valid = true;
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
