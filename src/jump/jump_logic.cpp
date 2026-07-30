// [Jump] Engine-free logic for the jump mod. See jump_logic.h.

#include "jump_logic.h"

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

} // namespace jump
