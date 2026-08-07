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
