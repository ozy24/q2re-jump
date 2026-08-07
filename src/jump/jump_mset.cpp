// [Jump] Per-map settings ("msets").
//
// Two sources, applied in order so the server always wins per key:
//   1. the map's own worldspawn "mset" key, e.g. mset "gravity 400 rocket 1"
//   2. <data>/mset/<map>.cfg, one `key value` per line
//
// The worldspawn key is parsed straight out of the entity string rather than
// through spawn_temp_t, which keeps the upstream spawn field table untouched.

#include "../g_local.h"
#include "jump_local.h"

#include <string>
#include <utility>
#include <vector>

jump_mset_t jump_mset;

// ---------------------------------------------------------------------------
// Applying values
// ---------------------------------------------------------------------------

// Distinguishes an unknown key from a bad value so the caller can say which it
// was; both upstream mods parse with atoi and accept either silently.
jump_mset_result_t Jump_ApplyMset(const std::string &key, const std::string &value)
{
	// Keys are matched case-insensitively, like every other command in this
	// file and like classic q2jump's Q_stricmp.
	const auto named = [&key](const char *name) { return Q_strcasecmp(key.c_str(), name) == 0; };

	// Assigns through `field` only when the value parses, so a rejected mset
	// leaves the previous value alone rather than half-applying.
	const auto set_bool = [&value](bool &field) {
		const std::optional<bool> parsed = jump::ParseMsetBool(value);

		if (!parsed)
			return false;

		field = *parsed;
		return true;
	};

	const auto set_int = [&value](int &field, bool &was_set) {
		const std::optional<int> parsed = jump::ParseMsetInt(value);

		if (!parsed)
			return false;

		field	= *parsed;
		was_set = true;
		return true;
	};

	bool ok;

	if (named("gravity"))
		ok = set_int(jump_mset.gravity, jump_mset.gravity_set);
	// The code in Q2JumpRefresh calls this "checkpoints" while its own docs
	// and the classic mod call it "checkpoint_total"; accept either.
	else if (named("checkpoints") || named("checkpoint_total"))
		ok = set_int(jump_mset.checkpoint_total, jump_mset.checkpoint_total_set);
	else if (named("damage"))
		ok = set_bool(jump_mset.damage);
	else if (named("fasttele"))
		ok = set_bool(jump_mset.fasttele);
	else if (named("rocket"))
		ok = set_bool(jump_mset.weapon_rocket);
	else if (named("grenadelauncher"))
		ok = set_bool(jump_mset.weapon_grenadelauncher);
	else if (named("hyperblaster"))
		ok = set_bool(jump_mset.weapon_hyperblaster);
	else if (named("bfg"))
		ok = set_bool(jump_mset.weapon_bfg);
	else
	{
		Jump_Log("unknown mset '%s'", key.c_str());
		return jump_mset_result_t::unknown_key;
	}

	if (!ok)
	{
		Jump_Log("bad value '%s' for mset '%s'", value.c_str(), key.c_str());
		return jump_mset_result_t::bad_value;
	}

	Jump_Log("mset %s = %s", key.c_str(), value.c_str());

	return jump_mset_result_t::ok;
}

// Space-separated "key value key value ..." as used by the worldspawn key.
static void Jump_ApplyMsetPairs(const std::string &text)
{
	std::vector<std::string> tokens;
	std::string				 current;

	for (char c : text)
	{
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
		{
			if (!current.empty())
			{
				tokens.push_back(current);
				current.clear();
			}
		}
		else
			current.push_back(c);
	}

	if (!current.empty())
		tokens.push_back(current);

	if (tokens.size() % 2 != 0)
	{
		gi.Com_PrintFmt("[jump] worldspawn mset has an odd number of tokens; ignoring\n");
		return;
	}

	for (size_t i = 0; i + 1 < tokens.size(); i += 2)
		Jump_ApplyMset(tokens[i], tokens[i + 1]);
}

// ---------------------------------------------------------------------------
// Sources
// ---------------------------------------------------------------------------

// Pull the "mset" key out of the first entity block (worldspawn) of the raw
// entity string.
static void Jump_LoadWorldspawnMsets(const char *entities)
{
	if (!entities)
		return;

	const char *p = strchr(entities, '{');

	if (!p)
		return;

	p++;

	std::string key, value;
	bool		reading_key = true;

	while (*p && *p != '}')
	{
		if (*p != '"')
		{
			p++;
			continue;
		}

		p++; // opening quote

		std::string token;

		while (*p && *p != '"')
			token.push_back(*p++);

		if (*p == '"')
			p++; // closing quote

		if (reading_key)
			key = token;
		else
		{
			value = token;

			if (key == "mset")
				Jump_ApplyMsetPairs(value);
		}

		reading_key = !reading_key;
	}
}

static void Jump_LoadMsetFile(const char *mapname)
{
	const std::filesystem::path path = Jump_MsetPath(mapname);

	std::string text;

	if (!Jump_ReadFile(path, text))
		return;

	// One `key value` per line; # starts a comment. Values may be quoted.
	size_t start = 0;

	while (start <= text.size())
	{
		size_t end = text.find('\n', start);

		if (end == std::string::npos)
			end = text.size();

		std::string line = text.substr(start, end - start);
		start = end + 1;

		const size_t comment = line.find('#');

		if (comment != std::string::npos)
			line.erase(comment);

		// Quotes are optional in this format, so just drop them.
		std::string cleaned;

		for (char c : line)
			if (c != '"')
				cleaned.push_back(c);

		if (cleaned.find_first_not_of(" \t\r") == std::string::npos)
			continue;

		Jump_ApplyMsetPairs(cleaned);

		if (end == text.size())
			break;
	}

	Jump_Log("applied msets from %s", path.string().c_str());
}

// Gravity is a server cvar, so it has to be pushed there to take effect. Always
// pushed, even when the map doesn't ask for it, or the previous map's value
// carries over.
static void Jump_PushGravityCvar()
{
	gi.cvar_set("sv_gravity", G_Fmt("{}", jump_mset.gravity_set ? jump_mset.gravity : 800).data());
}

// Deferred because SP_worldspawn sets sv_gravity from the map's own `gravity`
// key (g_spawn.cpp:1552-1561) and worldspawn spawns *after* Jump_InitLevel, so
// anything written during the mset load is overwritten a moment later. Latch the
// intent and push it on the next frame instead.
static bool jump_gravity_pending = false;

// One-shot on purpose: a map is still free to change gravity at runtime with
// target_gravity, and re-asserting the mset every frame would fight it.
void Jump_MsetFrame()
{
	if (!jump_gravity_pending)
		return;

	jump_gravity_pending = false;
	Jump_PushGravityCvar();
}

void Jump_LoadMsets(const char *entities)
{
	jump_mset = {};

	if (!Jump_Active())
		return;

	Jump_LoadWorldspawnMsets(entities);
	Jump_LoadMsetFile(jump_level.mapname);

	jump_gravity_pending = true;
}

bool Jump_FastTeleport()
{
	return Jump_Active() && jump_mset.fasttele;
}

// A weapon the map has enabled is an ordinary pickup rather than the finish.
bool Jump_IsUsableWeapon(item_id_t id)
{
	switch (id)
	{
	case IT_WEAPON_RLAUNCHER:
		return jump_mset.weapon_rocket;
	case IT_WEAPON_GLAUNCHER:
		return jump_mset.weapon_grenadelauncher;
	case IT_WEAPON_HYPERBLASTER:
		return jump_mset.weapon_hyperblaster;
	case IT_WEAPON_BFG:
		return jump_mset.weapon_bfg;
	default:
		return false;
	}
}

// ---------------------------------------------------------------------------
// `sv jump_mset` - authoring the settings the corpus never shipped
//
// Classic q2jump kept msets on the server rather than in the BSP, so almost no
// map in the wild carries a worldspawn mset. Without one every weapon ends the
// run, which is wrong on any map that hands out a rocket launcher as a tool.
// Tuning live and writing the result out beats editing cfg files blind.
//
// This hangs off ServerCommand rather than the client command dispatch because
// `sv` is console/rcon only: there is no privilege model on client commands, and
// these settings decide whether a run counts.
// ---------------------------------------------------------------------------

// Current values in the same `key value` form Jump_LoadMsetFile parses, so a
// listing and a saved file always agree.
static std::vector<std::pair<std::string, std::string>> Jump_MsetPairs()
{
	const auto flag = [](bool v) -> std::string { return v ? "1" : "0"; };

	return {
		{ "gravity", std::to_string(jump_mset.gravity) },
		{ "checkpoint_total", std::to_string(jump_mset.checkpoint_total) },
		{ "damage", flag(jump_mset.damage) },
		{ "fasttele", flag(jump_mset.fasttele) },
		{ "rocket", flag(jump_mset.weapon_rocket) },
		{ "grenadelauncher", flag(jump_mset.weapon_grenadelauncher) },
		{ "hyperblaster", flag(jump_mset.weapon_hyperblaster) },
		{ "bfg", flag(jump_mset.weapon_bfg) },
	};
}

static void Jump_MsetList()
{
	gi.Com_PrintFmt("msets for {}:\n", jump_level.mapname);

	for (const auto &pair : Jump_MsetPairs())
		gi.Com_PrintFmt("  {} {}\n", pair.first, pair.second);

	gi.Com_PrintFmt("checkpoints required: {}\n", Jump_CheckpointTotal());
}

// The player-facing half of the listing above. Read-only on purpose: setting
// stays on `sv jump_mset` for the reason in the block comment above, but a
// player who thinks a mset is broken needs to be able to see its value.
//
// `show_server_hint` is set when they typed `mset`, the command the upstream
// mods have and this port does not, so the reply says where settings come from
// instead of the engine's "invalid game command".
void Jump_CmdMsets(edict_t *ent, bool show_server_hint)
{
	std::string out = G_Fmt("Map settings for {}:\n", jump_level.mapname).data();

	for (const auto &pair : Jump_MsetPairs())
		out += G_Fmt("  {} {}\n", pair.first, pair.second).data();

	out += G_Fmt("checkpoints required: {}\n", Jump_CheckpointTotal()).data();

	if (show_server_hint)
		out += "These are set by the server, not by players: sv jump_mset <key> <value>\n";

	gi.Client_Print(ent, PRINT_HIGH, out.c_str());
}

static void Jump_MsetSave()
{
	const std::filesystem::path path = Jump_MsetPath(jump_level.mapname);

	std::string out = G_Fmt("# msets for {}\n", jump_level.mapname).data();

	for (const auto &pair : Jump_MsetPairs())
		out += G_Fmt("{} {}\n", pair.first, pair.second).data();

	if (Jump_WriteFileAtomic(path, out))
		gi.Com_PrintFmt("wrote {}\n", path.string());
	else
		gi.Com_PrintFmt("could not write {}\n", path.string());
}

static void Jump_MsetSet(const char *key, const char *value)
{
	switch (Jump_ApplyMset(key, value))
	{
	case jump_mset_result_t::unknown_key:
		gi.Com_PrintFmt("unknown mset '{}'\n", key);
		return;

	case jump_mset_result_t::bad_value:
		gi.Com_PrintFmt("bad value '{}' for mset '{}'\n", value, key);
		return;

	case jump_mset_result_t::ok:
		break;
	}

	// Mirror the two side effects a level load performs, so a live change behaves
	// the same as one read from the file.
	Jump_PushGravityCvar();
	Jump_InvalidateCheckpointTotal();

	gi.Com_PrintFmt("{} {}\n", key, value);
}

bool Jump_ServerCommand()
{
	if (Q_strcasecmp(gi.argv(1), "jump_mset") != 0)
		return false;

	if (!Jump_Active())
	{
		gi.Com_PrintFmt("jump mode is not active\n");
		return true;
	}

	const int argc = gi.argc();

	if (argc < 3)
	{
		Jump_MsetList();
		gi.Com_PrintFmt("usage: sv jump_mset [<key> <value> | save | reload]\n");
		return true;
	}

	const char *key = gi.argv(2);

	if (Q_strcasecmp(key, "save") == 0)
	{
		Jump_MsetSave();
		return true;
	}

	if (Q_strcasecmp(key, "reload") == 0)
	{
		// Re-reads the file over the current values. Keys the file omits keep
		// whatever they hold now, so this is "pick up my edits", not a full
		// reset - restart the map for that.
		Jump_LoadMsetFile(jump_level.mapname);
		Jump_PushGravityCvar();
		Jump_InvalidateCheckpointTotal();
		Jump_MsetList();
		return true;
	}

	if (argc < 4)
	{
		gi.Com_PrintFmt("usage: sv jump_mset <key> <value>\n");
		return true;
	}

	Jump_MsetSet(key, gi.argv(3));

	return true;
}
