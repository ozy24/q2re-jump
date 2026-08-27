// [Jump] Map records: JSON persistence and the points/ranking queries.
//
// One file per map under <data>/maptimes/<map>.json, holding one entry per
// player (their personal best). Rankings are derived by scanning those files
// on demand, so there is only ever one source of truth.

#include "../g_local.h"
#include "jump_local.h"

#include <json/json.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>

// Loaded records for the current map.
static jump::map_records_t jump_records;
static bool				   jump_records_loaded = false;

// A personal best is rare enough to write on the spot, but attempts change
// every few seconds per player - a write each would be a full file rewrite per
// restart. So counters accumulate in memory and Jump_RecordsFrame flushes them.
static bool	   jump_records_dirty = false;
static int64_t jump_records_flush_ms = 0;

// Wall clock, not level.time: that restarts at zero on every map load, so a
// stamp set late in one map would block every flush on the next until its clock
// caught up. Jump_NowMs is steady_clock and monotonic across maps.
constexpr int64_t JUMP_RECORDS_FLUSH_INTERVAL_MS = 15000;

// Answers to Jump_MapHasTimes for maps other than the one loaded, so paging
// through the vote menu parses each map's file at most once per level rather
// than twelve of them on every menu refresh. Cleared by Jump_LoadRecords.
static std::unordered_map<std::string, bool> jump_map_has_times;

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

// backfilled, when given, reports that a schema-1 file was migrated and so has
// changes worth writing back. An out-parameter rather than marking the table
// dirty in here, because Jump_PlayerTotals parses every map on the disk into
// throwaway tables and none of those should schedule a write.
static bool Jump_ParseRecords(const std::string &text, const char *mapname, jump::map_records_t &out,
							  bool *backfilled = nullptr)
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

	// jsoncpp is built with JSON_USE_EXCEPTION, so every accessor below throws
	// on a type it did not expect - asInt() on a string, asInt() on a value past
	// INT32_MAX, get() on an array element that is not an object. Unhandled,
	// that unwinds out of the game DLL during SpawnEntities and takes the server
	// down at map load. Worse, Jump_PlayerTotals parses EVERY file on the disk,
	// so one hand-edited file would break `playertimes` for everyone rather
	// than just its own map.
	//
	// Failing the parse routes the file into the jump_records_loaded guard,
	// which already refuses to write over a file it could not read - so a
	// damaged file is preserved for inspection rather than destroyed.
	try
	{
		const int version = root.get("version", 0).asInt();

		if (version > jump::map_records_t::SCHEMA_VERSION)
		{
			gi.Com_PrintFmt("[jump] {} was written by a newer version (schema {}); refusing to load\n", mapname, version);
			return false;
		}

		out.map = root.get("map", mapname).asString();
		out.times.clear();
		out.players.clear();

		// Note the shape: a missing or non-array `times` must NOT return early.
		// Doing so would report success with the players table unread, and the
		// next save would then write it back empty - destroying the counters on a
		// file the corrupt-file guard cannot help with, because the parse
		// succeeded.
		const Json::Value &times = root["times"];

		if (times.isArray())
		{
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
		}

		const Json::Value &stats = root["players"];

		if (stats.isArray())
		{
			for (const auto &entry : stats)
			{
				jump::player_stats_t row;

				row.id = entry.get("id", "").asString();
				row.name = entry.get("name", "").asString();

				// Clamped rather than trusted: a hand-edited negative would break
				// the attempts >= completions >= 0 invariant everything reading
				// these assumes.
				row.attempts = std::max(0, entry.get("attempts", 0).asInt());
				row.completions = std::max(0, entry.get("completions", 0).asInt());

				if (row.id.empty())
					continue; // same policy as times: drop the row, keep the file

				out.players.push_back(row);
			}
		}

		// Triggered off the parsed shape rather than off isMember, which would also
		// be true for a `"players": null` or `"players": {}` - and would then
		// suppress the migration and let the next flush write the table back empty,
		// which is the exact failure the restructure above removes for `times`.
		//
		// A healthy file behaves identically either way, since Jump_SerialiseRecords
		// always emits an array: missing key -> not an array -> migrate; a real
		// (even empty) array -> already ours -> leave alone. Testing the reference
		// is also immune to the operator[] insertion trap that testing isMember
		// here would walk straight into, since root["players"] above has by now
		// created the member on any file that lacked it.
		if (!stats.isArray() && out.BackfillFromTimes() && backfilled)
			*backfilled = true;

		return true;
	}
	catch (const Json::Exception &e)
	{
		gi.Com_PrintFmt("[jump] {} has a value of the wrong type ({}); refusing to load\n", mapname, e.what());
		return false;
	}
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

	Json::Value players(Json::arrayValue);

	for (const auto &stats : records.players)
	{
		Json::Value entry;

		entry["id"] = stats.id;
		entry["name"] = stats.name;
		entry["attempts"] = stats.attempts;
		entry["completions"] = stats.completions;

		players.append(entry);
	}

	// Always written, even empty: its presence is what tells the next load that
	// the schema-1 backfill has already run.
	root["players"] = players;

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

	// A level change is the one point where another map's file can have changed
	// underneath the cache without this process seeing it - a second server
	// sharing the data directory, or the map being left as it is written out.
	jump_map_has_times.clear();

	// Above the Jump_Active() gate below, not under it: the outgoing map has
	// already been flushed by Jump_InitLevel, and a level where the mod is off
	// would otherwise leave stale dirty state standing for the next one that
	// has it on.
	jump_records_dirty = false;

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

	bool backfilled = false;

	if (!Jump_ParseRecords(text, jump_level.mapname, jump_records, &backfilled))
	{
		// Leave the in-memory table empty and refuse to save over the file,
		// so a corrupt or newer-schema file is never silently destroyed.
		jump_records = {};
		jump_records.map = jump_level.mapname;
		jump_records_loaded = false;
		return;
	}

	jump_records_loaded = true;

	// A migrated schema-1 file only exists in memory until something writes it,
	// so schedule that rather than waiting for a player to do something.
	if (backfilled)
	{
		jump_records_dirty = true;
		Jump_Log("backfilled %d player counter row(s) for %s", (int) jump_records.players.size(),
				 jump_level.mapname);
	}

	Jump_Log("loaded %d record(s) for %s", (int) jump_records.times.size(), jump_level.mapname);
}

void Jump_MarkRecordsDirty()
{
	jump_records_dirty = true;
}

bool Jump_RecordsDirty()
{
	return jump_records_dirty;
}

// Make the loaded table unwritable until the next Jump_LoadRecords. Used across
// the level-change window, where the table and the map name briefly disagree
// about which map they belong to.
void Jump_InvalidateRecords()
{
	jump_records_loaded = false;
}

void Jump_SaveRecords()
{
	if (!jump_records_loaded)
		return; // don't overwrite a file we failed to read

	// Only on a successful write. Clearing regardless would throw away every
	// count since the last flush the moment a write failed - a disk full or a
	// locked temp file used to cost one personal best, and with batching it
	// would cost the lot.
	if (Jump_WriteFileAtomic(Jump_MapTimesPath(jump_level.mapname), Jump_SerialiseRecords(jump_records)))
		jump_records_dirty = false;
}

// Batched writes for the counters. Called once per server frame; the personal
// best path still writes immediately, so nothing a player earns waits on this.
void Jump_RecordsFrame()
{
	if (!jump_records_dirty)
		return;

	const int64_t now = Jump_NowMs();

	if (now < jump_records_flush_ms)
		return;

	jump_records_flush_ms = now + JUMP_RECORDS_FLUSH_INTERVAL_MS;

	Jump_SaveRecords();
}

// ---------------------------------------------------------------------------
// Attempt and completion counters
// ---------------------------------------------------------------------------
//
// Counted at the START of a run rather than at its abandonment. There are
// fifteen ways a run can end without finishing - death, kill, recall, team
// change, spectate, disconnect, map change, the menu's Restart - and counting
// the start means none of them need to know this exists, and a sixteenth
// cannot be missed. It also makes attempts >= completions true by construction.
//
// Ranked only, matching every other "not recorded" gate in the mod. That also
// makes the Practice store-recall resume a non-issue, since it can only happen
// on the team that does not count.

static jump::player_stats_t *Jump_StatsForClient(edict_t *ent)
{
	if (!Jump_Active() || !jump_records_loaded)
		return nullptr;

	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc || jc->team != jump_team_t::ranked)
		return nullptr;

	return &jump_records.StatsFor(Jump_PlayerId(ent), Jump_DisplayName(ent));
}

void Jump_CountAttempt(edict_t *ent)
{
	jump::player_stats_t *stats = Jump_StatsForClient(ent);

	if (!stats)
		return;

	stats->attempts++;
	Jump_MarkRecordsDirty();
}

void Jump_CountCompletion(edict_t *ent)
{
	jump::player_stats_t *stats = Jump_StatsForClient(ent);

	if (!stats)
		return;

	stats->completions++;
	Jump_MarkRecordsDirty();
}

// ---------------------------------------------------------------------------
// Queries used by the finish path and the console commands
// ---------------------------------------------------------------------------

int64_t Jump_PersonalBest(edict_t *ent)
{
	return jump_records.TimeOf(Jump_PlayerId(ent));
}

// True when at least one player has FINISHED this map, which is not the same
// question as whether the file exists: Jump_CountAttempt marks the table dirty
// too, so a map somebody only ever started has a file on disk carrying player
// counters and an empty times array. Deciding the marker on existence alone
// would star every map anyone had ever loaded.
bool Jump_MapHasTimes(const char *mapname)
{
	if (!Jump_Active() || !mapname || !mapname[0])
		return false;

	// The current map answers from the table that is already in memory, which
	// is both free and self-updating: the first completion of the session lights
	// the marker up with no cache entry to go stale.
	if (!Q_strcasecmp(mapname, jump_level.mapname))
		return !jump_records.times.empty();

	const std::string key = mapname;
	const auto		  cached = jump_map_has_times.find(key);

	if (cached != jump_map_has_times.end())
		return cached->second;

	bool		has_times = false;
	std::string text;

	if (Jump_ReadFile(Jump_MapTimesPath(mapname), text))
	{
		// A throwaway table, and deliberately no backfilled out-parameter: the
		// same rule Jump_PlayerTotals works under below. Nothing read on behalf
		// of another map may mark the loaded map's records dirty, and a
		// schema-1 file is migrated when that map is actually played.
		jump::map_records_t records;

		if (Jump_ParseRecords(text, mapname, records))
			has_times = !records.times.empty();
	}

	jump_map_has_times[key] = has_times;

	return has_times;
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

	// Marked dirty as well as written immediately. A personal best is worth a
	// write of its own rather than waiting on the batch, but if that write
	// fails the flag is what gets it retried on the next flush and at
	// shutdown - which is more than it used to get.
	if (rank > 0)
	{
		Jump_MarkRecordsDirty();
		Jump_SaveRecords();
	}

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
