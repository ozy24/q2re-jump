// [Jump] Per-viewer player body sounds for the jumpers toggle.
//
// Footsteps / falls / ladder steps stay on the entity as EV_* events. Jumpers-off
// viewers already omit other players via SVF_INSTANCED, so those events never
// reach them — no unicast fan-out (which overflows the datagram).
//
// Rare explicit gi.sound paths (jump, water enter/exit, gasp, drown, wade, burn)
// are re-emitted via gi.local_sound only when someone has jumpers off.

#include "../g_local.h"
#include "jump_local.h"

void Jump_PlayerSound(edict_t *ent, soundchan_t channel, int soundindex, float volume, float attenuation,
					  float timeofs)
{
	if (!ent)
		return;

	if (!Jump_Active() || !Jump_AnyHideJumpers())
	{
		gi.sound(ent, channel, soundindex, volume, attenuation, timeofs);
		return;
	}

	const uint32_t key = GetUnicastKey();

	for (uint32_t i = 1; i <= game.maxclients; i++)
	{
		edict_t *viewer = g_edicts + i;
		if (!viewer->inuse || !viewer->client || !viewer->client->pers.connected)
			continue;

		jump_client_t *vjc = Jump_ClientData(viewer);
		const bool show_jumpers = !vjc || vjc->show_jumpers;

		if (!jump::PlayerAudibleToViewer(show_jumpers, viewer == ent))
			continue;

		gi.local_sound(viewer, ent, channel, soundindex, volume, attenuation, timeofs, key);
	}
}
