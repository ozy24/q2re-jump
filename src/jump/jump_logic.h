// [Jump] Engine-free logic for the jump mod.
//
// Nothing in this header may include q_std.h, g_local.h or any other engine
// header: it is compiled both into the game DLL and into the host test binary
// (tests/jump_tests.vcxproj). Keep engine types out; the callers convert.

#pragma once

#include <cstdint>
#include <string>

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
// Filenames
// ---------------------------------------------------------------------------

// Reduce an untrusted name (map name, player id) to something safe to use as a
// single path component: lowercased, [a-z0-9._-] preserved, everything else
// replaced with '_'. Leading dots are stripped so "..", ".." can't escape.
// Returns "_" for input that reduces to nothing.
std::string SafeName(const std::string &name);

} // namespace jump
