// [Jump] cgame-side HUD overlay.
//
// Compiled against cg_local.h, not g_local.h: this is the client half of the
// DLL. It reads only player_state_t and the movement samples collected in
// jump_cg_move.cpp, so it stays prediction-safe and needs no extra network
// traffic.

#pragma once

// Registers the client-side cvars and clears any movement history left over
// from the last connection. Called once from CG_InitScreen.
void Jump_InitClientCvars();

void Jump_DrawHud(int32_t isplit, const player_state_t *ps, vrect_t hud_vrect, vrect_t hud_safe, int32_t scale);
