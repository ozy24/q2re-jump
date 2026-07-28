// [Jump] Host-compiled tests for the engine-free logic layer.
//
// Built by tests/jump_tests.vcxproj; no engine headers, no Quake II needed.

#include "../src/jump/jump_logic.h"

#include <cstdio>
#include <string>

static int g_failures = 0;
static int g_checks = 0;

static void Check(bool ok, const char *expr, const char *file, int line)
{
	g_checks++;

	if (!ok)
	{
		g_failures++;
		printf("FAIL %s:%d: %s\n", file, line, expr);
	}
}

static void CheckEq(const std::string &got, const std::string &want, const char *expr, const char *file, int line)
{
	g_checks++;

	if (got != want)
	{
		g_failures++;
		printf("FAIL %s:%d: %s\n  got  \"%s\"\n  want \"%s\"\n", file, line, expr, got.c_str(), want.c_str());
	}
}

#define CHECK(x)		 Check((x), #x, __FILE__, __LINE__)
#define CHECK_EQ(a, b) CheckEq((a), (b), #a " == " #b, __FILE__, __LINE__)

static void TestFormatTime()
{
	CHECK_EQ(jump::FormatTime(0), "0.000");
	CHECK_EQ(jump::FormatTime(1), "0.001");
	CHECK_EQ(jump::FormatTime(999), "0.999");
	CHECK_EQ(jump::FormatTime(1000), "1.000");
	CHECK_EQ(jump::FormatTime(12345), "12.345");
	CHECK_EQ(jump::FormatTime(123456), "123.456");

	// Negative elapsed time can't happen in play, but must not print garbage.
	CHECK_EQ(jump::FormatTime(-5), "0.000");
}

static void TestFormatDelta()
{
	CHECK_EQ(jump::FormatDelta(0), "+0.000");
	CHECK_EQ(jump::FormatDelta(1234), "+1.234");
	CHECK_EQ(jump::FormatDelta(-1234), "-1.234");
	CHECK_EQ(jump::FormatDelta(-60000), "-60.000");
}

static jump::store_slot_t MakeSlot(float x, int64_t elapsed)
{
	jump::store_slot_t s;
	s.origin[0] = x;
	s.elapsed_ms = elapsed;
	return s;
}

static void TestStoreRing()
{
	jump::store_ring_t ring;

	CHECK(ring.Empty());
	CHECK(ring.Get(1) == nullptr);

	ring.Push(MakeSlot(1.f, 1000));
	CHECK(!ring.Empty());
	CHECK(ring.count == 1);
	CHECK(ring.Get(1)->origin[0] == 1.f);

	// Asking past the stack depth clamps to the oldest slot.
	CHECK(ring.Get(5)->origin[0] == 1.f);
	CHECK(ring.Get(0)->origin[0] == 1.f);

	ring.Push(MakeSlot(2.f, 2000));
	ring.Push(MakeSlot(3.f, 3000));

	CHECK(ring.count == 3);
	CHECK(ring.Get(1)->origin[0] == 3.f);
	CHECK(ring.Get(2)->origin[0] == 2.f);
	CHECK(ring.Get(3)->origin[0] == 1.f);
	CHECK(ring.Get(4)->origin[0] == 1.f); // clamped

	// Overflow: the ring holds MAX_STORES and drops the oldest.
	ring.Push(MakeSlot(4.f, 4000));
	ring.Push(MakeSlot(5.f, 5000));
	ring.Push(MakeSlot(6.f, 6000));

	CHECK(ring.count == jump::MAX_STORES);
	CHECK(ring.Get(1)->origin[0] == 6.f);
	CHECK(ring.Get(5)->origin[0] == 2.f); // 1.f fell off the end

	CHECK(ring.Get(1)->elapsed_ms == 6000);

	ring.Clear();
	CHECK(ring.Empty());
	CHECK(ring.Get(1) == nullptr);
}

static void TestPoints()
{
	CHECK(jump::PointsForRank(1) == 25);
	CHECK(jump::PointsForRank(2) == 20);
	CHECK(jump::PointsForRank(3) == 16);
	CHECK(jump::PointsForRank(15) == 1);

	// Outside the scoring table.
	CHECK(jump::PointsForRank(16) == 0);
	CHECK(jump::PointsForRank(0) == 0);
	CHECK(jump::PointsForRank(-1) == 0);

	int total = 0;
	for (int i = 1; i <= jump::MAX_HIGHSCORES; i++)
		total += jump::PointsForRank(i);
	CHECK(total == 140);
}

static void TestSafeName()
{
	CHECK_EQ(jump::SafeName("ddrace"), "ddrace");
	CHECK_EQ(jump::SafeName("DDRace"), "ddrace");
	CHECK_EQ(jump::SafeName("map-01_final"), "map-01_final");

	// Path traversal must not survive.
	CHECK_EQ(jump::SafeName(".."), "_");
	CHECK_EQ(jump::SafeName("../../etc/passwd"), "_.._etc_passwd");
	CHECK_EQ(jump::SafeName("maps/foo"), "maps_foo");
	CHECK_EQ(jump::SafeName("foo\\bar"), "foo_bar");
	CHECK_EQ(jump::SafeName("C:evil"), "c_evil");

	// Degenerate input still yields a usable component.
	CHECK_EQ(jump::SafeName(""), "_");
	CHECK_EQ(jump::SafeName("..."), "_");
}

static jump::record_t MakeRecord(const char *id, int64_t time_ms)
{
	jump::record_t r;
	r.id = id;
	r.name = id;
	r.time_ms = time_ms;
	r.date = "2026-07-28T00:00:00Z";
	return r;
}

static void TestRecords()
{
	jump::map_records_t records;

	CHECK(records.RankOf("alice") == 0);
	CHECK(records.TimeOf("alice") == 0);
	CHECK(records.PointsOf("alice") == 0);

	// First submissions are always accepted and sorted by time.
	CHECK(records.Submit(MakeRecord("alice", 5000)) == 1);
	CHECK(records.Submit(MakeRecord("bob", 3000)) == 1);
	CHECK(records.Submit(MakeRecord("carol", 4000)) == 2);

	CHECK(records.times.size() == 3);
	CHECK(records.RankOf("bob") == 1);
	CHECK(records.RankOf("carol") == 2);
	CHECK(records.RankOf("alice") == 3);

	// A slower run by an existing player changes nothing at all.
	CHECK(records.Submit(MakeRecord("alice", 9000)) == 0);
	CHECK(records.times.size() == 3);
	CHECK(records.TimeOf("alice") == 5000);

	// An improvement replaces their single entry rather than adding one.
	CHECK(records.Submit(MakeRecord("alice", 1000)) == 1);
	CHECK(records.times.size() == 3);
	CHECK(records.TimeOf("alice") == 1000);
	CHECK(records.RankOf("alice") == 1);
	CHECK(records.RankOf("bob") == 2);
	CHECK(records.PointsOf("alice") == 25);
	CHECK(records.PointsOf("bob") == 20);

	// Equal times keep the earlier holder ahead.
	CHECK(records.Submit(MakeRecord("dave", 3000)) == 3);
	CHECK(records.RankOf("bob") == 2);
	CHECK(records.RankOf("dave") == 3);
}

int main()
{
	TestFormatTime();
	TestFormatDelta();
	TestStoreRing();
	TestPoints();
	TestSafeName();
	TestRecords();

	printf("%d checks, %d failures\n", g_checks, g_failures);

	return g_failures == 0 ? 0 : 1;
}
