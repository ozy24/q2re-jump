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
#include <vector>

jump_mset_t jump_mset;

// ---------------------------------------------------------------------------
// Applying values
// ---------------------------------------------------------------------------

static bool Jump_ParseBool(const std::string &value)
{
	return atoi(value.c_str()) != 0;
}

static void Jump_ApplyMset(const std::string &key, const std::string &value)
{
	if (key == "gravity")
	{
		jump_mset.gravity = atoi(value.c_str());
		jump_mset.gravity_set = true;
	}
	// The code in Q2JumpRefresh calls this "checkpoints" while its own docs
	// and the classic mod call it "checkpoint_total"; accept either.
	else if (key == "checkpoints" || key == "checkpoint_total")
	{
		jump_mset.checkpoint_total = atoi(value.c_str());
		jump_mset.checkpoint_total_set = true;
	}
	else if (key == "damage")
		jump_mset.damage = Jump_ParseBool(value);
	else if (key == "fasttele")
		jump_mset.fasttele = Jump_ParseBool(value);
	else if (key == "rocket")
		jump_mset.weapon_rocket = Jump_ParseBool(value);
	else if (key == "grenadelauncher")
		jump_mset.weapon_grenadelauncher = Jump_ParseBool(value);
	else if (key == "hyperblaster")
		jump_mset.weapon_hyperblaster = Jump_ParseBool(value);
	else if (key == "bfg")
		jump_mset.weapon_bfg = Jump_ParseBool(value);
	else
	{
		Jump_Log("unknown mset '%s'", key.c_str());
		return;
	}

	Jump_Log("mset %s = %s", key.c_str(), value.c_str());
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
	const std::filesystem::path path = Jump_DataRoot() / "mset" / (jump::SafeName(mapname) + ".cfg");

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

void Jump_LoadMsets(const char *entities)
{
	jump_mset = {};

	if (!Jump_Active())
		return;

	Jump_LoadWorldspawnMsets(entities);
	Jump_LoadMsetFile(jump_level.mapname);

	// Gravity is a server cvar, so it has to be re-set on every level even
	// when the map doesn't ask for it, or the previous map's value carries
	// over.
	gi.cvar_set("sv_gravity", G_Fmt("{}", jump_mset.gravity_set ? jump_mset.gravity : 800).data());
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
