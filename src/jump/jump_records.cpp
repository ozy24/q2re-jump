// [Jump] Map records: JSON persistence and the points/ranking queries.
//
// One file per map under <data>/maptimes/<map>.json, holding one entry per
// player (their personal best). Rankings are derived by scanning those files
// on demand, so there is only ever one source of truth.

#include "../g_local.h"
#include "jump_local.h"

#include <json/json.h>

#include <ctime>
#include <filesystem>
#include <map>

// Loaded records for the current map.
static jump::map_records_t jump_records;
static bool				   jump_records_loaded = false;

const jump::map_records_t &Jump_Records()
{
	return jump_records;
}

static std::string Jump_TimestampUtc()
{
	const std::time_t now = std::time(nullptr);
	std::tm			  tm {};

	gmtime_s(&tm, &now);

	char buf[32];
	strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);

	return buf;
}

// pers.netname is NOT a display name. ClientUserinfoChanged overwrites it with
// an encoded lobby token ("##P0" for slot 0) that the client decodes, and it is
// only meaningful as an argument to the Loc* print imports. Anything the mod
// writes to a file, or bakes into a plain string, has to read the real name out
// of the saved userinfo instead.
const char *Jump_DisplayName(edict_t *ent)
{
	// Rotating buffers so two names can appear in one expression without the
	// second call overwriting the first, the same trick G_Fmt uses.
	static char	  names[4][MAX_INFO_VALUE];
	static size_t next = 0;

	char *name = names[next];
	next = (next + 1) % std::size(names);

	name[0] = '\0';

	if (ent && ent->client)
		gi.Info_ValueForKey(ent->client->pers.userinfo, "name", name, MAX_INFO_VALUE);

	if (!name[0])
		Q_strlcpy(name, "player", MAX_INFO_VALUE);

	return name;
}

const char *Jump_PlayerId(edict_t *ent)
{
	// The engine supplies a stable per-account id.
	if (ent->client->pers.social_id[0])
		return ent->client->pers.social_id;

	// Fall back to the display name, never to netname: that token is a client
	// slot number, so every host would key their records as "##P0".
	return Jump_DisplayName(ent);
}

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------

static bool Jump_ParseRecords(const std::string &text, const char *mapname, jump::map_records_t &out)
{
	Json::Value				 root;
	Json::CharReaderBuilder	 builder;
	std::string				 errors;
	std::unique_ptr<Json::CharReader> reader(builder.newCharReader());

	if (!reader->parse(text.data(), text.data() + text.size(), &root, &errors))
	{
		gi.Com_PrintFmt("[jump] {} is not valid JSON: {}\n", mapname, errors);
		return false;
	}

	const int version = root.get("version", 0).asInt();

	if (version > jump::map_records_t::SCHEMA_VERSION)
	{
		gi.Com_PrintFmt("[jump] {} was written by a newer version (schema {}); refusing to load\n", mapname, version);
		return false;
	}

	out.map = root.get("map", mapname).asString();
	out.times.clear();

	const Json::Value &times = root["times"];

	if (!times.isArray())
		return true;

	for (const auto &entry : times)
	{
		jump::record_t rec;

		rec.id = entry.get("id", "").asString();
		rec.name = entry.get("name", "").asString();
		rec.time_ms = entry.get("time_ms", 0).asInt64();
		rec.date = entry.get("date", "").asString();

		if (rec.id.empty() || rec.time_ms <= 0)
			continue; // skip malformed rows rather than failing the whole file

		out.times.push_back(rec);
	}

	out.Sort();

	return true;
}

static std::string Jump_SerialiseRecords(const jump::map_records_t &records)
{
	Json::Value root;

	root["version"] = jump::map_records_t::SCHEMA_VERSION;
	root["map"] = records.map;

	Json::Value times(Json::arrayValue);

	for (const auto &rec : records.times)
	{
		Json::Value entry;

		entry["id"] = rec.id;
		entry["name"] = rec.name;
		entry["time_ms"] = (Json::Int64) rec.time_ms;
		entry["date"] = rec.date;

		times.append(entry);
	}

	root["times"] = times;

	Json::StreamWriterBuilder writer;
	writer["indentation"] = "  ";

	return Json::writeString(writer, root);
}

// ---------------------------------------------------------------------------
// Load / save
// ---------------------------------------------------------------------------

void Jump_LoadRecords()
{
	jump_records = {};
	jump_records.map = jump_level.mapname;
	jump_records_loaded = false;

	if (!Jump_Active())
		return;

	const std::filesystem::path path = Jump_MapTimesPath(jump_level.mapname);

	std::string text;

	if (!Jump_ReadFile(path, text))
	{
		// No file yet is the normal case for a map nobody has finished.
		jump_records_loaded = true;
		return;
	}

	if (!Jump_ParseRecords(text, jump_level.mapname, jump_records))
	{
		// Leave the in-memory table empty and refuse to save over the file,
		// so a corrupt or newer-schema file is never silently destroyed.
		jump_records = {};
		jump_records.map = jump_level.mapname;
		jump_records_loaded = false;
		return;
	}

	jump_records_loaded = true;
	Jump_Log("loaded %d record(s) for %s", (int) jump_records.times.size(), jump_level.mapname);
}

void Jump_SaveRecords()
{
	if (!jump_records_loaded)
		return; // don't overwrite a file we failed to read

	Jump_WriteFileAtomic(Jump_MapTimesPath(jump_level.mapname), Jump_SerialiseRecords(jump_records));
}

// ---------------------------------------------------------------------------
// Queries used by the finish path and the console commands
// ---------------------------------------------------------------------------

int64_t Jump_PersonalBest(edict_t *ent)
{
	return jump_records.TimeOf(Jump_PlayerId(ent));
}

int64_t Jump_MapRecord()
{
	return jump_records.times.empty() ? 0 : jump_records.times.front().time_ms;
}

int Jump_SubmitTime(edict_t *ent, int64_t time_ms)
{
	jump::record_t rec;

	rec.id = Jump_PlayerId(ent);
	rec.name = Jump_DisplayName(ent);
	rec.time_ms = time_ms;
	rec.date = Jump_TimestampUtc();

	Jump_Log("submit: id=%s name=%s time=%lld", rec.id.c_str(), rec.name.c_str(), (long long) time_ms);

	const int rank = jump_records.Submit(rec);

	if (rank > 0)
		Jump_SaveRecords();

	return rank;
}

// Sum a player's points across every map that has a records file.
void Jump_PlayerTotals(const std::string &id, int &points, int &completions, int &firsts)
{
	points = completions = firsts = 0;

	const std::filesystem::path dir = Jump_DataRoot() / "maptimes";

	std::error_code ec;

	if (!std::filesystem::exists(dir, ec))
		return;

	for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
	{
		if (ec)
			break;

		if (!entry.is_regular_file() || entry.path().extension() != ".json")
			continue;

		std::string text;

		if (!Jump_ReadFile(entry.path(), text))
			continue;

		jump::map_records_t records;

		if (!Jump_ParseRecords(text, entry.path().stem().string().c_str(), records))
			continue;

		const int rank = records.RankOf(id);

		if (rank <= 0)
			continue;

		completions++;
		points += jump::PointsForRank(rank);

		if (rank == 1)
			firsts++;
	}
}
