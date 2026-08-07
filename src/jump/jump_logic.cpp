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
