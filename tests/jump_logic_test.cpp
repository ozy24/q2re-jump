// [Jump] Host-compiled tests for the engine-free logic layer.
//
// Built by tests/jump_tests.vcxproj; no engine headers, no Quake II needed.

#include "../src/jump/jump_logic.h"

#include <cmath>
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

static bool Near(float got, float want, float eps)
{
	const float diff = got > want ? got - want : want - got;

	if (diff <= eps)
		return true;

	printf("  got %.6f, want %.6f (+/- %.6f)\n", got, want, eps);
	return false;
}

// A clean airborne sample: predicted, real inputs, no ground, water, ladder or
// timed state - i.e. one the strafe meter will actually measure.
static jump::move_sample_t MakeAirSample(uint64_t time_ms, float vx, float vy, float yaw,
										 float fmove, float smove, uint8_t msec, int air_accel = 0)
{
	jump::move_sample_t sample;
	sample.time_ms = time_ms;
	sample.vel_before[0] = vx;
	sample.vel_before[1] = vy;
	sample.velocity[0] = vx;
	sample.velocity[1] = vy;
	sample.speed = jump::HorizontalSpeed(vx, vy);
	sample.view_yaw = yaw;
	sample.forwardmove = fmove;
	sample.sidemove = smove;
	sample.msec = msec;
	sample.air_accel = air_accel;
	sample.predicted = true;
	sample.inputs_valid = true;
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

static void TestMoveAxes()
{
	float forward[2], right[2];

	jump::MoveAxes(0.f, 0.f, 0.f, forward, right);
	CHECK(Near(forward[0], 1.f, 1e-5f));
	CHECK(Near(forward[1], 0.f, 1e-5f));
	CHECK(Near(right[0], 0.f, 1e-5f));
	CHECK(Near(right[1], -1.f, 1e-5f));

	jump::MoveAxes(0.f, 90.f, 0.f, forward, right);
	CHECK(Near(forward[0], 0.f, 1e-5f));
	CHECK(Near(forward[1], 1.f, 1e-5f));
	CHECK(Near(right[0], 1.f, 1e-5f));
	CHECK(Near(right[1], 0.f, 1e-5f));

	// Pitch reaches the move axes divided by three, so looking 60 degrees down
	// scales forward by cos(20), not cos(60). Right is untouched by pitch.
	jump::MoveAxes(60.f, 0.f, 0.f, forward, right);
	CHECK(Near(forward[0], 0.93969f, 1e-4f));
	CHECK(Near(right[0], 0.f, 1e-5f));
	CHECK(Near(right[1], -1.f, 1e-5f));

	// Angles arrive unwrapped: 330 is -30, so a third of it is -10.
	jump::MoveAxes(330.f, 0.f, 0.f, forward, right);
	CHECK(Near(forward[0], 0.98481f, 1e-4f));
}

static void TestAccelGain()
{
	// From rest every direction is equally good and you get the whole budget.
	CHECK(Near(jump::AccelGain(0.f, 0.f, 300.f, 7.5f), 7.5f, 1e-3f));

	// Perfectly aligned and well below the target: also the whole budget.
	CHECK(Near(jump::AccelGain(100.f, 100.f, 300.f, 7.5f), 7.5f, 1e-3f));

	// Accelerating into your own velocity loses speed. This must not be
	// clamped away here - the sign is real, and BestAlong depends on it.
	CHECK(Near(jump::AccelGain(100.f, -100.f, 300.f, 7.5f), -7.5f, 1e-3f));

	// Already past the target: PM_Accelerate's addspeed <= 0 early-out.
	CHECK(Near(jump::AccelGain(400.f, 400.f, 300.f, 7.5f), 0.f, 1e-4f));
}

static void TestBestAlong()
{
	// Vanilla air at speed: the optimum sits a budget below the target.
	CHECK(Near(jump::BestAlong(400.f, 300.f, 7.5f), 292.5f, 1e-3f));

	// Too slow to be off-axis at all - clamps to your own speed, i.e. point
	// straight along the velocity, and the gain is then the full budget.
	CHECK(Near(jump::BestAlong(100.f, 300.f, 7.5f), 100.f, 1e-3f));
	CHECK(Near(jump::AccelGain(100.f, 100.f, 300.f, 7.5f), 7.5f, 1e-3f));

	// The regression that matters. With the 30-clamp air model the budget
	// exceeds the target, and the optimum is a projection of ZERO - strafe
	// perpendicular. Clamping the low end below zero would return -45 here and
	// report a 1.4 ups LOSS as the best available gain.
	CHECK(Near(jump::BestAlong(400.f, 30.f, 75.f), 0.f, 1e-4f));
	CHECK(jump::AccelGain(400.f, -45.f, 30.f, 75.f) < 0.f);
	CHECK(jump::AccelGain(400.f, 0.f, 30.f, 75.f) > 0.f);
}

static void TestStrafeFrame()
{
	const float still[2] = { 400.f, 0.f };

	// No directional input: nothing was on offer, which is not the same as
	// having wasted what was.
	jump::strafe_frame_t none = jump::StrafeFrame(still, 0.f, 0.f, 0.f, 0.f, 0.f, 0.025f, false, 0);
	CHECK(!none.opportunity);

	// The headline case. At 400 ups on vanilla air physics, holding forward
	// captures NONE of what was available - the projection is already past the
	// target, so the engine's early-out fires and you gain nothing, while a
	// 43 degree strafe would have gained 5.5 ups.
	jump::strafe_frame_t fwd =
		jump::StrafeFrame(still, 0.f, 0.f, 0.f, 400.f, 0.f, 0.025f, false, 0);
	CHECK(fwd.opportunity);
	CHECK(Near(fwd.wishspeed, 300.f, 1e-3f)); // clamped from 400
	CHECK(Near(fwd.budget, 7.5f, 1e-3f));
	CHECK(Near(fwd.along_best, 292.5f, 1e-2f));
	CHECK(Near(fwd.gain_max, 5.5166f, 1e-2f));
	CHECK(Near(fwd.gain, 0.f, 1e-3f));
	// Aiming straight down your own velocity is the too-little-turn error.
	CHECK(!fwd.over_turning);

	// The same input at 100 ups is perfect: below the optimum's reach, pointing
	// straight along the velocity is the best there is.
	const float slow[2] = { 100.f, 0.f };
	jump::strafe_frame_t aligned =
		jump::StrafeFrame(slow, 0.f, 0.f, 0.f, 400.f, 0.f, 0.025f, false, 0);
	CHECK(Near(aligned.gain, aligned.gain_max, 1e-4f));
	CHECK(Near(aligned.gain, 7.5f, 1e-3f));

	// Ducking clamps wishspeed to 100, which drops both the target and the
	// budget - the same input that was perfect above now captures nothing.
	jump::strafe_frame_t ducked =
		jump::StrafeFrame(slow, 0.f, 0.f, 0.f, 400.f, 0.f, 0.025f, true, 0);
	CHECK(Near(ducked.wishspeed, 100.f, 1e-3f));
	CHECK(Near(ducked.budget, 2.5f, 1e-3f));
	CHECK(Near(ducked.gain, 0.f, 1e-3f));
	CHECK(ducked.gain_max > 2.f);

	// The 30-clamp model: perpendicular is exactly optimal, so a pure sideways
	// input at 90 degrees of yaw scores full marks.
	jump::strafe_frame_t perp =
		jump::StrafeFrame(still, 0.f, 90.f, 0.f, 400.f, 0.f, 0.025f, false, 10);
	CHECK(Near(perp.target, 30.f, 1e-4f));
	CHECK(Near(perp.budget, 75.f, 1e-3f));
	CHECK(Near(perp.along, 0.f, 1e-3f));
	CHECK(Near(perp.along_best, 0.f, 1e-4f));
	CHECK(Near(perp.gain, perp.gain_max, 1e-4f));
	CHECK(Near(perp.gain, 1.1234f, 1e-2f));

	// Same model at 125 fps: the budget drops below the target, so the optimum
	// moves off perpendicular. This is the frame-rate dependence the vanilla
	// model does not have, and it is why air_accel is stored per sample.
	jump::strafe_frame_t fast =
		jump::StrafeFrame(still, 0.f, 90.f, 0.f, 400.f, 0.f, 0.008f, false, 10);
	CHECK(Near(fast.budget, 24.f, 1e-3f));
	CHECK(fast.along_best > 5.f);
}

static void TestClassifyStrafeFrame()
{
	const jump::move_sample_t clean = MakeAirSample(0, 400.f, 0.f, 0.f, 400.f, 0.f, 25);
	CHECK(jump::ClassifyStrafeFrame(clean) == jump::strafe_frame_kind_t::usable);

	// Every exclusion, one field at a time.
	jump::move_sample_t s = clean;
	s.predicted = false;
	CHECK(jump::ClassifyStrafeFrame(s) == jump::strafe_frame_kind_t::excluded);

	s = clean;
	s.inputs_valid = false;
	CHECK(jump::ClassifyStrafeFrame(s) == jump::strafe_frame_kind_t::excluded);

	s = clean;
	s.msec = 0;
	CHECK(jump::ClassifyStrafeFrame(s) == jump::strafe_frame_kind_t::excluded);

	s = clean;
	s.discontinuity = true;
	CHECK(jump::ClassifyStrafeFrame(s) == jump::strafe_frame_kind_t::excluded);

	s = clean;
	s.pm_normal = false;
	CHECK(jump::ClassifyStrafeFrame(s) == jump::strafe_frame_kind_t::excluded);

	// Ground at either end of the command: friction ran, or the slide move put
	// us on the floor partway through.
	s = clean;
	s.on_ground_entry = true;
	CHECK(jump::ClassifyStrafeFrame(s) == jump::strafe_frame_kind_t::excluded);

	s = clean;
	s.on_ground = true;
	CHECK(jump::ClassifyStrafeFrame(s) == jump::strafe_frame_kind_t::excluded);

	s = clean;
	s.jumped = true;
	CHECK(jump::ClassifyStrafeFrame(s) == jump::strafe_frame_kind_t::excluded);

	s = clean;
	s.on_ladder = true;
	CHECK(jump::ClassifyStrafeFrame(s) == jump::strafe_frame_kind_t::excluded);

	s = clean;
	s.water_level = 1;
	CHECK(jump::ClassifyStrafeFrame(s) == jump::strafe_frame_kind_t::excluded);

	s = clean;
	s.timed_move = true;
	CHECK(jump::ClassifyStrafeFrame(s) == jump::strafe_frame_kind_t::excluded);

	// Exact, but nothing to capture.
	s = clean;
	s.forwardmove = 0.f;
	s.sidemove = 0.f;
	CHECK(jump::ClassifyStrafeFrame(s) == jump::strafe_frame_kind_t::no_opportunity);

	// Ice. PM_Friction's drop on a slick surface is exactly zero, so a ground
	// frame there is as exact as an airborne one and IS graded - which is the
	// whole point: an ice slide is a sustained strafing phase, and excluding it
	// left the bar frozen and then blank for as long as the player was on it.
	s = clean;
	s.on_ground_entry = true;
	s.on_ground = true;
	s.on_slick = true;
	CHECK(jump::ClassifyStrafeFrame(s) == jump::strafe_frame_kind_t::usable);

	// Slick is only ever an excuse for GROUND. Every other exclusion still bites.
	for (int which = 0; which < 4; which++)
	{
		s = clean;
		s.on_ground_entry = true;
		s.on_ground = true;
		s.on_slick = true;

		switch (which)
		{
		case 0:
			// The takeoff stroke off an ice brush: PM_CheckJump clears
			// groundentity partway through, so the accel runs in the AIR branch
			// from a frame that started on the floor. Neither model fits.
			s.jumped = true;
			break;
		case 1:
			s.on_ladder = true;
			break;
		case 2:
			s.water_level = 1;
			break;
		case 3:
			s.timed_move = true;
			break;
		}

		CHECK(jump::ClassifyStrafeFrame(s) == jump::strafe_frame_kind_t::excluded);
	}

	// Landing on ice from the air: on_slick describes the ground the command
	// STARTED on, and this one started airborne. Still excluded, because the
	// slide move put the player on the floor partway through.
	s = clean;
	s.on_ground = true;
	s.on_slick = false;
	CHECK(jump::ClassifyStrafeFrame(s) == jump::strafe_frame_kind_t::excluded);
}

// The ground acceleration model, which ice frames are graded against. It is not
// the air model with a different number: PM_WalkMove hands the full wishspeed
// straight to PM_Accelerate, so the 30-unit air target never applies.
static void TestStrafeFrameOnGround()
{
	const float vel[2] = { 400.f, 0.f };

	// Air, with the 30-clamp model selected (airaccel > 0).
	const jump::strafe_frame_t air =
		jump::StrafeFrame(vel, 0.f, 0.f, 0.f, 0.f, 400.f, 0.025f, false, 30, false);

	// Same command, on frictionless ground.
	const jump::strafe_frame_t ground =
		jump::StrafeFrame(vel, 0.f, 0.f, 0.f, 0.f, 400.f, 0.025f, false, 30, true);

	// The air target is clamped to PM_AIR_TARGET; the ground target is the full
	// wishspeed, itself clamped only to pm_maxspeed.
	CHECK(air.target == jump::PM_AIR_TARGET);
	CHECK(ground.target == ground.wishspeed);
	CHECK(ground.target > air.target);

	// budget = accel * wishspeed * dt, and ground accel is 10 regardless of what
	// sv_airaccelerate happens to be.
	CHECK(Near(ground.budget, jump::PM_GROUND_ACCEL * ground.wishspeed * 0.025f, 0.001f));

	// A bigger target and a bigger budget mean there is more on offer, so the
	// bar is measuring against a genuinely different maximum.
	CHECK(ground.gain_max > air.gain_max);
}

static void TestStrafeState()
{
	jump::move_ring_t	 ring;
	jump::strafe_state_t state;

	CHECK(!state.Update(ring).valid);

	// A run of perfect frames converges on 1.0 and reports no error either way.
	jump::strafe_readout_t out;

	for (int i = 0; i < 10; i++)
	{
		ring.Push(MakeAirSample((uint64_t) i * 25, 100.f, 0.f, 0.f, 400.f, 0.f, 25));
		out = state.Update(ring);
	}

	CHECK(out.valid);
	CHECK(Near(out.efficiency, 1.f, 1e-3f));
	CHECK(Near(out.offset, 0.f, 1e-3f));

	// Then a run capturing nothing: the reading falls away rather than
	// snapping, and the sign says which way the error went.
	for (int i = 10; i < 40; i++)
	{
		ring.Push(MakeAirSample((uint64_t) i * 25, 400.f, 0.f, 0.f, 400.f, 0.f, 25));
		out = state.Update(ring);
	}

	CHECK(out.valid);
	CHECK(out.efficiency < 0.15f);
	CHECK(out.offset < -0.5f); // aiming along the velocity: turning too little

	// Ground frames offer nothing to measure, so the reading is held rather than
	// recomputed. A brief touch of ground must NOT blank the bar: a bunny-hop
	// chain crosses the ground twice a second, and an element that vanishes at
	// 2 Hz is worse than one that holds its last reading.
	const float held = out.efficiency;
	int			i = 40;

	for (int hop = 0; hop < 4; hop++, i++)
	{
		jump::move_sample_t ground = MakeAirSample((uint64_t) i * 25, 300.f, 0.f, 0.f, 400.f, 0.f, 25);
		ground.on_ground_entry = true;
		ground.on_ground = true;
		ring.Push(ground);
		out = state.Update(ring);
	}

	CHECK(out.valid);
	CHECK(Near(out.efficiency, held, 1e-4f)); // held, not recomputed

	// Standing still does blank it, and on a timer rather than by decay. Decay
	// alone never could: it scales numerator and denominator alike, so the ratio
	// would sit at its last value indefinitely instead of going away.
	for (; i < 80; i++)
	{
		jump::move_sample_t ground = MakeAirSample((uint64_t) i * 25, 300.f, 0.f, 0.f, 400.f, 0.f, 25);
		ground.on_ground_entry = true;
		ground.on_ground = true;
		ring.Push(ground);
		out = state.Update(ring);
	}

	CHECK(!out.valid);

	// Airborne with no keys held is the same case: nothing on offer, so the
	// reading goes rather than freezing.
	ring.Clear();
	state.Reset();

	for (int j = 0; j < 10; j++)
	{
		ring.Push(MakeAirSample((uint64_t) j * 25, 100.f, 0.f, 0.f, 400.f, 0.f, 25));
		out = state.Update(ring);
	}

	CHECK(out.valid);

	for (int j = 10; j < 50; j++)
	{
		ring.Push(MakeAirSample((uint64_t) j * 25, 100.f, 0.f, 0.f, 0.f, 0.f, 25));
		out = state.Update(ring);
	}

	CHECK(!out.valid);

	// A teleport resets the accumulator rather than averaging across it.
	ring.Clear();
	state.Reset();

	for (int i = 0; i < 10; i++)
	{
		ring.Push(MakeAirSample((uint64_t) i * 25, 100.f, 0.f, 0.f, 400.f, 0.f, 25));
		state.Update(ring);
	}

	jump::move_sample_t ported = MakeAirSample(250, 400.f, 0.f, 0.f, 400.f, 0.f, 25);
	ported.discontinuity = true;
	ring.Push(ported);
	out = state.Update(ring);

	// Only that frame is in the average now, so the smoothed value is its own
	// raw ratio - which for this input is zero.
	CHECK(Near(out.efficiency, out.frame, 1e-4f));

	// Frame-rate independence: the same movement sampled at 8 ms and at 25 ms
	// over the same wall-clock span must converge on the same reading. This is
	// what the ratio-of-sums smoothing buys, and it is worth enforcing rather
	// than asserting in a comment.
	jump::move_ring_t	 slow_ring, fast_ring;
	jump::strafe_state_t slow_state, fast_state;
	jump::strafe_readout_t slow_out, fast_out;

	for (uint64_t t = 0; t <= 1000; t += 25)
	{
		slow_ring.Push(MakeAirSample(t, 400.f, 0.f, 0.f, 400.f, 100.f, 25));
		slow_out = slow_state.Update(slow_ring);
	}

	for (uint64_t t = 0; t <= 1000; t += 8)
	{
		fast_ring.Push(MakeAirSample(t, 400.f, 0.f, 0.f, 400.f, 100.f, 8));
		fast_out = fast_state.Update(fast_ring);
	}

	CHECK(slow_out.valid && fast_out.valid);
	CHECK(Near(slow_out.efficiency, fast_out.efficiency, 0.02f));
}

// The angle between your velocity and the wishdir that would gain the most, in
// degrees. Everything the meter depends on is a function of this angle and your
// speed - the absolute compass direction never enters the maths - so a strafing
// technique can be expressed purely as the path this angle takes over time.
static float OptimalWishAngle(float speed, float target, float budget)
{
	const float best = jump::BestAlong(speed, target, budget);
	float		c = best / speed;

	if (c > 1.f)
		c = 1.f;
	if (c < -1.f)
		c = -1.f;

	return acosf(c) * 180.f / 3.14159265358979323846f;
}

// A sample whose WISHDIR sits `wish_deg` off the velocity, produced by a chosen
// set of keys. Velocity runs along +x, so the yaw needed to land the wishdir on
// a given angle depends on which keys are held - which is exactly the point.
enum class strafe_keys_t
{
	side,	  // pure +moveright: the ordinary strafe
	forward,  // pure +forward
	backward, // pure +back, and nothing else
	diagonal  // +forward and +moveright together
};

static jump::move_sample_t MakeWishSample(uint64_t time_ms, float speed, float wish_deg, strafe_keys_t keys,
										  uint8_t msec, int air_accel = 0)
{
	float yaw = 0.f;
	float fmove = 0.f;
	float smove = 0.f;

	switch (keys)
	{
	// right.xy is (sin yaw, -cos yaw), so its heading is yaw - 90.
	case strafe_keys_t::side:
		yaw = wish_deg + 90.f;
		smove = 400.f;
		break;

	// forward.xy points along yaw; pitch only scales it, never turns it.
	case strafe_keys_t::forward:
		yaw = wish_deg;
		fmove = 400.f;
		break;

	// Backwards is the same heading reached from the opposite view. Sata's
	// case: the keys cannot matter, only where the wish ends up pointing.
	case strafe_keys_t::backward:
		yaw = wish_deg - 180.f;
		fmove = -400.f;
		break;

	// Equal parts of both puts the wish 45 degrees off the view.
	case strafe_keys_t::diagonal:
		yaw = wish_deg + 45.f;
		fmove = 400.f;
		smove = 400.f;
		break;
	}

	return MakeAirSample(time_ms, speed, 0.f, yaw, fmove, smove, msec, air_accel);
}

static jump::strafe_frame_t FrameAt(float speed, float wish_deg, strafe_keys_t keys, uint8_t msec,
									int air_accel = 0)
{
	const jump::move_sample_t s = MakeWishSample(0, speed, wish_deg, keys, msec, air_accel);
	const float				  vel[2] = { s.vel_before[0], s.vel_before[1] };

	return jump::StrafeFrame(vel, s.view_pitch, s.view_yaw, s.view_roll, s.forwardmove, s.sidemove,
							 (float) s.msec / 1000.f, s.ducked, s.air_accel);
}

// Which keys produced the input must not change the reading.
//
// Raised by a player testing the meter: "it is possible to strafe forward with
// only +back with same efficiency as normal strafing, if you can move mouse that
// fast". True, and the meter has to agree - it grades where the wish pointed,
// not how you got there.
static void TestStrafeKeysDoNotMatter()
{
	const strafe_keys_t all[] = { strafe_keys_t::side, strafe_keys_t::forward, strafe_keys_t::backward,
								  strafe_keys_t::diagonal };

	for (const float speed : { 320.f, 400.f, 700.f, 1200.f })
	{
		for (const int accel : { 0, 10 })
		{
			for (const uint8_t msec : { (uint8_t) 8, (uint8_t) 25 })
			{
				// Read the optimum off the first frame rather than assuming it:
				// it moves with speed, frame time and the air model.
				const jump::strafe_frame_t probe = FrameAt(speed, 0.f, strafe_keys_t::side, msec, accel);
				const float best_deg = OptimalWishAngle(speed, probe.target, probe.budget);

				for (const strafe_keys_t keys : all)
				{
					// On the optimum, from every key combination.
					const jump::strafe_frame_t on = FrameAt(speed, best_deg, keys, msec, accel);
					CHECK(on.opportunity);
					CHECK(Near(on.gain, on.gain_max, on.gain_max * 0.01f));

					// And off it by the same amount, so a technique that reaches
					// the angle late is marked down identically whatever the keys.
					const jump::strafe_frame_t off = FrameAt(speed, best_deg + 15.f, keys, msec, accel);
					const jump::strafe_frame_t ref =
						FrameAt(speed, best_deg + 15.f, strafe_keys_t::side, msec, accel);
					CHECK(Near(off.gain, ref.gain, 1e-2f));
				}
			}
		}
	}
}

// Walk a technique's angle path and report what the smoothed bar would show.
//
// `sweep_deg` is how far either side of the optimum the mouse travels, and
// `beats` how many times it crosses in one airtime. Speed is advanced by the
// gain each frame, so the optimum drifts underneath the player exactly as it
// does in play.
static float RunTechnique(float speed, float sweep_deg, int beats, int frames, uint8_t msec, int air_accel = 0)
{
	jump::strafe_state_t   state;
	jump::strafe_readout_t out;

	for (int i = 0; i < frames; i++)
	{
		const jump::strafe_frame_t probe = FrameAt(speed, 0.f, strafe_keys_t::side, msec, air_accel);
		const float				   best_deg = OptimalWishAngle(speed, probe.target, probe.budget);

		// A triangle wave through the optimum: the mouse passes it, overshoots
		// by sweep_deg, and comes back.
		const float phase = beats > 0 ? (float) (i * beats) / (float) frames : 0.f;
		const float saw = phase - (float) (int) phase; // 0..1
		const float tri = saw < 0.5f ? (saw * 4.f - 1.f) : (3.f - saw * 4.f);

		const jump::strafe_frame_t f =
			FrameAt(speed, best_deg + sweep_deg * tri, strafe_keys_t::side, msec, air_accel);

		out = state.Add(f, true, msec);

		if (f.gain > 0.f)
			speed += f.gain;
	}

	return out.valid ? out.efficiency : -1.f;
}

// The finding behind "I'm doing perfect strafes and sometimes bar is empty and
// sometimes full".
//
// It is not a bug in the maths - every frame above grades correctly. It is the
// shape of vanilla Q2 air acceleration: the best projection is a budget short of
// the target, so the optimum sits exactly ON the boundary between gaining and
// losing, and the budget is proportional to frame time. Cross it and you do not
// merely capture less, you capture nothing.
//
// The consequence is an asymmetry of roughly thirty to one, and it gets worse
// the higher your frame rate. A player riding the optimum is riding a knife
// edge; a player sitting a few degrees wide gets a steady, high reading. Both
// facts below are true of the game, not of the meter - but they are why the
// readout looks broken to the players best equipped to sit on the line.
static void TestStrafeCliffAsymmetry()
{
	const float speed = 400.f;

	for (const uint8_t msec : { (uint8_t) 8, (uint8_t) 25 })
	{
		const jump::strafe_frame_t probe = FrameAt(speed, 0.f, strafe_keys_t::side, msec);
		const float				   best_deg = OptimalWishAngle(speed, probe.target, probe.budget);

		// Full marks on the line.
		const jump::strafe_frame_t on = FrameAt(speed, best_deg, strafe_keys_t::side, msec);
		CHECK(Near(on.gain, on.gain_max, on.gain_max * 0.01f));

		// Narrow side: find where it reaches zero.
		float narrow_zero = 0.f;

		for (float d = 0.f; d < 20.f; d += 0.05f)
		{
			if (FrameAt(speed, best_deg - d, strafe_keys_t::side, msec).gain <= 0.f)
			{
				narrow_zero = d;
				break;
			}
		}

		// Wide side: the same.
		float wide_zero = 0.f;

		for (float d = 0.f; d < 90.f; d += 0.05f)
		{
			if (FrameAt(speed, best_deg + d, strafe_keys_t::side, msec).gain <= 0.f)
			{
				wide_zero = d;
				break;
			}
		}

		// Measured: at 25 ms the optimum is 43.01 deg and the gain reaches zero
		// 1.60 deg narrow of it against 47.55 deg wide - a ratio of 30. At 8 ms
		// the budget is a third the size, and the narrow margin collapses to
		// 0.55 deg against 48.25 - a ratio of 88.
		CHECK(narrow_zero > 0.f && wide_zero > 0.f);
		CHECK(wide_zero > narrow_zero * 10.f); // the asymmetry, conservatively

		// And it tightens with frame time: the budget shrinks, so the window
		// between "everything" and "nothing" shrinks with it.
		if (msec == 8)
			CHECK(narrow_zero < 1.f); // about half a degree at 125 fps
		else
			CHECK(narrow_zero < 2.5f); // about 1.6 degrees at 40 Hz
	}

	// The perverse consequence, stated as a test so it cannot change silently:
	// oscillating across the optimum scores WORSE than sitting steadily wide of
	// it. That is honest - the narrow excursions really do cost speed - but it
	// means precision reads as noise while a safe, slightly wide line reads
	// clean.
	const float riding = RunTechnique(400.f, 1.f, 4, 60, 8);
	const float wide = RunTechnique(400.f, 0.f, 0, 60, 8);

	// Measured at 125 fps: a one-degree wobble across the line reads 61%, while
	// sitting still on it reads 100%. One degree of mouse noise is nothing, and
	// it costs almost forty points of bar.
	CHECK(riding >= 0.f && wide >= 0.f);
	CHECK(wide > riding);

	// Whatever the beat pattern, a technique that keeps the wish on the optimal
	// side of the line reads high. This is the check that would have caught a
	// real half-beat or zero-beat bug: the meter must not care how many times
	// the angle is crossed per airtime, only where it sits.
	for (const int beats : { 0, 1, 2, 4, 8 })
	{
		// Sweeping entirely on the wide side of the optimum, which is what a
		// controlled strafe of any beat count actually does.
		jump::strafe_state_t   state;
		jump::strafe_readout_t out;
		float				   speed = 400.f;

		for (int i = 0; i < 60; i++)
		{
			const jump::strafe_frame_t probe = FrameAt(speed, 0.f, strafe_keys_t::side, 8);
			const float				   best_deg = OptimalWishAngle(speed, probe.target, probe.budget);
			const float				   phase = beats > 0 ? (float) (i * beats) / 60.f : 0.f;
			const float				   saw = phase - (float) (int) phase;
			const float				   tri = saw < 0.5f ? saw * 2.f : (1.f - saw) * 2.f;

			const jump::strafe_frame_t f = FrameAt(speed, best_deg + 0.5f + tri * 2.f, strafe_keys_t::side, 8);

			out = state.Add(f, true, 8);

			if (f.gain > 0.f)
				speed += f.gain;
		}

		// Measured: 99% at zero beats and 97% at one, two, four and eight. The
		// beat count does not register at all - only which side of the line the
		// wish sits on. Whatever a half-beat or zero-beat player is seeing, it
		// is not the meter mishandling their technique.
		CHECK(out.valid);
		CHECK(out.efficiency > 0.9f);
	}
}

// `speed` on a teleporter is a minimum-speed gate, as in upstream q2jump. The
// map that found this, 4kv3, spawns the player inside a track-wide teleport
// gated at 4000 ups - so "no key means no gate" is what keeps every ordinary
// teleporter in the corpus behaving as it always has, and the gate itself is
// what keeps a speed map from finishing itself on frame one.
static void TestTeleportSpeedGate()
{
	// No gate: an absent key parses to 0, and 0 must never block.
	CHECK(jump::TeleportSpeedAllows(0.f, 0.f));
	CHECK(jump::TeleportSpeedAllows(1200.f, 0.f));

	// A negative speed is reachable - the key is free text - and upstream treats
	// it as no gate rather than as a gate nobody can fail.
	CHECK(jump::TeleportSpeedAllows(0.f, -100.f));

	// The gate itself, with the boundary inclusive: exactly the required speed
	// passes, as it does upstream, so a map asking for 4000 is satisfied by 4000.
	CHECK(!jump::TeleportSpeedAllows(3999.f, 4000.f));
	CHECK(jump::TeleportSpeedAllows(4000.f, 4000.f));
	CHECK(jump::TeleportSpeedAllows(4001.f, 4000.f));

	// 4kv3 in miniature: the spawn velocity is 3200 and the gate is 4000, so the
	// teleport the player is standing in must refuse them until they have earned
	// the other 800.
	CHECK(!jump::TeleportSpeedAllows(3200.f, 4000.f));
}

// The speedometer frozen at the last takeoff.
static void TestTakeoffSpeed()
{
	jump::jump_takeoff_state_t t;

	// n frames of a given ground state at 25 ms each. Only safe to CONTINUE an
	// air phase - its first frame after ground would be a takeoff, and it passes
	// no speed - so every takeoff below is an explicit Update.
	auto run = [&](bool on_ground, int frames) {
		for (int i = 0; i < frames; i++)
			t.Update(on_ground, 0.f, 25);
	};

	// Joining the game: the very first frames are not a takeoff. `airborne`
	// starts false, meaning "was on the ground", so without the have_ground gate
	// the first airborne frame counts as leaving it - and a player who has not
	// moved yet is doing 0, which is what put a "0" above the speedometer the
	// instant anyone joined.
	t.Reset();
	CHECK(!t.Update(false, 0.f, 25));
	CHECK(!t.have_speed);
	run(false, 10);
	CHECK(!t.have_speed);

	// Nothing to show until the feet leave the ground the first time.
	t.Reset();
	run(true, 2);
	CHECK(!t.have_speed);

	// Takeoff freezes the speedometer, and it holds all through the flight - that
	// is what makes it a mark to compare the live number against.
	CHECK(t.Update(false, 420.4f, 25));
	CHECK(t.have_speed);
	CHECK(t.speed == 420);

	run(false, 20);
	CHECK(t.speed == 420); // unchanged by anything that happens in the air

	// The next takeoff replaces it.
	run(true, 1);
	CHECK(t.Update(false, 445.f, 25));
	CHECK(t.speed == 445);

	// Rounded the way the live speedometer rounds, so the two agree at the
	// instant of takeoff rather than differing by one.
	run(true, 1);
	t.Update(false, 449.6f, 25);
	CHECK(t.speed == 449);

	// Stand about and the chain is over: the mark clears rather than hanging над
	// the live number from a run already abandoned.
	run(true, (int) (jump::JUMP_TAKEOFF_CHAIN_BREAK_MS / 25) + 1);
	CHECK(!t.have_speed);
	CHECK(t.speed == 0);

	// Hopping on the spot is not a mark. Nor is being put somewhere by a recall
	// or a teleport, both of which zero the velocity - the old behaviour showed
	// "0" for those too.
	t.Reset();
	run(true, 2);
	CHECK(!t.Update(false, 0.f, 25));
	CHECK(!t.have_speed);

	run(true, 2);
	CHECK(!t.Update(false, jump::JUMP_TAKEOFF_MIN_SPEED - 1.f, 25));
	CHECK(!t.have_speed);

	// And the threshold is a floor, not a band: at it, the mark is taken.
	run(true, 2);
	CHECK(t.Update(false, jump::JUMP_TAKEOFF_MIN_SPEED, 25));
	CHECK(t.have_speed);

	// Ground time resets at each takeoff, so an ordinary hop chain never
	// accumulates its way to the break however long it runs.
	t.Reset();
	t.Update(false, 400.f, 25);
	run(true, 10);
	CHECK(t.Update(false, 410.f, 25));
	run(false, 4);
	run(true, 10);
	CHECK(t.Update(false, 425.f, 25));
	CHECK(t.have_speed);
	CHECK(t.speed == 425);
}

static void TestCgaz()
{
	// No keys held: nothing to steer, so nothing to draw. A strip built from an
	// assumed key combination would be a guess dressed up as guidance.
	jump::move_sample_t idle = MakeAirSample(0, 400.f, 0.f, 0.f, 0.f, 0.f, 8);
	CHECK(!jump::CgazFromSample(idle).valid);

	// Standing still has no heading to be an angle from.
	jump::move_sample_t still = MakeAirSample(0, 0.f, 0.f, 0.f, 400.f, 0.f, 8);
	CHECK(!jump::CgazFromSample(still).valid);

	// The headline geometry, at 400 ups on vanilla air. Velocity along +x, pure
	// forward input, so the wish already points along the velocity and `base` is
	// zero - the strip is centred on the angle that earns nothing.
	jump::move_sample_t s = MakeAirSample(0, 400.f, 0.f, 0.f, 400.f, 0.f, 25);
	jump::cgaz_readout_t c = jump::CgazFromSample(s);

	CHECK(c.valid);
	CHECK(Near(c.base, 0.f, 1e-3f));

	// acos(300/400) = 41.41, acos(292.5/400) = 43.01, acos(-7.5/800) = 90.54.
	CHECK(Near(c.zone_inner, 41.41f, 0.05f));
	CHECK(Near(c.optimal, 43.01f, 0.05f));
	CHECK(Near(c.zone_outer, 90.54f, 0.05f));

	// The optimum lives just outside the near edge, not in the middle of the
	// zone - which is the whole reason a Q2 strip reads differently from a Q3
	// one, and why the useful thing to draw is the inner edge.
	CHECK(c.optimal > c.zone_inner);
	CHECK(c.optimal - c.zone_inner < 2.f);
	CHECK(c.zone_outer - c.optimal > 40.f);

	// Pointing straight down the velocity is inside the dead wedge.
	CHECK(!c.inside);

	// Only one of the two optima is drawn: the one on the side the wish is
	// already on, which is what every other CGaz shows and what stops two bright
	// lines competing for the same meaning.
	//
	// Velocity 90 degrees LEFT of the view (base +90) means the wish sits 90
	// degrees clockwise of the velocity, so the solution to point at is 43
	// degrees back off the velocity on that same side: 90 - 43 = 47 degrees left.
	jump::cgaz_readout_t c_side_left = jump::CgazFromSample(MakeAirSample(0, 0.f, 400.f, 0.f, 400.f, 0.f, 25));

	CHECK(c_side_left.valid);
	CHECK(Near(c_side_left.base, 90.f, 0.05f));
	CHECK(Near(c_side_left.optimal_view, 90.f - c_side_left.optimal, 0.05f));
	CHECK(c_side_left.optimal_view > 0.f); // still on the left, where the velocity is

	// Mirrored, and the tick swaps sides with it.
	jump::cgaz_readout_t c_side_right =
		jump::CgazFromSample(MakeAirSample(0, 0.f, -400.f, 0.f, 400.f, 0.f, 25));

	CHECK(Near(c_side_right.base, -90.f, 0.05f));
	CHECK(Near(c_side_right.optimal_view, -90.f + c_side_right.optimal, 0.05f));
	CHECK(c_side_right.optimal_view < 0.f);

	// It is always one of the two solutions, never something in between.
	for (const jump::cgaz_readout_t &r : { c, c_side_left, c_side_right })
	{
		const bool plus = Near(r.optimal_view, r.base + r.optimal, 0.05f);
		const bool minus = Near(r.optimal_view, r.base - r.optimal, 0.05f);

		CHECK(plus != minus || r.optimal == 0.f);
	}

	// Turn to the optimum and the strip says so. Yaw and `base` move together:
	// the wish turns with the view.
	jump::move_sample_t on = MakeAirSample(0, 400.f, 0.f, c.optimal, 400.f, 0.f, 25);
	jump::cgaz_readout_t c_on = jump::CgazFromSample(on);

	CHECK(c_on.valid);
	CHECK(c_on.inside);
	CHECK(Near(std::fabs(c_on.base), c_on.optimal, 0.05f));

	// Half a degree inside the near edge is outside the zone, at 125 fps. This
	// is the wall the strafe meter can only report after the fact.
	jump::move_sample_t fast_in = MakeAirSample(0, 400.f, 0.f, 41.f, 400.f, 0.f, 8);
	CHECK(!jump::CgazFromSample(fast_in).valid == false); // valid, just not inside
	CHECK(!jump::CgazFromSample(fast_in).inside);

	// The zone is symmetric about the velocity: mirroring the view mirrors it.
	jump::move_sample_t left = MakeAirSample(0, 400.f, 0.f, -c.optimal, 400.f, 0.f, 25);
	jump::cgaz_readout_t c_left = jump::CgazFromSample(left);

	CHECK(c_left.inside);
	CHECK(Near(c_left.zone_inner, c.zone_inner, 1e-3f));
	CHECK(Near(c_left.optimal, c.optimal, 1e-3f));

	// Which keys produced the wish cannot matter, exactly as for the meter. Pure
	// side input at yaw+90 puts the wish where pure forward at yaw does.
	jump::move_sample_t side = MakeAirSample(0, 400.f, 0.f, c.optimal + 90.f, 0.f, 400.f, 25);
	jump::cgaz_readout_t c_side = jump::CgazFromSample(side);

	CHECK(c_side.inside);
	CHECK(Near(std::fabs(c_side.base), c.optimal, 0.05f));

	// Slow enough and there is no dead wedge at all - the target is above your
	// speed, so every angle from dead ahead outwards earns something. Beginners
	// genuinely cannot strafe wrong, which is worth the strip showing honestly.
	jump::move_sample_t slow = MakeAirSample(0, 100.f, 0.f, 0.f, 400.f, 0.f, 25);
	jump::cgaz_readout_t c_slow = jump::CgazFromSample(slow);

	CHECK(c_slow.valid);
	CHECK(Near(c_slow.zone_inner, 0.f, 1e-3f));
	CHECK(c_slow.inside);

	// The 30-clamp model puts the optimum near perpendicular instead, which is
	// the different technique the strip has to keep up with.
	jump::move_sample_t clamped = MakeAirSample(0, 400.f, 0.f, 0.f, 400.f, 0.f, 25, 10);
	jump::cgaz_readout_t c_clamped = jump::CgazFromSample(clamped);

	CHECK(c_clamped.valid);
	CHECK(c_clamped.optimal > 80.f);

	// And its far edge is set by the TARGET, not half the budget. Here budget is
	// 75 against a target of 30, so the bound is -30 and the edge is
	// acos(-30/400) = 94.30 degrees. Using -budget/2 would claim 95.38 and paint
	// a degree of braking green; halving the smaller of the two instead - the
	// obvious wrong fix - claims 92.15 and hides three degrees of real zone.
	// With a larger sv_airaccelerate the first mistake greens the whole strip.
	CHECK(Near(c_clamped.zone_outer, 94.30f, 0.1f));

	{
		const float			 outer_deg = c_clamped.zone_outer;
		const float			 rad = outer_deg * 3.14159265358979323846f / 180.f;
		const float			 along = 400.f * std::cos(rad);
		jump::strafe_frame_t probe =
			jump::StrafeFrame(clamped.vel_before, 0.f, 0.f, 0.f, 400.f, 0.f, 0.025f, false, 10);

		// The reported edge is where the gain crosses zero: just inside it pays,
		// just outside it costs.
		CHECK(jump::AccelGain(400.f, along + 1.f, probe.target, probe.budget) > 0.f);
		CHECK(jump::AccelGain(400.f, along - 1.f, probe.target, probe.budget) < 0.f);
	}

	// Refused only where the wish itself would not be what the keys said:
	// PM_AddCurrents rewrites it on ladders and in water, so the drawn angles
	// would point somewhere else entirely rather than merely be imprecise.
	for (int which = 0; which < 5; which++)
	{
		jump::move_sample_t bad = MakeAirSample(0, 400.f, 0.f, 0.f, 400.f, 0.f, 25);

		switch (which)
		{
		case 0: bad.on_ladder = true; break;
		case 1: bad.water_level = 2; break;
		case 2: bad.discontinuity = true; break;
		case 3: bad.pm_normal = false; break;
		// Chasing: the ground flag belongs to the viewer, so the branch cannot
		// be chosen and the strip would model the wrong one.
		case 4: bad.on_ground_valid = false; break;
		}

		CHECK(!jump::CgazFromSample(bad).valid);
	}

	// The GROUND is a branch, not a refusal. CGaz follows whatever pmove is
	// doing - a strip that blanked on every ground contact would be useless on a
	// hop chain, and pointing at the angles that speed you up is forgiving in a
	// way that grading a frame after the fact is not.
	//
	// The ground branch is PM_Accelerate with pm_accelerate and NO target clamp,
	// so at 400 ups the target is the full wishspeed of 300 and the budget is
	// 10 * 300 * 0.025 = 75 - a far bigger budget than the air model's 7.5, which
	// pulls the optimum in towards the velocity.
	jump::move_sample_t ground = MakeAirSample(0, 400.f, 0.f, 0.f, 400.f, 0.f, 25);
	ground.on_ground = true;
	ground.on_ground_entry = true;

	jump::cgaz_readout_t c_ground = jump::CgazFromSample(ground);

	CHECK(c_ground.valid);
	CHECK(Near(c_ground.zone_inner, 41.41f, 0.05f)); // acos(300/400), same target
	CHECK(Near(c_ground.optimal, 55.77f, 0.1f));	 // acos((300-75)/400)
	CHECK(c_ground.optimal > c.optimal);			 // a bigger budget opens the angle

	// And the air model is untouched by the new parameter.
	CHECK(Near(c.optimal, 43.01f, 0.05f));

	// The sign of `base`, which the drawing depends on and which nothing else
	// here can pin: every other field is a magnitude. Quake yaw counts
	// counter-clockwise, so a velocity at heading 90 with the view at 0 is off to
	// the player's LEFT, and base must come out POSITIVE. A drawing that maps
	// positive degrees rightwards mirrors the instrument and steering by it takes
	// you away from the zone.
	jump::move_sample_t vel_left = MakeAirSample(0, 0.f, 400.f, 0.f, 400.f, 0.f, 25);
	jump::cgaz_readout_t c_left_vel = jump::CgazFromSample(vel_left);

	CHECK(c_left_vel.valid);
	CHECK(Near(c_left_vel.base, 90.f, 0.05f));

	jump::move_sample_t vel_right = MakeAirSample(0, 0.f, -400.f, 0.f, 400.f, 0.f, 25);
	jump::cgaz_readout_t c_right_vel = jump::CgazFromSample(vel_right);

	CHECK(c_right_vel.valid);
	CHECK(Near(c_right_vel.base, -90.f, 0.05f));

	// base agrees with the meter's own projection: cos(base) * speed is `along`.
	const jump::strafe_frame_t f = jump::StrafeFrame(s.vel_before, 0.f, 20.f, 0.f, 400.f, 0.f, 0.025f, false, 0);
	jump::move_sample_t		   at20 = MakeAirSample(0, 400.f, 0.f, 20.f, 400.f, 0.f, 25);
	jump::cgaz_readout_t	   c20 = jump::CgazFromSample(at20);

	CHECK(Near(400.f * std::cos(c20.base * 3.14159265358979323846f / 180.f), f.along, 0.5f));

	// Wrapping, which the drawing leans on to clip the zones to the strip.
	CHECK(Near(jump::WrapDegrees(190.f), -170.f, 1e-3f));
	CHECK(Near(jump::WrapDegrees(-190.f), 170.f, 1e-3f));
	CHECK(Near(jump::WrapDegrees(540.f), 180.f, 1e-3f));
	CHECK(Near(jump::WrapDegrees(0.f), 0.f, 1e-3f));
}

static void TestStrafeBarLevel()
{
	jump::strafe_readout_t out;

	// Nothing measured: no bar at all, which the caller turns into a zeroed
	// stat so the row is gated off entirely.
	CHECK(jump::StrafeBarLevel(out) == -1);

	out.valid = true;

	// Fill is EFFICIENCY - full means you took everything on offer. The
	// health-bar token this replaced could only draw red, which forced the
	// opposite reading; text does not, so the bar runs the way every other
	// bar anyone has seen does.
	out.efficiency = 0.f;
	CHECK(jump::StrafeBarLevel(out, 12) == 0);

	out.efficiency = 1.f;
	CHECK(jump::StrafeBarLevel(out, 12) == 12);

	out.efficiency = 0.5f;
	CHECK(jump::StrafeBarLevel(out, 12) == 6);

	// Out-of-range input cannot escape the bar.
	out.efficiency = 2.f;
	CHECK(jump::StrafeBarLevel(out, 12) == 12);

	out.efficiency = -1.f;
	CHECK(jump::StrafeBarLevel(out, 12) == 0);

	// Every level has a string, and they are all the same width - a bar that
	// changed length as it filled would shift the whole HUD row.
	const size_t width = jump::StrafeBarString(0, 12).size();

	CHECK(width == 14); // 12 cells plus the two brackets

	for (int i = 0; i <= 12; i++)
		CHECK(jump::StrafeBarString(i, 12).size() == width);

	CHECK_EQ(jump::StrafeBarString(0, 12), "[------------]");
	CHECK_EQ(jump::StrafeBarString(12, 12), "[############]");
	CHECK_EQ(jump::StrafeBarString(4, 12), "[####--------]");

	// Out-of-range levels clamp rather than running off the end.
	CHECK_EQ(jump::StrafeBarString(-3, 12), "[------------]");
	CHECK_EQ(jump::StrafeBarString(99, 12), "[############]");

	// The dead-side family: same fill, same glyph count, and one mark at the edge
	// of what was captured. Equal length is not cosmetic - the row is drawn with a
	// centred token, so a longer string would shift the bar sideways every time the
	// mark appeared, which is precisely when the player is crossing the line and
	// least wants the thing moving. Marking every empty cell instead was tried and
	// was unreadable.
	CHECK_EQ(jump::StrafeBarString(4, 12, true), "[####!-------]");
	CHECK_EQ(jump::StrafeBarString(0, 12, true), "[!-----------]");

	// A full bar cannot be dead-side - |offset| <= 1 - efficiency caps a marked bar
	// at three quarters - but if it ever were, there is no cell past the fill to
	// mark and the string must still be the right width.
	CHECK_EQ(jump::StrafeBarString(12, 12, true), "[############]");

	for (int i = 0; i <= 12; i++)
		CHECK(jump::StrafeBarString(i, 12, true).size() == width);

	// Nothing measured is never the dead side - there is no error to have a
	// direction, and the row is gated off anyway.
	jump::strafe_readout_t blank;
	CHECK(!jump::StrafeBarDeadSide(blank));

	// Wide of the line is the safe error and stays unmarked, however much is
	// being wasted. Narrow of it is the wall, and gets the marker.
	out.efficiency = 0.3f;
	out.offset = 0.7f; // over-turning: wide
	CHECK(!jump::StrafeBarDeadSide(out));

	out.offset = -0.7f; // under-turning: inside the line
	CHECK(jump::StrafeBarDeadSide(out));

	// A deadband, not a sign test. A reading hovering either side of perfect
	// must not strobe the marker - which would land hardest on the players
	// riding the optimum, who are the ones it is meant to help.
	out.offset = -0.05f;
	CHECK(!jump::StrafeBarDeadSide(out));

	out.offset = jump::STRAFE_DEAD_OFFSET;
	CHECK(jump::StrafeBarDeadSide(out));

	// The two families have to fit the configstring block they share.
	CHECK(2 * (jump::STRAFE_BAR_SEGMENTS + 1) <= 32);
	CHECK(jump::STRAFE_BAR_DEAD_OFFSET == jump::STRAFE_BAR_SEGMENTS + 1);

	// The fill and the marker are chosen by two different smoothed numbers, so
	// it is worth knowing they cannot contradict each other. |offset| can never
	// exceed 1 - efficiency, because the signed loss is built from the same
	// per-frame shortfalls the efficiency is missing - so a bar cannot be nearly
	// full AND marked dead. At the -0.25 threshold the fullest it can be is 75%.
	{
		jump::move_ring_t	 ring;
		jump::strafe_state_t state;
		int					 worst_dead_level = -1;

		// Alternate perfect frames with dead ones - the mixed case that produces
		// a mid bar and a dead lean at the same time.
		for (int i = 0; i < 80; i++)
		{
			const bool dead_frame = (i % 2) == 0;
			// 400 fwd at 100 ups is optimal; the same input at 400 ups is inside
			// the line and pays nothing.
			ring.Push(MakeAirSample((uint64_t) i * 25, dead_frame ? 400.f : 100.f, 0.f, 0.f, 400.f, 0.f, 25));

			const jump::strafe_readout_t r = state.Update(ring);

			if (!r.valid)
				continue;

			CHECK(std::fabs(r.offset) <= 1.f - r.efficiency + 1e-3f);

			if (jump::StrafeBarDeadSide(r))
			{
				const int level = jump::StrafeBarLevel(r);

				if (level > worst_dead_level)
					worst_dead_level = level;
			}
		}

		// The mix does reach the marker, and never with a bar that looks healthy.
		CHECK(worst_dead_level >= 0);
		CHECK(worst_dead_level <= 9); // 0.75 * 12, rounded
	}
}

static void TestSpeedDigits()
{
	int d[jump::SPEED_DIGITS];

	// Leading zeros are blanked (-1), so 320 draws as three digits rather than
	// 0320 - but the units digit always draws, so a standstill reads "0".
	jump::SpeedDigits(0, d);
	CHECK(d[0] == -1 && d[1] == -1 && d[2] == -1 && d[3] == 0);

	jump::SpeedDigits(7, d);
	CHECK(d[0] == -1 && d[1] == -1 && d[2] == -1 && d[3] == 7);

	jump::SpeedDigits(320, d);
	CHECK(d[0] == -1 && d[1] == 3 && d[2] == 2 && d[3] == 0);

	jump::SpeedDigits(1234, d);
	CHECK(d[0] == 1 && d[1] == 2 && d[2] == 3 && d[3] == 4);

	// An interior zero is a real digit, not a leading one.
	jump::SpeedDigits(408, d);
	CHECK(d[0] == -1 && d[1] == 4 && d[2] == 0 && d[3] == 8);

	// Clamped to what four cells can show, and never negative.
	jump::SpeedDigits(99999, d);
	CHECK(d[0] == 9 && d[1] == 9 && d[2] == 9 && d[3] == 9);

	jump::SpeedDigits(-50, d);
	CHECK(d[0] == -1 && d[1] == -1 && d[2] == -1 && d[3] == 0);
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
	TestMoveAxes();
	TestAccelGain();
	TestBestAlong();
	TestStrafeFrame();
	TestClassifyStrafeFrame();
	TestStrafeFrameOnGround();
	TestStrafeState();
	TestStrafeKeysDoNotMatter();
	TestStrafeCliffAsymmetry();
	TestTeleportSpeedGate();
	TestTakeoffSpeed();
	TestCgaz();
	TestStrafeBarLevel();
	TestSpeedDigits();
	TestRecords();
	TestJumpersPolicy();
	TestSortPlayerRows();

	printf("%d checks, %d failures\n", g_checks, g_failures);

	return g_failures == 0 ? 0 : 1;
}
