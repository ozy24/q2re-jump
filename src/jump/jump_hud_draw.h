// [Jump] cgame-side HUD overlay.
//
// Compiled against cg_local.h, not g_local.h: this is the client half of the
// DLL. It reads only player_state_t stats, so it stays prediction-safe and
// needs no extra network traffic.

#pragma once

// Registers the client-side cvars. Called once from CG_InitScreen.
void Jump_InitClientCvars();

void Jump_DrawHud(const player_state_t *ps, vrect_t hud_vrect, int32_t scale);
