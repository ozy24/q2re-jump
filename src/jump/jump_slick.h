// [Jump] Frictionless ground, shared by the game and cgame halves of the DLL.
//
// This is one line of logic in a header of its own for a reason: the strafe
// meter grades a frame on the server AND in the overlay, and which of the two
// draws it depends on the player's cvars (Jump_ServerDrawsStrafeBar). If the
// two halves disagreed about what counts as ice, a player running the DLL would
// see a different bar from a player on a stock client on the same brush. One
// definition, included by both, makes that impossible rather than unlikely.
//
// Only game.h is required, so the cgame can include it without dragging in any
// server-side declarations.

#pragma once

// Ground that pmove will not apply friction to.
//
// The test mirrors p_move.cpp:564 exactly, and the null case is not an
// oversight there: PM_Friction runs only when `pm->groundentity &&
// pml.groundsurface && !(pml.groundsurface->flags & SURF_SLICK)`, so a missing
// ground surface skips friction just as a slick one does. Both are therefore
// frames where nothing has been taken out of the velocity before PM_Accelerate
// sees it - which is the whole reason the strafe meter can grade them.
//
// Callers supply the trace themselves, because pmove keeps its ground surface in
// pml_t and never publishes it through pmove_t.
//
// A trace that hit NOTHING is not the null-surface case above, even though both
// leave `surface` null: it means there is no floor a quarter unit down, so
// pmove's own categorize is about to clear the ground and run the AIR branch.
// Grading that command against the ground model would be exactly the mistake the
// meter exists to avoid, so a miss reads as "not frictionless ground" and the
// caller falls back to its ordinary airborne handling.
[[nodiscard]] inline bool Jump_TraceHitFrictionlessGround(const trace_t &tr)
{
	if (tr.fraction >= 1.0f)
		return false;

	return !tr.surface || (tr.surface->flags & SURF_SLICK);
}
