// [Jump] Client-side movement sampling. See jump_cg_move.cpp.
//
// Include after cg_local.h - like jump_hud_draw.h, this relies on the includer
// for pmove_t and player_state_t rather than dragging the engine headers in.

#pragma once

#include "jump_logic.h"

// The cglobals.Pmove target (cg_main.cpp). Observes the movement the client
// just predicted and then calls the real Pmove with an untouched pmove_t, so a
// stock client's prediction and this one stay identical.
void Jump_CG_Pmove(pmove_t *pm);

// Drops all history. Called from Jump_InitClientCvars, which CG_InitScreen runs
// on every map change and reconnect.
void Jump_CG_ResetSamples();

// Commits at most one sample and returns the current readout. Call exactly once
// per rendered frame. `wanted` is the overlay's cvar state: when false nothing
// is sampled and the wrapper above costs one bool test per call.
const jump::speed_readout_t &Jump_CG_SampleFrame(const player_state_t *ps, bool wanted);
