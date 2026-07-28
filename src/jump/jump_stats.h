// [Jump] HUD stat slots, shared by the game and cgame halves of the DLL.
//
// The stat table is full, so jump reuses the CTF block (18-31). That is safe
// because Jump_Init() forces ctf/teamplay off and Jump_SetStats() runs after
// SetCTFStats() in G_SetStats(), making jump the last writer.
//
// Only bg_local.h is required to use this header, so the cgame can include it
// without dragging in any server-side declarations.

#pragma once

constexpr player_stat_t JUMP_STAT_TIME_SEC = STAT_CTF_TEAM1_PIC;			// 18: run time, whole seconds
constexpr player_stat_t JUMP_STAT_TIME_MS = STAT_CTF_TEAM1_CAPS;			// 19: run time, hundredths
constexpr player_stat_t JUMP_STAT_RUN_STATE = STAT_CTF_TEAM2_PIC;			// 20: jump_run_state_t
constexpr player_stat_t JUMP_STAT_STORES = STAT_CTF_TEAM2_CAPS;				// 21: stores held
constexpr player_stat_t JUMP_STAT_TEAM = STAT_CTF_FLAG_PIC;					// 22: jump_team_t
constexpr player_stat_t JUMP_STAT_PB_SEC = STAT_CTF_JOINED_TEAM1_PIC;		// 23: personal best, whole seconds
constexpr player_stat_t JUMP_STAT_PB_MS = STAT_CTF_JOINED_TEAM2_PIC;		// 24: personal best, hundredths
constexpr player_stat_t JUMP_STAT_CHECKPOINTS = STAT_CTF_TEAM1_HEADER;		// 25: checkpoints taken
constexpr player_stat_t JUMP_STAT_CHECKPOINT_TOTAL = STAT_CTF_TEAM2_HEADER; // 26: checkpoints required
constexpr player_stat_t JUMP_STAT_ENABLED = STAT_CTF_TECH;					// 27: 1 while jump mode owns the level
// 28-31 reserved (replay rank, vote countdown).

// Mirrors jump_team_t / jump_run_state_t, which the cgame can't see.
constexpr int16_t JUMP_TEAM_SPECTATOR = 0;
constexpr int16_t JUMP_TEAM_EASY = 1;
constexpr int16_t JUMP_TEAM_HARD = 2;

constexpr int16_t JUMP_RUN_IDLE = 0;
constexpr int16_t JUMP_RUN_RUNNING = 1;
constexpr int16_t JUMP_RUN_FINISHED = 2;
