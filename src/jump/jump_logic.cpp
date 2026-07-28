// [Jump] Engine-free logic for the jump mod. See jump_logic.h.

#include "jump_logic.h"

#include <cstdio>
#include <cstdlib>

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

} // namespace jump
