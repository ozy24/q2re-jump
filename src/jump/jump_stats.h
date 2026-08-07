// [Jump] HUD stat slots, shared by the game and cgame halves of the DLL.
//
// The stat table is full, so jump reuses the CTF block (18-31). That is safe
// because Jump_Init() forces ctf/teamplay off and Jump_SetStats() runs after
// SetCTFStats() in G_SetStats(), making jump the last writer.
//
// Only bg_local.h is required to use this header, so the cgame can include it
// without dragging in any server-side declarations.
//
// The live run timer's fraction is published as three single-digit stats
// rather than one 0-999 value. The statusbar's `num` token right-aligns
// inside a fixed field and never pads, so a value of 5 would draw as "  5"
// and a time of 25.005 would read as "25.  5". One digit per field cannot do
// that. Personal best sidesteps the problem entirely - see below.

#pragma once

constexpr player_stat_t JUMP_STAT_TIME_SEC = STAT_CTF_TEAM1_PIC;			 // 18: run time, whole seconds
constexpr player_stat_t JUMP_STAT_TIME_HUN_TENS = STAT_CTF_TEAM1_CAPS;		 // 19: run time, first decimal digit
constexpr player_stat_t JUMP_STAT_TIME_HUN_UNITS = STAT_CTF_TEAM2_PIC;		 // 20: run time, second decimal digit
constexpr player_stat_t JUMP_STAT_RUN_STATE = STAT_CTF_TEAM2_CAPS;			 // 21: jump_run_state_t
constexpr player_stat_t JUMP_STAT_STORES = STAT_CTF_FLAG_PIC;				 // 22: stores held
constexpr player_stat_t JUMP_STAT_TEAM_PRACTICE = STAT_CTF_JOINED_TEAM1_PIC; // 23: 1 while on Practice
constexpr player_stat_t JUMP_STAT_TEAM_RANKED = STAT_CTF_JOINED_TEAM2_PIC;	 // 24: 1 while on Ranked
constexpr player_stat_t JUMP_STAT_CHECKPOINTS = STAT_CTF_TEAM1_HEADER;		 // 25: checkpoints taken
constexpr player_stat_t JUMP_STAT_CHECKPOINT_TOTAL = STAT_CTF_TEAM2_HEADER;	 // 26: checkpoints required

// 27 (STAT_CTF_TECH) is deliberately left alone. The stock statusbar draws a
// pic from it in EVERY deathmatch game rather than only under teamplay
// (g_spawn.cpp, after the teamplay branch), so any value we put there would
// render as an arbitrary image the moment the stock script ran.

// Personal best is a `stat_string`, not three digit stats: it changes only on
// a new PB (a rare event), so it can afford to be a fully-formatted string
// ("12.345", via jump::FormatTime) fetched from a per-client configstring
// instead of costing one slot per digit. See CONFIG_JUMP_PB_STRING in
// bg_local.h and Jump_UpdatePbString() in jump_hud.cpp. 0 means "no PB yet",
// matching the old ifstat(JUMP_STAT_PB_SEC) gate.
constexpr player_stat_t JUMP_STAT_PB_STRING = STAT_CTF_ID_VIEW; // 28: stat_string -> CONFIG_JUMP_PB_STRING + client#

// The two slots that freed up: one becomes the run timer's third decimal
// digit (matching PB's millisecond precision), one carries the announcement
// banner.
constexpr player_stat_t JUMP_STAT_TIME_THOU = STAT_CTF_MATCH; // 29: run time, third decimal digit

// The banner across the top of the HUD for a new PB or a new map record. Same
// reasoning as the PB string - rare event, arbitrary text - but the text is
// global rather than per-client, so it points at a single configstring. 0
// means "nothing showing", which is what the ifstat gate in the statusbar
// tests: stat_string indexes a configstring BY VALUE, so an ungated 0 would
// draw configstring 0.
constexpr player_stat_t JUMP_STAT_ANNOUNCE = STAT_CTF_ID_VIEW_COLOR; // 30: stat_string -> CONFIG_JUMP_ANNOUNCE

constexpr player_stat_t JUMP_STAT_ENABLED = STAT_CTF_TEAMINFO; // 31: 1 while jump mode owns the level

// Sizing for CONFIG_JUMP_PB_STRING's per-client block in bg_local.h - keep
// the two in sync. Clients past this index just don't get a PB display.
constexpr int JUMP_MAX_PB_STRING_CLIENTS = 64;

// ---------------------------------------------------------------------------
// Beyond the CTF block
// ---------------------------------------------------------------------------
//
// All 13 usable CTF slots (18-31 less 27) are taken, so the speedometer takes
// the first genuinely free index instead. STAT_LAST is 54 (bg_local.h) and
// MAX_STATS is 64 (game.h), so 54-63 belong to nobody.
//
// That makes 54 strictly SAFER than any CTF slot: 27 is forbidden because the
// stock statusbar draws a pic from it, whereas nothing anywhere reads 54 -
// every stat CG_ExecuteLayoutString reads is either an index taken from our own
// layout string or one of a fixed named set, all 53 or below.
//
// Horizontal speed in units per second, truncated and clamped to
// jump::SPEED_STAT_MAX. The clamp is not cosmetic: the statusbar draws this in
// a `num 4` field, and an over-wide value is truncated to its LEADING digits,
// so an unclamped 12345 would read as a believable 1234. 0 means "hide", which
// is what the ifstat gate in Jump_EmitStatusbar tests - and is the hook any
// future per-player toggle would use, since CS_STATUSBAR is one shared
// broadcast layout that cannot vary between clients.
constexpr player_stat_t JUMP_STAT_SPEED = (player_stat_t) 54;

// If an upstream merge ever grows STAT_LAST past 54, fail the build rather than
// let two stats share a slot. The upper bound is MAX_STATS, not the upstream
// static_assert in bg_local.h: that one allows STAT_LAST == MAX_STATS + 1,
// which is one past the end of a 64-element array.
static_assert((int) JUMP_STAT_SPEED >= (int) STAT_LAST, "jump stat overlaps an engine stat");
static_assert((int) JUMP_STAT_SPEED < (int) MAX_STATS, "jump stat out of range");

// The CTF block is closed. New stats go in 54-63; 54 is taken, 55-63 remain.

constexpr int16_t JUMP_RUN_IDLE = 0;
constexpr int16_t JUMP_RUN_RUNNING = 1;
constexpr int16_t JUMP_RUN_FINISHED = 2;
