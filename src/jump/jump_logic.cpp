// [Jump] Engine-free logic for the jump mod. See jump_logic.h.

#include "jump_logic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace jump
{

std::string FormatTime(int64_t ms)
{
	if (ms < 0)
		ms = 0;

	char buf[32];
	snprintf(buf, sizeof(buf), "%lld.%03lld", (long long) (ms / 1000), (long long) (ms % 1000));
	return buf;
}

std::string FormatDelta(int64_t delta_ms)
{
	const char *sign = delta_ms < 0 ? "-" : "+";
	int64_t		mag = delta_ms < 0 ? -delta_ms : delta_ms;

	char buf[32];
	snprintf(buf, sizeof(buf), "%s%lld.%03lld", sign, (long long) (mag / 1000), (long long) (mag % 1000));
	return buf;
}

void store_ring_t::Clear()
{
	next = 0;
	count = 0;
}

void store_ring_t::Push(const store_slot_t &slot)
{
	slots[next] = slot;
	next = (next + 1) % MAX_STORES;

	if (count < MAX_STORES)
		count++;
}

const store_slot_t *store_ring_t::Get(int prev) const
{
	if (count == 0)
		return nullptr;

	if (prev < 1)
		prev = 1;
	if (prev > count)
		prev = count;

	// next points one past the most recent write, so step back from there.
	int index = (next - prev + MAX_STORES * 2) % MAX_STORES;
	return &slots[index];
}

int PointsForRank(int rank)
{
	static const int points[MAX_HIGHSCORES] = { 25, 20, 16, 13, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1 };

	if (rank < 1 || rank > MAX_HIGHSCORES)
		return 0;

	return points[rank - 1];
}

int map_records_t::Submit(const record_t &rec)
{
	for (auto &existing : times)
	{
		if (existing.id != rec.id)
			continue;

		if (rec.time_ms >= existing.time_ms)
			return 0; // not an improvement

		existing = rec;
		Sort();
		return RankOf(rec.id);
	}

	times.push_back(rec);
	Sort();
	return RankOf(rec.id);
}

void map_records_t::Sort()
{
	// Plain insertion sort: the table is at most a few hundred entries and
	// this keeps ties in insertion order, so an equal time never displaces
	// the player who set it first.
	for (size_t i = 1; i < times.size(); i++)
	{
		record_t key = times[i];
		size_t	 j = i;

		while (j > 0 && times[j - 1].time_ms > key.time_ms)
		{
			times[j] = times[j - 1];
			j--;
		}

		times[j] = key;
	}
}

int map_records_t::RankOf(const std::string &id) const
{
	for (size_t i = 0; i < times.size(); i++)
		if (times[i].id == id)
			return (int) i + 1;

	return 0;
}

int64_t map_records_t::TimeOf(const std::string &id) const
{
	for (const auto &rec : times)
		if (rec.id == id)
			return rec.time_ms;

	return 0;
}

int map_records_t::PointsOf(const std::string &id) const
{
	return PointsForRank(RankOf(id));
}

player_stats_t &map_records_t::StatsFor(const std::string &id, const std::string &name)
{
	for (auto &stats : players)
	{
		if (stats.id != id)
			continue;

		// A player can rename between runs, and the row outlives the session
		// that created it, so the newest name wins - same as record_t, which
		// carries the name as of the run that set it.
		if (!name.empty())
			stats.name = name;

		return stats;
	}

	player_stats_t created;
	created.id = id;
	created.name = name;

	players.push_back(created);

	return players.back();
}

const player_stats_t *map_records_t::StatsOf(const std::string &id) const
{
	for (const auto &stats : players)
		if (stats.id == id)
			return &stats;

	return nullptr;
}

bool map_records_t::BackfillFromTimes()
{
	bool created = false;

	for (const auto &rec : times)
	{
		if (StatsOf(rec.id))
			continue; // already counted; leave the real figures alone

		player_stats_t seeded;
		seeded.id = rec.id;
		seeded.name = rec.name;
		seeded.attempts = 1;
		seeded.completions = 1;

		players.push_back(seeded);
		created = true;
	}

	return created;
}

// ---------------------------------------------------------------------------
// Replays
// ---------------------------------------------------------------------------

void replay_recorder_t::Reset()
{
	frames.clear();
	overflowed = false;
}

bool replay_recorder_t::Sample(int64_t elapsed_ms, const replay_frame_t &frame)
{
	if (overflowed || elapsed_ms < 0)
		return false;

	// Which 40 Hz slot this sample belongs to. frames.size() is exactly the
	// count already emitted, so comparing against it tells us whether this
	// slot is already filled (too soon - reject) or whether we have fallen
	// behind it (pad up to it first).
	const int64_t target_index = elapsed_ms / REPLAY_FRAME_MS;

	if (target_index < (int64_t) frames.size())
		return false;

	// Polling ran coarser than 40 Hz (a slow server tick, a stalled command):
	// pad the gap by holding the most recent known frame rather than
	// skipping slots outright, which would shift every later index and
	// silently speed the whole recording up on playback.
	while ((int64_t) frames.size() < target_index)
	{
		if ((int) frames.size() >= REPLAY_MAX_FRAMES)
		{
			overflowed = true;
			return false;
		}

		frames.push_back(frames.empty() ? frame : frames.back());
	}

	if ((int) frames.size() >= REPLAY_MAX_FRAMES)
	{
		overflowed = true;
		return false;
	}

	frames.push_back(frame);
	return true;
}

int64_t replay_t::DurationMs() const
{
	if (frames.size() < 2)
		return 0;

	return (int64_t) (frames.size() - 1) * REPLAY_FRAME_MS;
}

static void WriteU16LE(std::string &out, uint16_t v)
{
	out.push_back((char) (uint8_t) (v & 0xFF));
	out.push_back((char) (uint8_t) ((v >> 8) & 0xFF));
}

static void WriteU32LE(std::string &out, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		out.push_back((char) (uint8_t) ((v >> (i * 8)) & 0xFF));
}

static void WriteI32LE(std::string &out, int32_t v)
{
	WriteU32LE(out, (uint32_t) v);
}

static bool ReadU16LE(const std::string &bytes, size_t &pos, uint16_t &out)
{
	if (pos + 2 > bytes.size())
		return false;

	out = (uint16_t) (uint8_t) bytes[pos] | ((uint16_t) (uint8_t) bytes[pos + 1] << 8);
	pos += 2;
	return true;
}

static bool ReadU32LE(const std::string &bytes, size_t &pos, uint32_t &out)
{
	if (pos + 4 > bytes.size())
		return false;

	out = 0;
	for (int i = 0; i < 4; i++)
		out |= (uint32_t) (uint8_t) bytes[pos + i] << (i * 8);

	pos += 4;
	return true;
}

static bool ReadI32LE(const std::string &bytes, size_t &pos, int32_t &out)
{
	uint32_t raw;

	if (!ReadU32LE(bytes, pos, raw))
		return false;

	out = (int32_t) raw;
	return true;
}

static void WriteVarint(std::string &out, uint64_t value)
{
	while (value >= 0x80)
	{
		out.push_back((char) (uint8_t) (0x80 | (value & 0x7F)));
		value >>= 7;
	}

	out.push_back((char) (uint8_t) value);
}

// LEB128-style, capped at 10 bytes (enough for a full 64-bit value) so a
// malformed stream of continuation bits cannot spin forever.
static bool ReadVarint(const std::string &bytes, size_t &pos, uint64_t &out)
{
	out = 0;

	for (int shift = 0; shift < 70; shift += 7)
	{
		if (pos >= bytes.size())
			return false;

		const uint8_t b = (uint8_t) bytes[pos++];

		out |= (uint64_t) (b & 0x7F) << shift;

		if (!(b & 0x80))
			return true;
	}

	return false; // too long to be a real value
}

static uint64_t ZigZagEncode(int64_t v)
{
	return (uint64_t) ((v << 1) ^ (v >> 63));
}

static int64_t ZigZagDecode(uint64_t v)
{
	return (int64_t) (v >> 1) ^ -(int64_t) (v & 1);
}

static constexpr char REPLAY_MAGIC[4] = { 'J', 'R', 'E', 'P' };

std::string EncodeReplay(const replay_t &replay)
{
	std::string out;
	out.reserve(replay.frames.size() * 14 + 16);

	out.append(REPLAY_MAGIC, 4);
	out.push_back((char) (uint8_t) replay_t::SCHEMA_VERSION);
	out.push_back((char) (uint8_t) (replay.sample_hz & 0xFF));
	WriteU32LE(out, (uint32_t) replay.frames.size());

	replay_frame_t prev {};

	for (size_t i = 0; i < replay.frames.size(); i++)
	{
		const replay_frame_t &f = replay.frames[i];
		const bool			   keyframe = (i % REPLAY_KEYFRAME_INTERVAL) == 0;

		if (keyframe)
		{
			for (int a = 0; a < 3; a++)
				WriteI32LE(out, f.origin_ticks[a]);
			for (int a = 0; a < 3; a++)
				WriteI32LE(out, f.velocity[a]);

			WriteU16LE(out, f.yaw_ticks);
			WriteU16LE(out, f.pitch_ticks);
		}
		else
		{
			for (int a = 0; a < 3; a++)
				WriteVarint(out, ZigZagEncode((int64_t) f.origin_ticks[a] - (int64_t) prev.origin_ticks[a]));
			for (int a = 0; a < 3; a++)
				WriteVarint(out, ZigZagEncode((int64_t) f.velocity[a] - (int64_t) prev.velocity[a]));

			// uint16 subtraction cast to int16 is exact two's-complement
			// wraparound, so this is correct across the 65535->0 seam without
			// special-casing it.
			WriteVarint(out, ZigZagEncode((int64_t) (int16_t) (f.yaw_ticks - prev.yaw_ticks)));
			WriteVarint(out, ZigZagEncode((int64_t) (int16_t) (f.pitch_ticks - prev.pitch_ticks)));
		}

		out.push_back((char) f.buttons);
		prev = f;
	}

	return out;
}

bool DecodeReplay(const std::string &bytes, replay_t &out)
{
	out = replay_t {};

	if (bytes.size() < 10 || bytes.compare(0, 4, REPLAY_MAGIC, 4) != 0)
		return false;

	size_t pos = 4;

	const uint8_t version = (uint8_t) bytes[pos++];

	if (version > replay_t::SCHEMA_VERSION)
		return false; // written by a newer build; refuse rather than misread it

	out.sample_hz = (uint8_t) bytes[pos++];

	uint32_t count;

	if (!ReadU32LE(bytes, pos, count))
		return false;

	// Refuse a file claiming more frames than the recorder could ever have
	// produced, before reserving space for it.
	if (count > (uint32_t) REPLAY_MAX_FRAMES)
		return false;

	out.frames.reserve(count);

	replay_frame_t prev {};

	for (uint32_t i = 0; i < count; i++)
	{
		replay_frame_t f {};
		const bool	   keyframe = (i % REPLAY_KEYFRAME_INTERVAL) == 0;

		if (keyframe)
		{
			for (int a = 0; a < 3; a++)
				if (!ReadI32LE(bytes, pos, f.origin_ticks[a]))
					return false;
			for (int a = 0; a < 3; a++)
				if (!ReadI32LE(bytes, pos, f.velocity[a]))
					return false;
			if (!ReadU16LE(bytes, pos, f.yaw_ticks))
				return false;
			if (!ReadU16LE(bytes, pos, f.pitch_ticks))
				return false;
		}
		else
		{
			uint64_t raw;

			for (int a = 0; a < 3; a++)
			{
				if (!ReadVarint(bytes, pos, raw))
					return false;
				f.origin_ticks[a] = (int32_t) (prev.origin_ticks[a] + ZigZagDecode(raw));
			}

			for (int a = 0; a < 3; a++)
			{
				if (!ReadVarint(bytes, pos, raw))
					return false;
				f.velocity[a] = (int32_t) (prev.velocity[a] + ZigZagDecode(raw));
			}

			if (!ReadVarint(bytes, pos, raw))
				return false;
			f.yaw_ticks = (uint16_t) (prev.yaw_ticks + (int16_t) ZigZagDecode(raw));

			if (!ReadVarint(bytes, pos, raw))
				return false;
			f.pitch_ticks = (uint16_t) (prev.pitch_ticks + (int16_t) ZigZagDecode(raw));
		}

		if (pos >= bytes.size())
			return false;

		f.buttons = (uint8_t) bytes[pos++];

		out.frames.push_back(f);
		prev = f;
	}

	return true;
}

static void ReplaySampleFromFrame(const replay_frame_t &f, replay_sample_t &out)
{
	for (int a = 0; a < 3; a++)
	{
		out.origin[a] = (float) f.origin_ticks[a] / 8.f;
		out.velocity[a] = (float) f.velocity[a];
	}

	out.yaw = (float) f.yaw_ticks * (360.f / 65536.f);
	out.pitch = (float) f.pitch_ticks * (360.f / 65536.f);
	out.buttons = f.buttons;
}

bool ReplaySampleAt(const replay_t &replay, int64_t elapsed_ms, replay_sample_t &out)
{
	if (replay.frames.empty() || elapsed_ms < 0)
		return false;

	const int64_t duration_ms = replay.DurationMs();

	if (elapsed_ms > duration_ms)
		return false;

	const size_t last = replay.frames.size() - 1;

	// Integer frame index and remainder, both exact - avoids a float boundary
	// case landing just short of (or past) the final frame.
	size_t i0 = (size_t) (elapsed_ms / REPLAY_FRAME_MS);

	if (i0 >= last)
	{
		ReplaySampleFromFrame(replay.frames[last], out);
		return true;
	}

	const int64_t rem_ms = elapsed_ms - (int64_t) i0 * REPLAY_FRAME_MS;
	const float	  t = (float) rem_ms / (float) REPLAY_FRAME_MS;

	const replay_frame_t &a = replay.frames[i0];
	const replay_frame_t &b = replay.frames[i0 + 1];

	for (int k = 0; k < 3; k++)
	{
		out.origin[k] = ((float) a.origin_ticks[k] + (float) (b.origin_ticks[k] - a.origin_ticks[k]) * t) / 8.f;
		out.velocity[k] = (float) a.velocity[k] + (float) (b.velocity[k] - a.velocity[k]) * t;
	}

	// Shortest-arc: the tick delta is taken as a signed 16-bit wraparound
	// before interpolating, so 359 degrees to 1 degree crosses through the
	// 360/0 seam rather than the long way through 180.
	const float yaw_delta = (float) (int16_t) (b.yaw_ticks - a.yaw_ticks);
	const float pitch_delta = (float) (int16_t) (b.pitch_ticks - a.pitch_ticks);

	out.yaw = ((float) a.yaw_ticks + yaw_delta * t) * (360.f / 65536.f);
	out.pitch = ((float) a.pitch_ticks + pitch_delta * t) * (360.f / 65536.f);
	out.buttons = a.buttons;

	return true;
}

// Rows sort into three bands before anything else is compared.
static int PlayerRowGroup(const player_row_t &row)
{
	if (row.spectator)
		return 2;

	return row.session_ms ? 0 : 1;
}

void SortPlayerRows(std::vector<player_row_t> &rows)
{
	std::stable_sort(rows.begin(), rows.end(), [](const player_row_t &a, const player_row_t &b) {
		const int ga = PlayerRowGroup(a);
		const int gb = PlayerRowGroup(b);

		if (ga != gb)
			return ga < gb;

		// Posted a time on this map: fastest first.
		if (ga == 0)
			return a.session_ms < b.session_ms;

		// Yet to post one: by all-time best, with "never finished it" last.
		if (ga == 1 && a.pb_ms != b.pb_ms)
		{
			if (!a.pb_ms || !b.pb_ms)
				return a.pb_ms != 0;

			return a.pb_ms < b.pb_ms;
		}

		// Spectators, and every remaining tie, keep collection order.
		return false;
	});
}

int VotesNeeded(int voters, float pass_fraction)
{
	if (voters <= 0)
		return 0;

	// + 0.999f rather than std::ceil to keep this identical to what the vote
	// code did inline before it moved here. The products involved are small
	// exact multiples, so an exact threshold never rounds up an extra ballot.
	return (int) (voters * pass_fraction + 0.999f);
}

vote_result_t ResolveVote(int yes, int no, int voters, int needed, bool expired)
{
	if (voters <= 0)
		return vote_result_t::pending;

	if (yes >= needed)
		return vote_result_t::passed;

	if (no > voters - needed || expired)
		return vote_result_t::failed;

	return vote_result_t::pending;
}

bool IsSafeMapToken(const std::string &token)
{
	constexpr size_t MAX_MAP_NAME = 64; // MAX_QPATH

	if (token.empty() || token.size() >= MAX_MAP_NAME)
		return false;

	for (unsigned char c : token)
	{
		if (c <= ' ' || c >= 0x7f)
			return false;

		// Quoting, shell and wildcard characters.
		if (strchr("\"'`;:*?<>|\\", c))
			return false;
	}

	// No directory traversal, and no empty or dot-only path segments.
	size_t start = 0;

	while (start <= token.size())
	{
		size_t end = token.find('/', start);

		if (end == std::string::npos)
			end = token.size();

		const std::string segment = token.substr(start, end - start);

		if (segment.empty() || segment == "." || segment == "..")
			return false;

		if (end == token.size())
			break;

		start = end + 1;
	}

	return true;
}

std::string SanitizeLayoutText(const std::string &text, size_t max_len)
{
	std::string out;
	out.reserve(text.size());

	bool pending_space = false;

	for (unsigned char c : text)
	{
		// Quote and backslash would terminate or escape the token; control and
		// high bytes render as junk in the layout font.
		const bool safe = c >= 0x20 && c < 0x7f && c != '"' && c != '\\';

		if (!safe || c == ' ')
		{
			pending_space = !out.empty();
			continue;
		}

		if (pending_space)
		{
			if (out.size() + 1 >= max_len)
				break;

			out.push_back(' ');
			pending_space = false;
		}

		if (out.size() >= max_len)
			break;

		out.push_back((char) c);
	}

	if (out.empty())
		return "?";

	return out;
}

std::string SafeName(const std::string &name)
{
	std::string out;
	out.reserve(name.size());

	for (char c : name)
	{
		if (c >= 'A' && c <= 'Z')
			c = (char) (c - 'A' + 'a');

		const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';

		out.push_back(ok ? c : '_');
	}

	// A component that starts with a dot can be "." or ".."; strip them so the
	// result can never refer to a parent directory.
	size_t first = out.find_first_not_of('.');
	if (first == std::string::npos)
		return "_";
	out.erase(0, first);

	if (out.empty())
		return "_";

	return out;
}

bool IsCheckpointBarrierTarget(const std::string &target)
{
	static const char prefix[] = "checkpoint";
	static const size_t prefix_len = sizeof(prefix) - 1;

	if (target.size() < prefix_len)
		return false;

	for (size_t i = 0; i < prefix_len; i++)
	{
		char c = target[i];

		if (c >= 'A' && c <= 'Z')
			c = (char) (c - 'A' + 'a');

		if (c != prefix[i])
			return false;
	}

	return true;
}

// Trimmed and lowercased, which is all either mset parser needs.
static std::string NormalizeMsetValue(const std::string &value)
{
	const size_t first = value.find_first_not_of(" \t\r\n");

	if (first == std::string::npos)
		return {};

	const size_t last = value.find_last_not_of(" \t\r\n");

	std::string out = value.substr(first, last - first + 1);

	for (char &c : out)
	{
		if (c >= 'A' && c <= 'Z')
			c = (char) (c - 'A' + 'a');
	}

	return out;
}

std::optional<bool> ParseMsetBool(const std::string &value)
{
	const std::string text = NormalizeMsetValue(value);

	if (text.empty())
		return std::nullopt;

	if (text == "1" || text == "on" || text == "true" || text == "yes")
		return true;

	if (text == "0" || text == "off" || text == "false" || text == "no")
		return false;

	// Anything else has to be a number to count, so "fasttele banana" is an
	// error while the "fasttele 2" a cfg file might hold still means on.
	if (const std::optional<int> number = ParseMsetInt(text))
		return *number != 0;

	return std::nullopt;
}

std::optional<int> ParseMsetInt(const std::string &value)
{
	const std::string text = NormalizeMsetValue(value);

	if (text.empty())
		return std::nullopt;

	size_t i = 0;

	if (text[i] == '+' || text[i] == '-')
		i++;

	if (i >= text.size())
		return std::nullopt;

	for (size_t j = i; j < text.size(); j++)
	{
		if (text[j] < '0' || text[j] > '9')
			return std::nullopt;
	}

	// strtol rather than atoi: atoi is undefined on overflow, and a gravity
	// value that wrapped would be worse than one clamped to the extreme.
	const long number = std::strtol(text.c_str(), nullptr, 10);

	if (number > INT32_MAX)
		return INT32_MAX;

	if (number < INT32_MIN)
		return INT32_MIN;

	return (int) number;
}

float HorizontalSpeed(float vel_x, float vel_y)
{
	return (float) std::sqrt((double) vel_x * (double) vel_x + (double) vel_y * (double) vel_y);
}

int SpeedStat(float vel_x, float vel_y)
{
	const double speed = std::sqrt((double) vel_x * (double) vel_x + (double) vel_y * (double) vel_y);

	// Written as a negated comparison so that NaN lands here rather than
	// reaching the cast, where it would be undefined.
	if (!(speed > 0.0))
		return 0;

	if (speed >= (double) SPEED_STAT_MAX)
		return SPEED_STAT_MAX;

	return (int) speed;
}

std::string FormatSpeed(float ups)
{
	char buf[16];
	snprintf(buf, sizeof(buf), "%d", SpeedStat(ups, 0.f));
	return buf;
}

std::string FormatSpeedDelta(float ups)
{
	const int magnitude = SpeedStat(ups, 0.f);

	// The sign follows the magnitude, not the input: a loss too small to show
	// as a digit would otherwise read "-0".
	char buf[16];
	snprintf(buf, sizeof(buf), "%s%d", (ups < 0.f && magnitude > 0) ? "-" : "+", magnitude);
	return buf;
}

void move_ring_t::Clear()
{
	next = 0;
	count = 0;
}

void move_ring_t::Push(const move_sample_t &sample)
{
	samples[next] = sample;
	next = (next + 1) % MOVE_SAMPLES;

	if (count < MOVE_SAMPLES)
		count++;
}

const move_sample_t *move_ring_t::Get(int prev) const
{
	if (count == 0)
		return nullptr;

	if (prev < 1)
		prev = 1;
	if (prev > count)
		prev = count;

	const int index = (next - prev + MOVE_SAMPLES * 2) % MOVE_SAMPLES;
	return &samples[index];
}

const move_sample_t *move_ring_t::AtAge(uint64_t now_ms, uint64_t age_ms) const
{
	// Signed, so that an age reaching back before the first sample yields no
	// match rather than wrapping around and returning the oldest one held.
	const int64_t target = (int64_t) now_ms - (int64_t) age_ms;

	for (int prev = 1; prev <= count; prev++)
	{
		const move_sample_t *sample = Get(prev);

		// A discontinuity is a wall, not a data point: whatever the player was
		// doing on the other side of a teleport or a recall says nothing about
		// what they are doing now.
		if (sample->discontinuity)
			return nullptr;

		if ((int64_t) sample->time_ms <= target)
			return sample;
	}

	return nullptr; // the history does not reach back that far
}

void speed_state_t::Reset()
{
	peak = 0.f;
	peak_time_ms = 0;
	grounded_since_ms = 0;
	grounded = false;
}

speed_readout_t speed_state_t::Update(const move_ring_t &ring, uint64_t window_ms, float deadband_ups)
{
	const move_sample_t *now = ring.Get(1);

	if (!now)
	{
		Reset();
		return {};
	}

	speed_readout_t out;
	out.current = now->speed;
	out.valid = true;

	if (now->discontinuity)
	{
		// Start again from here: the speed either side of a teleport is not a
		// gain or a loss, and the peak before it was somebody else's problem.
		Reset();
		grounded = now->on_ground_valid && now->on_ground;
		grounded_since_ms = now->time_ms;
		peak = now->speed;
		peak_time_ms = now->time_ms;

		out.peak = peak;
		return out;
	}

	if (now->on_ground_valid)
	{
		if (now->on_ground && !grounded)
		{
			grounded = true;
			grounded_since_ms = now->time_ms;
		}
		else if (!now->on_ground)
		{
			grounded = false;
		}

		if (grounded && now->time_ms - grounded_since_ms > SPEED_PEAK_GROUND_MS)
		{
			peak = now->speed;
			peak_time_ms = now->time_ms;
		}
	}
	else
	{
		grounded = false;

		if (now->time_ms - peak_time_ms >= SPEED_PEAK_DECAY_MS)
		{
			peak = now->speed;
			peak_time_ms = now->time_ms;
		}
	}

	if (now->speed >= peak)
	{
		peak = now->speed;
		peak_time_ms = now->time_ms;
	}

	out.peak = peak;

	if (const move_sample_t *then = ring.AtAge(now->time_ms, window_ms))
	{
		out.delta = now->speed - then->speed;

		// The deadband is not cosmetic: a standing player's speed jitters by
		// fractions of a unit, and an arrow that flickers between gaining and
		// losing is worse than no arrow at all.
		if (out.delta > deadband_ups)
			out.trend = 1;
		else if (out.delta < -deadband_ups)
			out.trend = -1;
	}

	return out;
}

// ---------------------------------------------------------------------------
// Strafe efficiency
// ---------------------------------------------------------------------------

static constexpr double JUMP_PI = 3.14159265358979323846;

static double Radians(double degrees)
{
	return degrees * (JUMP_PI / 180.0);
}

static double Degrees(double radians)
{
	return radians * (180.0 / JUMP_PI);
}

void MoveAxes(float pitch_deg, float yaw_deg, float roll_deg, float out_forward_xy[2],
			  float out_right_xy[2])
{
	// pmove wraps pitch into (-180,180] and divides by three before building the
	// axes (p_move.cpp:1684-1692). Skipping either step tilts every wishdir this
	// file derives, by up to 13% on the forward axis.
	double pitch = pitch_deg;

	while (pitch > 180.0)
		pitch -= 360.0;
	while (pitch <= -180.0)
		pitch += 360.0;

	pitch /= 3.0;

	const double p = Radians(pitch);
	const double y = Radians(yaw_deg);
	const double r = Radians(roll_deg);

	const double sp = std::sin(p), cp = std::cos(p);
	const double sy = std::sin(y), cy = std::cos(y);
	const double sr = std::sin(r), cr = std::cos(r);

	out_forward_xy[0] = (float) (cp * cy);
	out_forward_xy[1] = (float) (cp * sy);

	// AngleVectors' right vector. At roll 0 this is (sin yaw, -cos yaw) and
	// carries no pitch at all - only forward is scaled by cos(pitch/3).
	out_right_xy[0] = (float) (-sr * sp * cy + cr * sy);
	out_right_xy[1] = (float) (-sr * sp * sy - cr * cy);
}

void WishVelocity(float pitch_deg, float yaw_deg, float roll_deg, float forwardmove, float sidemove,
				  float out_xy[2])
{
	float forward[2], right[2];
	MoveAxes(pitch_deg, yaw_deg, roll_deg, forward, right);

	out_xy[0] = forward[0] * forwardmove + right[0] * sidemove;
	out_xy[1] = forward[1] * forwardmove + right[1] * sidemove;
}

float AccelGain(float speed, float along, float target, float budget)
{
	double a = (double) target - (double) along;

	if (a < 0.0)
		a = 0.0;
	if (a > (double) budget)
		a = (double) budget;

	const double s = speed;
	const double delta = 2.0 * a * (double) along + a * a;

	if (delta == 0.0)
		return 0.f;

	// Algebraically sqrt(s*s + delta) - s, but that form loses about four
	// significant digits in float at jump speeds, where s*s is ~1e5 and delta is
	// a few thousand. The denominator is at least s, so no new hazard.
	const double root = std::sqrt(s * s + delta);

	if (root + s <= 0.0)
		return 0.f;

	return (float) (delta / (root + s));
}

float BestAlong(float speed, float target, float budget)
{
	double u = (double) target - (double) budget;

	// The low clamp is zero, not -speed. Past this point the gain is
	// s^2 + target^2 - along^2, which peaks at along = 0, so when the budget
	// exceeds the target the best direction is perpendicular to your velocity.
	// Clamping lower reports a large loss as the maximum gain.
	if (u < 0.0)
		u = 0.0;
	if (u > (double) speed)
		u = speed;

	return (float) u;
}

strafe_frame_t StrafeFrame(const float vel_before_xy[2], float pitch_deg, float yaw_deg,
						   float roll_deg, float forwardmove, float sidemove, float dt, bool ducked,
						   int air_accel, bool on_ground)
{
	strafe_frame_t out;

	// A negative sv_airaccelerate is reachable - the cvar has no lower bound -
	// and would make the whole model degenerate.
	const float accel =
		on_ground ? PM_GROUND_ACCEL : (air_accel > 0 ? (float) air_accel : (air_accel == 0 ? 1.f : 0.f));

	if (accel <= 0.f || dt <= 0.f)
		return out;

	float wishvel[2];
	WishVelocity(pitch_deg, yaw_deg, roll_deg, forwardmove, sidemove, wishvel);

	float wishspeed = HorizontalSpeed(wishvel[0], wishvel[1]);

	if (wishspeed < 1.f)
		return out; // no directional input: nothing was on offer

	const float wishdir[2] = { wishvel[0] / wishspeed, wishvel[1] / wishspeed };

	// The clamp reduces wishspeed only; wishdir was normalised before it and is
	// deliberately left alone, exactly as PM_AirMove does it.
	const float maxspeed = ducked ? PM_DUCKSPEED : PM_MAXSPEED;

	if (wishspeed > maxspeed)
		wishspeed = maxspeed;

	out.wishspeed = wishspeed;
	// The 30-clamp is an AIR rule. PM_WalkMove hands the full wishspeed straight
	// to PM_Accelerate, so on the ground the target is never clamped.
	out.target = (!on_ground && air_accel != 0) ? (wishspeed < PM_AIR_TARGET ? wishspeed : PM_AIR_TARGET)
												: wishspeed;
	out.budget = accel * wishspeed * dt;

	const float speed = HorizontalSpeed(vel_before_xy[0], vel_before_xy[1]);

	out.along = vel_before_xy[0] * wishdir[0] + vel_before_xy[1] * wishdir[1];
	out.along_best = BestAlong(speed, out.target, out.budget);

	out.gain = AccelGain(speed, out.along, out.target, out.budget);
	out.gain_max = AccelGain(speed, out.along_best, out.target, out.budget);

	out.over_turning = out.along < out.along_best;
	out.opportunity = out.gain_max > STRAFE_MIN_GAIN;

	return out;
}

void jump_takeoff_state_t::Reset()
{
	*this = jump_takeoff_state_t {};
}

bool jump_takeoff_state_t::Update(bool on_ground, float speed, uint64_t dt_ms)
{
	bool produced = false;

	if (on_ground)
	{
		ground_ms += dt_ms;
		have_ground = true;

		// The chain is over. Clear the mark rather than leave the speed of a run
		// the player has already stopped sitting above their live number.
		if (ground_ms >= JUMP_TAKEOFF_CHAIN_BREAK_MS)
		{
			have_speed = false;
			this->speed = 0;
		}
	}

	// Takeoff: on the ground last frame, not on it now. Leaving a ledge counts,
	// which is right - it is still the start of an air phase, and the mark stays
	// honest about what the whole cycle did.
	//
	// Two things have to be true before that is a real takeoff. The feet must
	// have been on the ground at some point, or the first frame of all counts as
	// one - which is what put a "0" on screen the instant a player joined. And
	// the player must be moving: hopping on the spot, or being put somewhere by a
	// teleport or a recall, leaves the ground at nothing, and a mark of 0 is
	// worse than no mark.
	if (airborne == false && on_ground == false && have_ground && speed >= JUMP_TAKEOFF_MIN_SPEED)
	{
		// Rounded the same way the live speedometer rounds, so the frozen number
		// and the moving one cannot disagree at the instant of takeoff.
		this->speed = SpeedStat(speed, 0.f);
		have_speed = true;
		ground_ms = 0;
		produced = true;
	}

	airborne = !on_ground;

	return produced;
}

bool TeleportSpeedAllows(float horizontal_speed, float required)
{
	if (!(required > 0.f))
		return true; // no key, zero, negative or NaN: no gate

	return horizontal_speed >= required;
}

float WrapDegrees(float degrees)
{
	while (degrees > 180.f)
		degrees -= 360.f;
	while (degrees <= -180.f)
		degrees += 360.f;

	return degrees;
}

// acos in degrees, with the argument clamped. Every use below divides by a
// speed, and a speed one float-epsilon under the numerator would otherwise hand
// acos a value outside its domain and return a NaN into the drawing.
static float AcosDegrees(float value)
{
	if (value > 1.f)
		value = 1.f;
	if (value < -1.f)
		value = -1.f;

	return (float) Degrees(std::acos(value));
}

cgaz_readout_t CgazFromSample(const move_sample_t &sample)
{
	cgaz_readout_t out;

	// CGaz is live, the way every other CGaz is: it follows whatever branch pmove
	// is actually taking, and the GROUND is a branch rather than a reason to stop
	// drawing. This is where it parts company with the strafe meter, which takes
	// a ground frame only when the surface is frictionless - and the two are
	// asking different questions. Grading a ground frame needs the
	// reconstruction to be exact, and on ordinary floor it is not: friction has
	// already run by the time PM_Accelerate sees the velocity. Pointing at the
	// angles that would speed you up is far more forgiving, and a strip that
	// blanked every time you touched the floor would be useless on a hop chain.
	//
	// What is still refused is anything where the wish itself is not what the
	// keys said. PM_AddCurrents rewrites it on ladders and in water, so the
	// drawn angles would not merely be imprecise, they would point somewhere
	// else entirely.
	if (!sample.predicted || !sample.inputs_valid || sample.msec == 0)
		return out;

	if (sample.discontinuity || !sample.pm_normal || sample.on_ladder || sample.water_level > 0)
		return out;

	// Ground state is the viewer's while chasing (see move_sample_t), so the
	// branch cannot be chosen and the strip would be modelling the wrong one.
	if (!sample.on_ground_valid)
		return out;

	const float vx = sample.vel_before[0];
	const float vy = sample.vel_before[1];
	const float speed = HorizontalSpeed(vx, vy);

	// Below walking pace the velocity has no meaningful heading to be an angle
	// from, and the strip would spin on noise.
	if (speed < 10.f)
		return out;

	// on_ground_entry rather than on_ground: the branch is chosen from the state
	// pmove STARTED the command in, and on_ground after the move is where the
	// player landed.
	const strafe_frame_t frame =
		StrafeFrame(sample.vel_before, sample.view_pitch, sample.view_yaw, sample.view_roll,
					sample.forwardmove, sample.sidemove, (float) sample.msec / 1000.f, sample.ducked,
					sample.air_accel, sample.on_ground_entry);

	if (!frame.opportunity)
		return out;

	// Where the wish points now. StrafeFrame worked this out too, but from the
	// projection alone the heading cannot be recovered, so it comes back out of
	// the same function rather than being reconstructed a second way - one place
	// that knows what the keys mean, pitch/3 included.
	float wishvel[2];
	WishVelocity(sample.view_pitch, sample.view_yaw, sample.view_roll, sample.forwardmove, sample.sidemove,
				 wishvel);

	const float wish_heading = (float) Degrees(std::atan2(wishvel[1], wishvel[0]));
	const float vel_heading = (float) Degrees(std::atan2(vy, vx));

	// Turning the view by r turns the wish by r, so the strip is just the angles
	// either side of where the wish would line up with the velocity.
	//
	// SIGN, and it is the one thing here worth reading twice. Quake yaw counts
	// counter-clockwise, so a world heading further anticlockwise than your view
	// appears on the LEFT of the screen. `base` is therefore positive-left, and
	// the drawing has to map positive degrees leftwards to match. Getting this
	// backwards mirrors the whole instrument, and no test of this function can
	// catch it, because everything below is symmetric in |base|.
	out.base = WrapDegrees(vel_heading - wish_heading);

	// The near edge is where the projection reaches `target` and PM_Accelerate
	// stops paying out. When the target is above your speed there is no near
	// edge at all - every angle from dead ahead outwards earns something, which
	// is why slow players cannot strafe wrong. AcosDegrees clamps, so a ratio
	// over 1 already lands on zero without a test here.
	out.zone_inner = AcosDegrees(frame.target / speed);

	// The far edge is where you start pushing hard enough against your own
	// velocity to lose. Gain is positive when 2*along + a > 0, with
	// a = clamp(target - along, 0, budget), and that splits:
	//
	//   along <= target - budget : a is the budget      -> along > -budget/2
	//   along >  target - budget : a is target - along  -> along > -target
	//
	// The first branch is empty once the budget reaches twice the target, which
	// is the 30-clamp model's normal state, so the bound there is the target and
	// not half of it. Note the halving applies to ONLY the budget - taking half
	// of whichever is smaller is a different and wrong answer.
	const float half_budget = frame.budget * 0.5f;
	const float far_along = -(half_budget < frame.target ? half_budget : frame.target);

	out.zone_outer = AcosDegrees(far_along / speed);
	out.optimal = AcosDegrees(frame.along_best / speed);

	// Which of the two optima to draw. A view-relative angle r puts the wish at
	// (r - base) from the velocity, so the current wish sits at -base - and the
	// solution on that same side is the one worth pointing at:
	//
	//   wish side = sign(-base)  ->  r = base + sign(-base) * optimal
	//
	// At base == 0 the wish is straight down the velocity and the two are equally
	// good; it picks one, and swaps the instant you rotate off. That swap is a
	// real jump across the dead wedge, and it is what every CGaz does when you
	// change strafe direction.
	out.optimal_view = out.base + (out.base >= 0.f ? -out.optimal : out.optimal);

	// The view sits `base` degrees off the wish-along-velocity angle, so that is
	// also how far the current wish is from the velocity.
	const float here = std::fabs(out.base);

	out.inside = here >= out.zone_inner && here <= out.zone_outer;
	out.valid = true;

	return out;
}

strafe_frame_kind_t ClassifyStrafeFrame(const move_sample_t &sample)
{
	// Anything that would make the reconstruction inexact is excluded outright
	// rather than shown as a bad score - telling a player they strafed badly
	// when the truth is we could not tell is the one failure worth avoiding.
	if (!sample.predicted || !sample.inputs_valid || sample.msec == 0)
		return strafe_frame_kind_t::excluded;

	if (sample.discontinuity || !sample.pm_normal)
		return strafe_frame_kind_t::excluded;

	// A fresh jump press clears groundentity partway through the command, so the
	// accel runs in the AIR branch even though the frame started on the floor.
	// Neither model describes it, so it is excluded whatever it was stood on.
	if (sample.jumped)
		return strafe_frame_kind_t::excluded;

	// Ground at either end of the command: friction ran, or the slide move put
	// us on the floor partway through - EXCEPT on ice, where PM_Friction's drop
	// is exactly zero (p_move.cpp:564) and the reconstruction is as exact as it
	// is in the air. Ice strafing is a real technique and a sustained one, so
	// refusing to grade it left the bar dead for the whole slide.
	//
	// Both ends of the command have to be frictionless. on_slick is sampled from
	// the pre-move origin, so a slide that ends on normal floor is excluded by
	// on_ground below - which is what we want, since friction ran on that one.
	if (sample.on_ground_entry && !sample.on_slick)
		return strafe_frame_kind_t::excluded;

	if (sample.on_ground && !sample.on_slick)
		return strafe_frame_kind_t::excluded;

	// PM_AddCurrents rewrites the wish vector on ladders and in water, and both
	// take friction.
	if (sample.on_ladder || sample.water_level != 0)
		return strafe_frame_kind_t::excluded;

	if (sample.timed_move)
		return strafe_frame_kind_t::excluded;

	if (sample.forwardmove == 0.f && sample.sidemove == 0.f)
		return strafe_frame_kind_t::no_opportunity;

	return strafe_frame_kind_t::usable;
}

void strafe_state_t::Reset()
{
	weight = 0.f;
	taken = 0.f;
	signed_loss = 0.f;
	idle_ms = 0;
	last_time_ms = 0;
	have_last = false;
}

strafe_readout_t strafe_state_t::Add(const strafe_frame_t &frame, bool usable, uint64_t dt_ms,
									 uint64_t tau_ms)
{
	strafe_readout_t out;

	if (dt_ms > 2000)
		dt_ms = 2000;

	if (tau_ms == 0)
		tau_ms = 1;

	const double decay = std::exp(-(double) dt_ms / (double) tau_ms);

	weight = (float) (weight * decay);
	taken = (float) (taken * decay);
	signed_loss = (float) (signed_loss * decay);

	if (usable && frame.opportunity)
	{
		// Clamped at zero rather than allowed negative, so one frame of
		// accelerating backwards reads as a zero rather than cancelling out
		// a good frame either side of it.
		const float got = frame.gain > 0.f ? frame.gain : 0.f;

		weight += frame.gain_max;
		taken += got;
		signed_loss += (frame.over_turning ? 1.f : -1.f) * (frame.gain_max - got);

		out.frame = got / frame.gain_max;
		out.opportunity = true;
		idle_ms = 0;
	}
	else
	{
		idle_ms += dt_ms;
	}

	if (weight > STRAFE_MIN_WEIGHT && idle_ms < STRAFE_HOLD_MS)
	{
		out.valid = true;
		out.efficiency = taken / weight;
		out.offset = signed_loss / weight;

		if (out.efficiency < 0.f)
			out.efficiency = 0.f;
		if (out.efficiency > 1.f)
			out.efficiency = 1.f;
		if (out.offset < -1.f)
			out.offset = -1.f;
		if (out.offset > 1.f)
			out.offset = 1.f;
	}

	return out;
}

strafe_readout_t strafe_state_t::Update(const move_ring_t &ring, uint64_t tau_ms)
{
	const move_sample_t *now = ring.Get(1);

	if (!now)
	{
		Reset();
		return {};
	}

	// A teleport or a recall must not be averaged across.
	if (now->discontinuity)
		Reset();

	uint64_t dt_ms = 0;

	if (have_last && now->time_ms > last_time_ms)
		dt_ms = now->time_ms - last_time_ms;

	last_time_ms = now->time_ms;
	have_last = true;

	const bool usable = ClassifyStrafeFrame(*now) == strafe_frame_kind_t::usable;

	strafe_frame_t frame;

	if (usable)
	{
		const float vel[2] = { now->vel_before[0], now->vel_before[1] };

		// The ground model when the frame was admitted on ice: PM_WalkMove hands
		// the full wishspeed to PM_Accelerate at accel 10, with no 30-clamp.
		// Same switch CGaz uses, and the reason it was already a parameter.
		const bool on_ground = now->on_ground_entry;

		frame = StrafeFrame(vel, now->view_pitch, now->view_yaw, now->view_roll, now->forwardmove,
							now->sidemove, now->msec / 1000.f, now->ducked, now->air_accel, on_ground);
	}

	return Add(frame, usable, dt_ms, tau_ms);
}

void SpeedDigits(int speed, int out[SPEED_DIGITS])
{
	if (speed < 0)
		speed = 0;
	if (speed > SPEED_STAT_MAX)
		speed = SPEED_STAT_MAX;

	int place = 1000;

	for (int i = 0; i < SPEED_DIGITS; i++, place /= 10)
		out[i] = (speed / place) % 10;

	// Blank the leading zeros. The units digit always draws, so a genuine zero
	// still reads as "0" rather than as nothing at all.
	for (int i = 0; i < SPEED_DIGITS - 1; i++)
	{
		if (out[i] != 0)
			break;

		out[i] = -1;
	}
}

int StrafeBarLevel(const strafe_readout_t &readout, int segments)
{
	if (!readout.valid || segments < 1)
		return -1;

	float efficiency = readout.efficiency;

	if (efficiency < 0.f)
		efficiency = 0.f;
	if (efficiency > 1.f)
		efficiency = 1.f;

	int level = (int) (efficiency * segments + 0.5f);

	if (level < 0)
		level = 0;
	if (level > segments)
		level = segments;

	return level;
}

bool StrafeBarDeadSide(const strafe_readout_t &readout)
{
	return readout.valid && readout.offset <= STRAFE_DEAD_OFFSET;
}

std::string StrafeBarString(int level, int segments, bool dead_side)
{
	if (segments < 1)
		return {};

	if (level < 0)
		level = 0;
	if (level > segments)
		level = segments;

	// Plain ASCII on purpose. The bar has to render in both HUD fonts - the
	// 1997 character set and the rerelease's own - and anything outside ASCII
	// is only guaranteed in one of them.
	// The dead side is marked inside the bar, so both families are the same number
	// of glyphs. A cstring2 centres the whole string, so a marker appended on the
	// end would slide the bar sideways every time it appeared - and it appears
	// exactly when the player is crossing the line repeatedly, which is the shimmy
	// this readout is meant to cure.
	//
	// ONE cell, immediately past the fill, rather than the whole remainder. Filling
	// every empty cell was the first attempt and it was unreadable: a dozen glyphs
	// of noise, with no way to see where the fill ended. A single mark sits exactly
	// at the boundary of what you captured, which is the thing worth looking at.
	//
	// '!' rather than an arrow, deliberately. The bar has no left or right meaning
	// - "turn less" is a left correction or a right one depending on which way your
	// velocity lies, which is why the centred meter reports over/under rather than
	// a screen direction - so a glyph that points somewhere would invite exactly
	// that misreading. This one only says "the missing part is the kind that pays
	// nothing".
	std::string out = "[";

	out.append((size_t) level, '#');

	if (dead_side && level < segments)
	{
		out += '!';
		out.append((size_t) (segments - level - 1), '-');
	}
	else
	{
		out.append((size_t) (segments - level), '-');
	}

	out += ']';

	return out;
}

bool PlayerVisibleToViewer(bool show_jumpers, bool eyecam_following_target, bool ent_is_viewer)
{
	if (ent_is_viewer)
		return true;

	if (eyecam_following_target)
		return false;

	return show_jumpers;
}

bool PlayerAudibleToViewer(bool show_jumpers, bool ent_is_viewer)
{
	if (ent_is_viewer)
		return true;

	return show_jumpers;
}

} // namespace jump
