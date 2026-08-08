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

// The strafe meter, as an index into the pre-rendered bar strings in
// CONFIG_JUMP_STRAFE_BAR. A stat_string indexes a configstring BY VALUE, so 0
// means "hidden" and the row must sit inside an ifstat - an ungated 0 would
// draw configstring 0.
//
// Text rather than the health_bars token, which was tried first: that one is
// the only graphical bar the layout can draw, but it is fixed at roughly half
// the screen width and only ever red, so it was both enormous and had to be
// inverted to mean "waste" for red to make sense. A row of characters costs one
// stat and a handful of configstrings written once per level, and in exchange
// the width, the colour and the direction are all ours.
constexpr player_stat_t JUMP_STAT_STRAFE_BAR = (player_stat_t) 54;

static_assert((int) JUMP_STAT_STRAFE_BAR >= (int) STAT_LAST, "jump stat overlaps an engine stat");
static_assert((int) JUMP_STAT_STRAFE_BAR < (int) MAX_STATS, "jump stat out of range");

// The speedometer, one stat per digit. Each points at one of the pre-rendered
// digit strings in CONFIG_JUMP_DIGIT, or at the blank one for a leading zero.
//
// Four stats for one number looks extravagant until you try the alternatives.
// No token formats a stat into text, so the choices are the `num` pic font -
// which is 16x24 per digit and dominates the screen - or pre-rendering, and
// pre-rendering whole speeds would cost a configstring per value. Per-digit
// costs ten strings, total, for every number from 0 to 9999.
constexpr player_stat_t JUMP_STAT_SPEED_D1000 = (player_stat_t) 55;
constexpr player_stat_t JUMP_STAT_SPEED_D100 = (player_stat_t) 56;
constexpr player_stat_t JUMP_STAT_SPEED_D10 = (player_stat_t) 57;
constexpr player_stat_t JUMP_STAT_SPEED_D1 = (player_stat_t) 58;

static_assert((int) JUMP_STAT_SPEED_D1 < (int) MAX_STATS, "jump stat out of range");

// All 13 usable CTF slots (18-31 less 27) are taken, so the CTF block is closed.
// New stats go in 54-63, which are genuinely free: STAT_LAST is 54
// (bg_local.h) and MAX_STATS is 64 (game.h), so nothing owns that range - and
// nothing reads it either, which makes it safer ground than the CTF block,
// where 27's problem is precisely that the stock statusbar reads it. 54 is the
// strafe bar and 55-58 the speedometer's digits; 59-63 remain.
//
// Ten, not eleven: bg_local.h's own static_assert allows
// STAT_LAST == MAX_STATS + 1, which would permit a stat at index 64, one past
// the end of a 64-element array. Guard a new stat with `< MAX_STATS` rather
// than trusting that one.

constexpr int16_t JUMP_RUN_IDLE = 0;
constexpr int16_t JUMP_RUN_RUNNING = 1;
constexpr int16_t JUMP_RUN_FINISHED = 2;

// Where the two performance readouts sit, in layout units up from the bottom of
// the screen - the statusbar's `yb` anchor, and the overlay mirrors that
// arithmetic exactly (see jump_overlay_ctx_t).
//
// Shared because BOTH halves draw these two rows: the server emits them into
// CS_STATUSBAR for every player, and the overlay redraws the same two for anyone
// running this DLL. Only one copy is ever on screen - the overlay tells the
// server to drop its own - so the anchors have to agree, or the readout hops
// when `jump_hud` is toggled. They drifted 32 units apart while each half owned
// its own copy, which is why there is only one now.
//
// Centred, one block up from the bottom edge, where a first-person player is
// already looking. Both upstream mods put their speedometer in the bottom-right
// corner, which is a glance away from the map at exactly the moment you want the
// number; q2re-map-trainer centres it, and this follows that. -82 leaves 14
// units of clearance above the FOLLOWING / SPECTATOR rows at -68.
constexpr int JUMP_HUD_SPEED_YB = -96;
constexpr int JUMP_HUD_STRAFE_YB = -82;

// yb counts upwards from the bottom edge, so "below" is the larger value. Easy
// to get backwards, and a swap would put the bar over the number.
static_assert(JUMP_HUD_STRAFE_YB > JUMP_HUD_SPEED_YB, "the strafe bar sits below the speed number");
