// [Jump] Host-compiled tests for the engine-free logic layer.
//
// Built by tests/jump_tests.vcxproj; no engine headers, no Quake II needed.

#include "../src/jump/jump_logic.h"

#include <cstdio>
#include <string>
#include <vector>

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

static void TestIsSafeMapToken()
{
	CHECK(jump::IsSafeMapToken("q2dm1"));
	CHECK(jump::IsSafeMapToken("jumptest1"));
	CHECK(jump::IsSafeMapToken("packs/slip_map3"));
	CHECK(jump::IsSafeMapToken("map-01_final"));

	CHECK(!jump::IsSafeMapToken(""));
	CHECK(!jump::IsSafeMapToken("has space"));

	// Directory traversal, in every shape.
	CHECK(!jump::IsSafeMapToken(".."));
	CHECK(!jump::IsSafeMapToken("../secret"));
	CHECK(!jump::IsSafeMapToken("maps/../../etc"));
	CHECK(!jump::IsSafeMapToken("maps//double"));
	CHECK(!jump::IsSafeMapToken("./here"));
	CHECK(!jump::IsSafeMapToken("trailing/"));

	// Characters that would break out of `gamemap "<name>"`.
	CHECK(!jump::IsSafeMapToken("evil\"map"));
	CHECK(!jump::IsSafeMapToken("evil;quit"));
	CHECK(!jump::IsSafeMapToken("back\\slash"));
	CHECK(!jump::IsSafeMapToken("star*"));
	CHECK(!jump::IsSafeMapToken("tick`cmd`"));

	// Over-long names would overflow MAX_QPATH.
	CHECK(!jump::IsSafeMapToken(std::string(64, 'a')));
	CHECK(jump::IsSafeMapToken(std::string(63, 'a')));
}

static void TestIsCheckpointBarrierTarget()
{
	// Upstream keys on a "checkpoint" prefix, so a suffix is allowed.
	CHECK(jump::IsCheckpointBarrierTarget("checkpoint"));
	CHECK(jump::IsCheckpointBarrierTarget("checkpoint1"));
	CHECK(jump::IsCheckpointBarrierTarget("checkpoint_final"));

	// Deliberately case-insensitive where upstream uses strncmp.
	CHECK(jump::IsCheckpointBarrierTarget("Checkpoint"));
	CHECK(jump::IsCheckpointBarrierTarget("CHECKPOINT4"));

	// A prefix match only: the marker has to start the target.
	CHECK(!jump::IsCheckpointBarrierTarget("mycheckpoint"));
	CHECK(!jump::IsCheckpointBarrierTarget("cp_checkpoint"));

	// Too short to be the marker, and the empty target an entity may carry.
	CHECK(!jump::IsCheckpointBarrierTarget("check"));
	CHECK(!jump::IsCheckpointBarrierTarget("checkpoin"));
	CHECK(!jump::IsCheckpointBarrierTarget(""));

	// Ordinary push targets must not be mistaken for barriers.
	CHECK(!jump::IsCheckpointBarrierTarget("door01"));
}

static void TestSanitizeLayoutText()
{
	CHECK_EQ(jump::SanitizeLayoutText("chris"), "chris");
	CHECK_EQ(jump::SanitizeLayoutText("two words"), "two words");

	// The characters that would break the client's layout parser.
	CHECK_EQ(jump::SanitizeLayoutText("say \"hi\""), "say hi");
	CHECK_EQ(jump::SanitizeLayoutText("back\\slash"), "back slash");
	// Split literals: "\x01c" would parse as the single byte 0x1c.
	CHECK_EQ(jump::SanitizeLayoutText("nul\x01" "ctrl"), "nul ctrl");
	CHECK_EQ(jump::SanitizeLayoutText("hi\x80there"), "hi there");

	// Runs of whitespace collapse, and edges are trimmed.
	CHECK_EQ(jump::SanitizeLayoutText("  lots   of   space  "), "lots of space");
	CHECK_EQ(jump::SanitizeLayoutText("\"\"\""), "?");
	CHECK_EQ(jump::SanitizeLayoutText(""), "?");

	// Truncation never leaves a trailing space.
	CHECK_EQ(jump::SanitizeLayoutText("abcdefghij", 5), "abcde");
	CHECK_EQ(jump::SanitizeLayoutText("ab cdefghij", 5), "ab cd");
	CHECK(jump::SanitizeLayoutText("a very long player name here", 10).size() <= 10);
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

static void TestJumpersPolicy()
{
	// Self is always visible / audible.
	CHECK(jump::PlayerVisibleToViewer(true, false, true));
	CHECK(jump::PlayerVisibleToViewer(false, false, true));
	CHECK(jump::PlayerVisibleToViewer(false, true, true));
	CHECK(jump::PlayerAudibleToViewer(true, true));
	CHECK(jump::PlayerAudibleToViewer(false, true));

	// Other players follow the jumpers preference.
	CHECK(jump::PlayerVisibleToViewer(true, false, false));
	CHECK(!jump::PlayerVisibleToViewer(false, false, false));
	CHECK(jump::PlayerAudibleToViewer(true, false));
	CHECK(!jump::PlayerAudibleToViewer(false, false));

	// First-person eyecam always hides the followed body, even with jumpers on.
	CHECK(!jump::PlayerVisibleToViewer(true, true, false));
	CHECK(!jump::PlayerVisibleToViewer(false, true, false));
}

static jump::player_row_t MakeRow(const char *name, int64_t session, int64_t pb, bool spectator)
{
	jump::player_row_t row;
	row.name = name;
	row.session_ms = session;
	row.pb_ms = pb;
	row.spectator = spectator;
	return row;
}

static std::string RowOrder(const std::vector<jump::player_row_t> &rows)
{
	std::string order;

	for (const jump::player_row_t &row : rows)
	{
		if (!order.empty())
			order += ",";
		order += row.name;
	}

	return order;
}

static void TestSortPlayerRows()
{
	// Whoever has posted a time today leads, fastest first; then the rest by
	// all-time best; spectators last.
	std::vector<jump::player_row_t> rows = {
		MakeRow("spec", 0, 1000, true),		 MakeRow("yet_to_run_slow", 0, 9000, false),
		MakeRow("posted_slow", 8000, 0, false), MakeRow("yet_to_run_fast", 0, 2000, false),
		MakeRow("posted_fast", 3000, 0, false),
	};

	jump::SortPlayerRows(rows);
	CHECK_EQ(RowOrder(rows), "posted_fast,posted_slow,yet_to_run_fast,yet_to_run_slow,spec");

	// A slow time today still beats a fast all-time best that has not been
	// matched today - the first column is what the page is ranking.
	rows = { MakeRow("pb_hero", 0, 100, false), MakeRow("showed_up", 99999, 0, false) };
	jump::SortPlayerRows(rows);
	CHECK_EQ(RowOrder(rows), "showed_up,pb_hero");

	// Never finished the map at all sorts behind every all-time time, however slow.
	rows = { MakeRow("unranked", 0, 0, false), MakeRow("slow", 0, 999999, false) };
	jump::SortPlayerRows(rows);
	CHECK_EQ(RowOrder(rows), "slow,unranked");

	// A spectator stays last even holding the fastest time on the board.
	rows = { MakeRow("watcher", 1, 1, true), MakeRow("player", 0, 0, false) };
	jump::SortPlayerRows(rows);
	CHECK_EQ(RowOrder(rows), "player,watcher");

	// Ties keep collection order, so the board does not reshuffle every resend.
	rows = { MakeRow("c", 5000, 0, false), MakeRow("a", 5000, 0, false), MakeRow("b", 5000, 0, false) };
	jump::SortPlayerRows(rows);
	CHECK_EQ(RowOrder(rows), "c,a,b");

	// Degenerate inputs must not fall over.
	rows.clear();
	jump::SortPlayerRows(rows);
	CHECK(rows.empty());
}

static void TestParseMset()
{
	// The forms a cfg file or an `sv jump_mset` argument might reasonably use.
	CHECK(jump::ParseMsetBool("1") == true);
	CHECK(jump::ParseMsetBool("0") == false);
	CHECK(jump::ParseMsetBool("on") == true);
	CHECK(jump::ParseMsetBool("off") == false);
	CHECK(jump::ParseMsetBool("yes") == true);
	CHECK(jump::ParseMsetBool("no") == false);

	// Case and surrounding whitespace are the caller's problem, not the map
	// author's - a cfg line arrives with its trailing \r still attached.
	CHECK(jump::ParseMsetBool("TRUE") == true);
	CHECK(jump::ParseMsetBool("False") == false);
	CHECK(jump::ParseMsetBool("  On \r") == true);

	// atoi treated every other number as true; keep that, since a saved file
	// could hold one and rejecting it would change how an existing map plays.
	CHECK(jump::ParseMsetBool("2") == true);
	CHECK(jump::ParseMsetBool("-1") == true);

	// The whole point: a typo must be reported, not silently read as off.
	CHECK(jump::ParseMsetBool("banana") == std::nullopt);
	CHECK(jump::ParseMsetBool("") == std::nullopt);
	CHECK(jump::ParseMsetBool("   ") == std::nullopt);
	CHECK(jump::ParseMsetBool("1x") == std::nullopt);
	CHECK(jump::ParseMsetBool("onn") == std::nullopt);

	CHECK(jump::ParseMsetInt("800") == 800);
	CHECK(jump::ParseMsetInt("0") == 0);
	CHECK(jump::ParseMsetInt("-400") == -400);
	CHECK(jump::ParseMsetInt("+400") == 400);
	CHECK(jump::ParseMsetInt(" 800\r") == 800);

	// atoi read all of these as a number; none of them are one.
	CHECK(jump::ParseMsetInt("800x") == std::nullopt);
	CHECK(jump::ParseMsetInt("abc") == std::nullopt);
	CHECK(jump::ParseMsetInt("") == std::nullopt);
	CHECK(jump::ParseMsetInt("-") == std::nullopt);
	CHECK(jump::ParseMsetInt("4.5") == std::nullopt);
	CHECK(jump::ParseMsetInt("8 0 0") == std::nullopt);

	// Out of range clamps rather than wrapping into a plausible gravity.
	CHECK(jump::ParseMsetInt("99999999999") == 2147483647);
	CHECK(jump::ParseMsetInt("-99999999999") == -2147483647 - 1);
}

static void TestSpeedStat()
{
	// At rest the element is hidden - 0 is a contract, not a rounding artefact.
	CHECK(jump::SpeedStat(0.0f, 0.0f) == 0);
	CHECK(jump::SpeedStat(-0.0f, -0.0f) == 0);

	// A speedometer has no direction.
	CHECK(jump::SpeedStat(320.0f, 0.0f) == 320);
	CHECK(jump::SpeedStat(-320.0f, 0.0f) == 320);
	CHECK(jump::SpeedStat(0.0f, -320.0f) == 320);

	// Pythagoras, not an axis sum: 3-4-5 would read 700 if the axes were added.
	CHECK(jump::SpeedStat(300.0f, 400.0f) == 500);

	// Truncated rather than rounded, matching both upstream mods, so the number
	// agrees with the one a classic q2jump server would show.
	CHECK(jump::SpeedStat(320.0f, 320.0f) == 452);

	// The ifstat boundary: the last value that hides, the first that shows.
	CHECK(jump::SpeedStat(0.9f, 0.0f) == 0);
	CHECK(jump::SpeedStat(1.0f, 0.0f) == 1);

	// sv_maxvelocity 2000 on both axes - the fastest legitimate reading, and
	// still four digits, so the clamp never engages in normal play.
	CHECK(jump::SpeedStat(2000.0f, 2000.0f) == 2828);

	// The clamp. Without it the `num 4` field truncates to LEADING digits, so
	// 10000 would draw "1000" - a plausible, wrong number.
	CHECK(jump::SpeedStat(9999.0f, 0.0f) == jump::SPEED_STAT_MAX);
	CHECK(jump::SpeedStat(10000.0f, 0.0f) == jump::SPEED_STAT_MAX);
	CHECK(jump::SpeedStat(50000.0f, 12345.0f) == jump::SPEED_STAT_MAX);

	// Overflow clamps rather than wrapping negative through the cast.
	CHECK(jump::SpeedStat(1e30f, 1e30f) == jump::SPEED_STAT_MAX);
	CHECK(jump::SpeedStat(1e30f, 1e30f) >= 0);

	// The value is published in an int16_t stat slot.
	CHECK(jump::SPEED_STAT_MAX <= 32767);

	CHECK(jump::HorizontalSpeed(3.0f, 4.0f) == 5.0f);
	CHECK(jump::HorizontalSpeed(0.0f, 0.0f) == 0.0f);
}

static void TestFormatSpeed()
{
	CHECK_EQ(jump::FormatSpeed(0.0f), "0");
	CHECK_EQ(jump::FormatSpeed(1234.7f), "1234");
	CHECK_EQ(jump::FormatSpeed(-1.0f), "1");

	CHECK_EQ(jump::FormatSpeedDelta(38.4f), "+38");
	CHECK_EQ(jump::FormatSpeedDelta(-12.9f), "-12");
	CHECK_EQ(jump::FormatSpeedDelta(0.0f), "+0");

	// A loss too small to show as a digit must not read "-0".
	CHECK_EQ(jump::FormatSpeedDelta(-0.4f), "+0");
}

static jump::move_sample_t MakeSample(uint64_t time_ms, float speed, bool on_ground)
{
	jump::move_sample_t sample;
	sample.time_ms = time_ms;
	sample.speed = speed;
	sample.on_ground = on_ground;
	return sample;
}

static void TestMoveRing()
{
	jump::move_ring_t ring;

	CHECK(ring.Empty());
	CHECK(ring.Get(1) == nullptr);
	CHECK(ring.AtAge(1000, 250) == nullptr);

	ring.Push(MakeSample(0, 100.f, true));
	ring.Push(MakeSample(100, 200.f, false));
	ring.Push(MakeSample(200, 300.f, false));
	ring.Push(MakeSample(300, 400.f, false));

	// Get(1) is the newest; higher values walk back and clamp to the oldest.
	CHECK(ring.Get(1)->speed == 400.f);
	CHECK(ring.Get(2)->speed == 300.f);
	CHECK(ring.Get(4)->speed == 100.f);
	CHECK(ring.Get(99)->speed == 100.f);
	CHECK(ring.Get(0)->speed == 400.f);

	// The newest sample at or before (now - age).
	CHECK(ring.AtAge(300, 150)->time_ms == 100);
	CHECK(ring.AtAge(300, 100)->time_ms == 200);
	CHECK(ring.AtAge(300, 0)->time_ms == 300);

	// Older than anything held - the history does not reach that far back.
	CHECK(ring.AtAge(300, 500) == nullptr);

	ring.Clear();
	CHECK(ring.Empty());
	CHECK(ring.Get(1) == nullptr);

	// A discontinuity walls off everything before it: comparing across a
	// teleport or a recall would report a speed change nobody made.
	jump::move_sample_t teleported = MakeSample(100, 0.f, false);
	teleported.discontinuity = true;

	ring.Push(MakeSample(0, 900.f, false));
	ring.Push(teleported);
	ring.Push(MakeSample(200, 100.f, false));
	ring.Push(MakeSample(300, 150.f, false));

	CHECK(ring.AtAge(300, 100)->time_ms == 200);
	CHECK(ring.AtAge(300, 250) == nullptr);

	// Overfilling drops the oldest samples, not the newest.
	ring.Clear();

	for (int i = 0; i < jump::MOVE_SAMPLES + 10; i++)
		ring.Push(MakeSample((uint64_t) i * 10, (float) i, false));

	CHECK(ring.Get(1)->speed == (float) (jump::MOVE_SAMPLES + 9));
	CHECK(ring.Get(jump::MOVE_SAMPLES)->speed == 10.f);
	CHECK(ring.Get(jump::MOVE_SAMPLES + 5)->speed == 10.f);
}

static void TestSpeedState()
{
	jump::move_ring_t  ring;
	jump::speed_state_t state;

	// Nothing sampled yet.
	CHECK(!state.Update(ring).valid);

	// The peak latches at the best of the jump, not the latest value.
	ring.Push(MakeSample(0, 300.f, false));
	state.Update(ring);
	ring.Push(MakeSample(100, 500.f, false));
	state.Update(ring);
	ring.Push(MakeSample(200, 400.f, false));

	jump::speed_readout_t out = state.Update(ring);
	CHECK(out.valid);
	CHECK(out.current == 400.f);
	CHECK(out.peak == 500.f);

	// A bunny-hop chain keeps its peak: a touch of ground shorter than
	// SPEED_PEAK_GROUND_MS is part of the same run, not the end of it.
	ring.Clear();
	state.Reset();

	ring.Push(MakeSample(0, 500.f, false));
	state.Update(ring);
	ring.Push(MakeSample(100, 500.f, true));
	state.Update(ring);
	ring.Push(MakeSample(200, 520.f, false));
	CHECK(state.Update(ring).peak == 520.f);

	// Standing still clears it.
	ring.Clear();
	state.Reset();

	ring.Push(MakeSample(0, 500.f, false));
	state.Update(ring);
	ring.Push(MakeSample(100, 40.f, true));
	CHECK(state.Update(ring).peak == 500.f);
	ring.Push(MakeSample(300, 40.f, true));
	CHECK(state.Update(ring).peak == 40.f);

	// A teleport or a recall starts again rather than showing a huge loss.
	ring.Clear();
	state.Reset();

	ring.Push(MakeSample(0, 900.f, false));
	state.Update(ring);

	jump::move_sample_t after_teleport = MakeSample(100, 100.f, false);
	after_teleport.discontinuity = true;
	ring.Push(after_teleport);

	out = state.Update(ring);
	CHECK(out.peak == 100.f);
	CHECK(out.delta == 0.f);
	CHECK(out.trend == 0);

	// Trend, with the deadband that stops a stationary player's jitter reading
	// as a gain.
	ring.Clear();
	state.Reset();

	ring.Push(MakeSample(0, 300.f, false));
	state.Update(ring);
	ring.Push(MakeSample(250, 301.f, false));
	out = state.Update(ring);
	CHECK(out.trend == 0);
	CHECK(out.delta == 1.f);

	ring.Push(MakeSample(500, 341.f, false));
	CHECK(state.Update(ring).trend == 1);

	ring.Push(MakeSample(750, 301.f, false));
	CHECK(state.Update(ring).trend == -1);

	// Irregular frame times must not break the window lookup.
	ring.Clear();
	state.Reset();

	const uint64_t times[] = { 0, 7, 23, 24, 91, 250 };
	const float	   speeds[] = { 100.f, 110.f, 120.f, 130.f, 140.f, 200.f };

	for (int i = 0; i < 6; i++)
	{
		ring.Push(MakeSample(times[i], speeds[i], false));
		out = state.Update(ring);
	}

	CHECK(out.delta == 100.f); // 200 now vs 100 at t=0, the sample 250 ms back
	CHECK(out.trend == 1);

	// A single sample has nothing to compare against, and must not read past
	// the start of the history.
	ring.Clear();
	state.Reset();

	ring.Push(MakeSample(1000, 400.f, false));
	out = state.Update(ring);
	CHECK(out.valid);
	CHECK(out.delta == 0.f);
	CHECK(out.trend == 0);

	// Chase cam: the ground flag belongs to the viewer, so the peak falls back
	// to a time decay instead of latching at the highest value ever seen.
	ring.Clear();
	state.Reset();

	jump::move_sample_t watched = MakeSample(0, 500.f, false);
	watched.on_ground_valid = false;
	ring.Push(watched);
	state.Update(ring);

	watched = MakeSample(1400, 100.f, false);
	watched.on_ground_valid = false;
	ring.Push(watched);
	CHECK(state.Update(ring).peak == 500.f);

	watched = MakeSample(1500, 100.f, false);
	watched.on_ground_valid = false;
	ring.Push(watched);
	CHECK(state.Update(ring).peak == 100.f);
}

int main()
{
	TestFormatTime();
	TestFormatDelta();
	TestStoreRing();
	TestPoints();
	TestSafeName();
	TestSanitizeLayoutText();
	TestIsSafeMapToken();
	TestIsCheckpointBarrierTarget();
	TestParseMset();
	TestSpeedStat();
	TestFormatSpeed();
	TestMoveRing();
	TestSpeedState();
	TestRecords();
	TestJumpersPolicy();
	TestSortPlayerRows();

	printf("%d checks, %d failures\n", g_checks, g_failures);

	return g_failures == 0 ? 0 : 1;
}
