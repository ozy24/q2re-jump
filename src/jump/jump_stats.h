// [Jump] HUD stat slots, shared by the game and cgame halves of the DLL.
//
// The stat table is full, so jump reuses the CTF block (18-31). That is safe
// because Jump_Init() forces ctf/teamplay off and Jump_SetStats() runs after
// SetCTFStats() in G_SetStats(), making jump the last writer.
//
// Only bg_local.h is required to use this header, so the cgame can include it
// without dragging in any server-side declarations.
//
// Fractions are published as two single-digit stats rather than one 0-99
// value. The statusbar's `num` token right-aligns inside a fixed field and
// never pads, so a hundredths value of 5 would draw as " 5" and a time of
// 25.05 would read as "25. 5". One digit per field cannot do that.

#pragma once

constexpr player_stat_t JUMP_STAT_TIME_SEC = STAT_CTF_TEAM1_PIC;			 // 18: run time, whole seconds
constexpr player_stat_t JUMP_STAT_TIME_HUN_TENS = STAT_CTF_TEAM1_CAPS;		 // 19: hundredths, tens digit
constexpr player_stat_t JUMP_STAT_TIME_HUN_UNITS = STAT_CTF_TEAM2_PIC;		 // 20: hundredths, units digit
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

constexpr player_stat_t JUMP_STAT_PB_SEC = STAT_CTF_ID_VIEW;			// 28: personal best, whole seconds
constexpr player_stat_t JUMP_STAT_PB_HUN_TENS = STAT_CTF_MATCH;			// 29: PB hundredths, tens digit
constexpr player_stat_t JUMP_STAT_PB_HUN_UNITS = STAT_CTF_ID_VIEW_COLOR; // 30: PB hundredths, units digit
constexpr player_stat_t JUMP_STAT_ENABLED = STAT_CTF_TEAMINFO;			// 31: 1 while jump mode owns the level

// That is all 13 usable slots (18-31 less 27). Adding another display value
// means reclaiming one of these, not finding a spare.

constexpr int16_t JUMP_RUN_IDLE = 0;
constexpr int16_t JUMP_RUN_RUNNING = 1;
constexpr int16_t JUMP_RUN_FINISHED = 2;
