// [Jump] The main options menu (restart, teams, extend, vote map), opened by
// the `inven` command and unprompted on join. Map voting lives in a submenu;
// an active vote opens the yes/no cast UI.
//
// Built on the stock PMenu system (ctf/p_ctf_menu.h), which the engine already
// drives for us: invnext/invprev move the cursor, invuse selects, and inven
// closes. Submenus follow the MuffMode pattern: close, then open the next menu.
//
// Menu entries are copied into the handle by PMenu_Open, so the update
// function rewrites hnd->entries in place each refresh. The map name for a row
// is stashed in that row's text_arg1, which gives every entry a small data
// payload without needing a parallel array - the same trick MuffMode uses.

#include "../g_local.h"
#include "jump_local.h"
#include "jump_version.h"

#include <string>
#include <vector>

constexpr int JUMP_MENU_ENTRIES = 18;

// Rows 0-1 are the title block, the last row is Return/Close, and two rows
// above that are the pager. Everything between is maps.
constexpr int JUMP_MENU_FIRST_MAP = 2;
constexpr int JUMP_MENU_CLOSE = JUMP_MENU_ENTRIES - 1;

// The two main menus give the last row to the mod version and sit Close a blank
// line above it. The submenus have no version line and keep Return/Close on
// JUMP_MENU_CLOSE, so this is deliberately a separate pair of constants rather
// than a shift applied to that one.
constexpr int JUMP_MAIN_VERSION = JUMP_MENU_ENTRIES - 1;
constexpr int JUMP_MAIN_CLOSE = JUMP_MENU_ENTRIES - 3;
constexpr int JUMP_MENU_NEXT = JUMP_MENU_CLOSE - 2;
constexpr int JUMP_MENU_PREV = JUMP_MENU_NEXT - 1;
constexpr int JUMP_MENU_MAPS_PER_PAGE = JUMP_MENU_PREV - JUMP_MENU_FIRST_MAP;

// Paging state, TagMalloc'd as the menu's arg so PMenu_Close frees it.
struct jump_menu_page_t
{
	int offset;
};

static void Jump_MenuSelectMap(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuPrevPage(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuNextPage(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuClose(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuReturnToMain(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuUpdateMaps(edict_t *ent);

static void Jump_MenuVoteYes(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuVoteNo(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuUpdateVote(edict_t *ent);

static void Jump_MenuUpdateInGame(edict_t *ent);
static void Jump_MenuUpdateSpectator(edict_t *ent);
static void Jump_MenuStore(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuRecall(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuRestart(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuJoinPractice(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuJoinRanked(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuJoinSpectator(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuFollowPlayer(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuFollowView(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuOpenMapVote(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuExtendTime(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuOpenHelp(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuOpenOptions(edict_t *ent, pmenuhnd_t *hnd);

static void Jump_MenuCycleSpeedo(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuToggleTakeoff(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuCycleStrafe(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuCycleCgaz(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuToggleJumpers(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuResetReadouts(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuUpdateOptions(edict_t *ent);

static void Jump_OpenCastMenu(edict_t *ent);
static void Jump_OpenMapMenu(edict_t *ent);
static void Jump_OpenHelpMenu(edict_t *ent);
static void Jump_OpenOptionsMenu(edict_t *ent);

// Two main menus, because half the rows only mean something on one side of the
// line: a spectator has no run to restart, and a player in the map has nothing
// to follow. Both are still JUMP_MENU_ENTRIES long, so a handle opened for one
// can never be indexed past its end by the other's update function.
//
// The SelectFuncs below matter even though every row is rewritten on the first
// refresh: PMenu_Open validates the requested starting cursor against the
// static template, not against what the update function will write. A template
// of empty rows would drop the cursor onto the only selectable entry there is -
// Close - which is a poor place to land when the menu opened itself to ask
// which team you want.

// In-game rows. There is one join row, not two: you are always on one of the
// three teams and the title block already names it, so a row for the team you
// are on would say nothing and could not be picked. That keeps the layout
// fixed whichever team you are on, which is what lets Jump_OpenMainMenu name a
// starting row by constant.
constexpr int JUMP_GAME_RESTART = 3;
constexpr int JUMP_GAME_STORE = 4;
constexpr int JUMP_GAME_RECALL = 5;
constexpr int JUMP_GAME_JOIN = 7;
constexpr int JUMP_GAME_SPECTATE = 8;
constexpr int JUMP_GAME_VOTE = 10;
constexpr int JUMP_GAME_EXTEND = 11;
constexpr int JUMP_GAME_HELP = 13;

// Spectator rows.
constexpr int JUMP_SPEC_PRACTICE = 3;
constexpr int JUMP_SPEC_RANKED = 4;
constexpr int JUMP_SPEC_FOLLOW = 6;
constexpr int JUMP_SPEC_FOLLOW_VIEW = 7;
constexpr int JUMP_SPEC_VOTE = 9;
constexpr int JUMP_SPEC_EXTEND = 10;
constexpr int JUMP_SPEC_HELP = 12;

// Jump_MenuClearTail writes Options immediately below How to Play, so both help
// rows need a row spare before Close. The tail is indexed by hand, and writing
// over Close would cost a player the way out of the menu.
static_assert(JUMP_GAME_HELP + 1 < JUMP_MAIN_CLOSE, "no room for Options on the in-game menu");
static_assert(JUMP_SPEC_HELP + 1 < JUMP_MAIN_CLOSE, "no room for Options on the spectator menu");

static const pmenu_t jump_ingame_menu[JUMP_MENU_ENTRIES] = {
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 0  title
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 1  current team
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 2  blank
	{ "", PMENU_ALIGN_LEFT, Jump_MenuRestart },			  // 3  restart run
	{ "", PMENU_ALIGN_LEFT, Jump_MenuStore },			  // 4  save position
	{ "", PMENU_ALIGN_LEFT, Jump_MenuRecall },			  // 5  load position
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 6  blank
	{ "", PMENU_ALIGN_LEFT, Jump_MenuJoinRanked },		  // 7  join the other team
	{ "", PMENU_ALIGN_LEFT, Jump_MenuJoinSpectator },	  // 8  spectate
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 9  blank
	{ "", PMENU_ALIGN_LEFT, Jump_MenuOpenMapVote },		  // 10 vote map
	{ "", PMENU_ALIGN_LEFT, Jump_MenuExtendTime },		  // 11 extend
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 12 blank
	{ "", PMENU_ALIGN_LEFT, Jump_MenuOpenHelp },		  // 13 how to play
	{ "", PMENU_ALIGN_LEFT, Jump_MenuOpenOptions },		  // 14 options
	{ "Close", PMENU_ALIGN_LEFT, Jump_MenuClose },		  // 15
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 16 blank
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 17 version
};

static const pmenu_t jump_spectator_menu[JUMP_MENU_ENTRIES] = {
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 0  title
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 1  "Spectator"
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 2  blank
	{ "", PMENU_ALIGN_LEFT, Jump_MenuJoinPractice },	  // 3  join practice
	{ "", PMENU_ALIGN_LEFT, Jump_MenuJoinRanked },		  // 4  join ranked
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 5  blank
	{ "", PMENU_ALIGN_LEFT, Jump_MenuFollowPlayer },	  // 6  follow player
	{ "", PMENU_ALIGN_LEFT, Jump_MenuFollowView },		  // 7  follow view
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 8  blank
	{ "", PMENU_ALIGN_LEFT, Jump_MenuOpenMapVote },		  // 9  vote map
	{ "", PMENU_ALIGN_LEFT, Jump_MenuExtendTime },		  // 10 extend
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 11 blank
	{ "", PMENU_ALIGN_LEFT, Jump_MenuOpenHelp },		  // 12 how to play
	{ "", PMENU_ALIGN_LEFT, Jump_MenuOpenOptions },		  // 13 options
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 14 blank
	{ "Close", PMENU_ALIGN_LEFT, Jump_MenuClose },		  // 15
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 16 blank
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 17 version
};

static const pmenu_t jump_map_menu[JUMP_MENU_ENTRIES] = {
	{ "", PMENU_ALIGN_CENTER, nullptr },   // 0  title
	{ "", PMENU_ALIGN_CENTER, nullptr },   // 1  blank
	{ "", PMENU_ALIGN_LEFT, nullptr },	   // 2  maps...
	{ "", PMENU_ALIGN_LEFT, nullptr },	   // 3
	{ "", PMENU_ALIGN_LEFT, nullptr },	   // 4
	{ "", PMENU_ALIGN_LEFT, nullptr },	   // 5
	{ "", PMENU_ALIGN_LEFT, nullptr },	   // 6
	{ "", PMENU_ALIGN_LEFT, nullptr },	   // 7
	{ "", PMENU_ALIGN_LEFT, nullptr },	   // 8
	{ "", PMENU_ALIGN_LEFT, nullptr },	   // 9
	{ "", PMENU_ALIGN_LEFT, nullptr },	   // 10
	{ "", PMENU_ALIGN_LEFT, nullptr },	   // 11
	{ "", PMENU_ALIGN_LEFT, nullptr },	   // 12
	{ "", PMENU_ALIGN_LEFT, nullptr },	   // 13
	{ "", PMENU_ALIGN_LEFT, nullptr },	   // 14 prev page
	{ "", PMENU_ALIGN_LEFT, nullptr },	   // 15 next page
	{ "", PMENU_ALIGN_CENTER, nullptr },   // 16 blank
	{ "Return", PMENU_ALIGN_LEFT, Jump_MenuReturnToMain }, // 17
};

static const pmenu_t jump_vote_menu[JUMP_MENU_ENTRIES] = {
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_LEFT, Jump_MenuVoteYes },
	{ "", PMENU_ALIGN_LEFT, Jump_MenuVoteNo },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "", PMENU_ALIGN_CENTER, nullptr },
	{ "Close", PMENU_ALIGN_LEFT, Jump_MenuClose },
};

// The one-pager. Static text, so it needs no UpdateFunc - PMenu_Open copies the
// entries into the handle and the periodic re-send just redraws them.
//
// It fills the panel exactly: 18 rows is all there is, and left-aligned rows
// draw from x=64 against a backdrop that ends at x=288, which is about 26
// characters of the proportional client font. Every line below is kept to 24
// so the mixed-width glyphs have somewhere to go; over-long text is not
// clipped, it just draws out over the map. Return is the only pickable row, so
// the cursor lands there whichever way it is opened.
static const pmenu_t jump_help_menu[JUMP_MENU_ENTRIES] = {
	{ "How to Play", PMENU_ALIGN_CENTER, nullptr },			   // 0
	{ "", PMENU_ALIGN_CENTER, nullptr },					   // 1
	{ "Get to the finish fast.", PMENU_ALIGN_LEFT, nullptr },  // 2
	{ "Touch all checkpoints.", PMENU_ALIGN_LEFT, nullptr },   // 3
	{ "", PMENU_ALIGN_CENTER, nullptr },					   // 4
	{ "Practice: save and load", PMENU_ALIGN_LEFT, nullptr },  // 5
	{ "freely. Never recorded.", PMENU_ALIGN_LEFT, nullptr },  // 6
	{ "", PMENU_ALIGN_CENTER, nullptr },					   // 7
	{ "Ranked: one clean run.", PMENU_ALIGN_LEFT, nullptr },   // 8
	{ "No loading. Times saved.", PMENU_ALIGN_LEFT, nullptr }, // 9
	{ "", PMENU_ALIGN_CENTER, nullptr },					   // 10
	{ "Finish on a weapon/key.", PMENU_ALIGN_LEFT, nullptr },  // 11
	{ "", PMENU_ALIGN_CENTER, nullptr },					   // 12
	{ "Handy binds:", PMENU_ALIGN_LEFT, nullptr },			   // 13
	{ "bind mouse4 store", PMENU_ALIGN_LEFT, nullptr },		   // 14
	{ "bind mouse5 recall", PMENU_ALIGN_LEFT, nullptr },	   // 15
	{ "", PMENU_ALIGN_CENTER, nullptr },					   // 16
	{ "Return", PMENU_ALIGN_CENTER, Jump_MenuReturnToMain },   // 17
};

// Per-player display settings. Every row here changes something the player sees
// and nothing anyone else does, which is what separates it from the main menu.
//
// The two readouts each exist twice over - the server draws them on the status
// bar for everyone, this DLL's overlay redraws them finer for whoever installed
// it - and a row here means the readout, not one of the two copies. So "On" is
// the best version that player can get, and the handler works out which half to
// speak to. See Jump_MenuUpdateOptions.
// Grouped in pairs, because that is how they are used: the speedometer and the
// mark above it are one instrument, and the strafe meter and CGaz are the two
// halves of the angle question - where to point, and how well you pointed.
constexpr int JUMP_OPT_SPEEDO = 2;
constexpr int JUMP_OPT_TAKEOFF = 3;
constexpr int JUMP_OPT_STRAFE = 5;
constexpr int JUMP_OPT_CGAZ = 6;
constexpr int JUMP_OPT_JUMPERS = 8;
constexpr int JUMP_OPT_RESET = 10;

static const pmenu_t jump_options_menu[JUMP_MENU_ENTRIES] = {
	{ "Options", PMENU_ALIGN_CENTER, nullptr },				 // 0
	{ "", PMENU_ALIGN_CENTER, nullptr },					 // 1  blank
	{ "", PMENU_ALIGN_LEFT, Jump_MenuCycleSpeedo },			 // 2  speedometer
	{ "", PMENU_ALIGN_LEFT, Jump_MenuToggleTakeoff },		 // 3  takeoff mark
	{ "", PMENU_ALIGN_CENTER, nullptr },					 // 4  blank
	{ "", PMENU_ALIGN_LEFT, Jump_MenuCycleStrafe },			 // 5  strafe meter
	{ "", PMENU_ALIGN_LEFT, Jump_MenuCycleCgaz },			 // 6  cgaz
	{ "", PMENU_ALIGN_CENTER, nullptr },					 // 7  blank
	{ "", PMENU_ALIGN_LEFT, Jump_MenuToggleJumpers },		 // 8  hide players
	{ "", PMENU_ALIGN_CENTER, nullptr },					 // 9  blank
	{ "", PMENU_ALIGN_LEFT, Jump_MenuResetReadouts },		 // 10 reset readouts
	{ "", PMENU_ALIGN_CENTER, nullptr },					 // 11
	{ "", PMENU_ALIGN_CENTER, nullptr },					 // 12
	{ "", PMENU_ALIGN_CENTER, nullptr },					 // 13
	{ "", PMENU_ALIGN_CENTER, nullptr },					 // 14
	{ "", PMENU_ALIGN_CENTER, nullptr },					 // 15
	{ "", PMENU_ALIGN_CENTER, nullptr },					 // 16
	{ "Return", PMENU_ALIGN_CENTER, Jump_MenuReturnToMain }, // 17
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void Jump_MenuSetRow(pmenu_t &entry, const char *text, int align, SelectFunc_t select)
{
	Q_strlcpy(entry.text, text, sizeof(entry.text));
	entry.align = align;
	entry.SelectFunc = select;
	entry.text_arg1[0] = '\0';
}

static void Jump_MenuClose(edict_t *ent, pmenuhnd_t *hnd)
{
	PMenu_Close(ent);
	ent->client->update_chase = true;
}

static void Jump_MenuJoinRow(pmenu_t &entry, jump_team_t team, SelectFunc_t select)
{
	Jump_MenuSetRow(entry, G_Fmt("Join {}", Jump_TeamName(team)).data(), PMENU_ALIGN_LEFT, select);
}

// ---------------------------------------------------------------------------
// Main options menu
// ---------------------------------------------------------------------------

// Blanks everything from `first` up to the foot of the menu, then writes the
// four rows both main menus end with: How to Play and Options, which trail the
// gameplay rows a blank line below them, then Close, then the mod version along
// the bottom. `help` is a parameter rather than a shared constant because the two
// menus have different numbers of gameplay rows above it; the other two are
// pinned to the foot of the panel and are the same either way.
//
// The version is a compile-time literal, so it costs nothing to rewrite here
// on every refresh and there is no second place to keep it in sync.
static void Jump_MenuClearTail(pmenuhnd_t *hnd, int first, int help)
{
	// Up to but not including the version row, so the gap between Close and it
	// comes out of the blanking rather than needing a row written by hand.
	for (int i = first; i < JUMP_MAIN_VERSION; i++)
		Jump_MenuSetRow(hnd->entries[i], "", PMENU_ALIGN_CENTER, nullptr);

	Jump_MenuSetRow(hnd->entries[help], "How to Play", PMENU_ALIGN_LEFT, Jump_MenuOpenHelp);
	Jump_MenuSetRow(hnd->entries[help + 1], "Options", PMENU_ALIGN_LEFT, Jump_MenuOpenOptions);
	Jump_MenuSetRow(hnd->entries[JUMP_MAIN_CLOSE], "Close", PMENU_ALIGN_LEFT, Jump_MenuClose);
	Jump_MenuSetRow(hnd->entries[JUMP_MAIN_VERSION], "Q2RE-Jump v" JUMP_VERSION_STRING, PMENU_ALIGN_CENTER,
					nullptr);
}

static void Jump_MenuUpdateInGame(edict_t *ent)
{
	pmenuhnd_t *hnd = ent->client->menu;

	if (!hnd)
		return;

	jump_client_t *jc = Jump_ClientData(ent);
	const jump_team_t team = jc ? jc->team : jump_team_t::ranked;

	Jump_MenuSetRow(hnd->entries[0], "Jump", PMENU_ALIGN_CENTER, nullptr);
	Jump_MenuSetRow(hnd->entries[1], Jump_TeamName(team), PMENU_ALIGN_CENTER, nullptr);
	Jump_MenuSetRow(hnd->entries[2], "", PMENU_ALIGN_CENTER, nullptr);

	// Store and recall are a Practice facility: Ranked refuses `store` outright
	// and turns `recall` into a restart, which is what keeps its times
	// comparable. The rows stay on the Ranked menu anyway, so someone who has
	// only ever played Ranked still learns the feature exists - there is no
	// dim variant in the layout font (a row is either normal or the bright alt
	// colour the cursor uses), so "unavailable" is carried by dropping the
	// SelectFunc, which makes PMenu_Next skip straight past the row, plus the
	// suffix that says why.
	//
	// The suffix is "(Locked)" rather than the more explicit "(Practice Only)"
	// because rows draw from x=64 and the inventory backdrop behind them ends
	// at x=288, which is about 26 characters of the client font. Nothing clips
	// the overflow - the text just draws out over the map. Picking the row is
	// not the way anyone finds out why anyway, since the cursor will not land
	// on it; `store` from the console says which team it needs.
	//
	// An empty store stack is not marked at all: Load Position stays live and
	// Jump_CmdRecall prints "You have no stores.", which is a clearer answer
	// than a row the cursor refuses to land on.
	const bool ranked = (team == jump_team_t::ranked);

	Jump_MenuSetRow(hnd->entries[JUMP_GAME_RESTART], "Restart Run", PMENU_ALIGN_LEFT, Jump_MenuRestart);
	Jump_MenuSetRow(hnd->entries[JUMP_GAME_STORE], ranked ? "Save Position (Locked)" : "Save Position",
					PMENU_ALIGN_LEFT, ranked ? nullptr : Jump_MenuStore);
	Jump_MenuSetRow(hnd->entries[JUMP_GAME_RECALL], ranked ? "Load Position (Locked)" : "Load Position",
					PMENU_ALIGN_LEFT, ranked ? nullptr : Jump_MenuRecall);

	Jump_MenuSetRow(hnd->entries[6], "", PMENU_ALIGN_CENTER, nullptr);

	if (ranked)
		Jump_MenuJoinRow(hnd->entries[JUMP_GAME_JOIN], jump_team_t::practice, Jump_MenuJoinPractice);
	else
		Jump_MenuJoinRow(hnd->entries[JUMP_GAME_JOIN], jump_team_t::ranked, Jump_MenuJoinRanked);

	Jump_MenuSetRow(hnd->entries[JUMP_GAME_SPECTATE], "Spectate", PMENU_ALIGN_LEFT, Jump_MenuJoinSpectator);

	Jump_MenuSetRow(hnd->entries[9], "", PMENU_ALIGN_CENTER, nullptr);
	Jump_MenuSetRow(hnd->entries[JUMP_GAME_VOTE], "Vote Map", PMENU_ALIGN_LEFT, Jump_MenuOpenMapVote);
	Jump_MenuSetRow(hnd->entries[JUMP_GAME_EXTEND], "Extend Time", PMENU_ALIGN_LEFT, Jump_MenuExtendTime);

	Jump_MenuClearTail(hnd, JUMP_GAME_EXTEND + 1, JUMP_GAME_HELP);
}

static void Jump_MenuUpdateSpectator(edict_t *ent)
{
	pmenuhnd_t *hnd = ent->client->menu;

	if (!hnd)
		return;

	jump_client_t *jc = Jump_ClientData(ent);

	Jump_MenuSetRow(hnd->entries[0], "Jump", PMENU_ALIGN_CENTER, nullptr);
	Jump_MenuSetRow(hnd->entries[1], Jump_TeamName(jump_team_t::spectator), PMENU_ALIGN_CENTER, nullptr);
	Jump_MenuSetRow(hnd->entries[2], "", PMENU_ALIGN_CENTER, nullptr);

	Jump_MenuJoinRow(hnd->entries[JUMP_SPEC_PRACTICE], jump_team_t::practice, Jump_MenuJoinPractice);
	Jump_MenuJoinRow(hnd->entries[JUMP_SPEC_RANKED], jump_team_t::ranked, Jump_MenuJoinRanked);
	Jump_MenuSetRow(hnd->entries[5], "", PMENU_ALIGN_CENTER, nullptr);

	Jump_MenuSetRow(hnd->entries[JUMP_SPEC_FOLLOW], ent->client->chase_target ? "Stop Following" : "Follow Player",
					PMENU_ALIGN_LEFT, Jump_MenuFollowPlayer);
	Jump_MenuSetRow(hnd->entries[JUMP_SPEC_FOLLOW_VIEW],
					G_Fmt("Follow View: {}", jc && jc->eyecam ? "First-Person" : "Third-Person").data(),
					PMENU_ALIGN_LEFT, Jump_MenuFollowView);

	Jump_MenuSetRow(hnd->entries[8], "", PMENU_ALIGN_CENTER, nullptr);
	Jump_MenuSetRow(hnd->entries[JUMP_SPEC_VOTE], "Vote Map", PMENU_ALIGN_LEFT, Jump_MenuOpenMapVote);
	Jump_MenuSetRow(hnd->entries[JUMP_SPEC_EXTEND], "Extend Time", PMENU_ALIGN_LEFT, Jump_MenuExtendTime);

	Jump_MenuClearTail(hnd, JUMP_SPEC_EXTEND + 1, JUMP_SPEC_HELP);
}

// Both close first: you want to be looking at the map, not the menu, the
// moment a position is saved or recalled. The rows are only offered when the
// action can succeed, but the commands keep their own guards regardless - a
// team change between the refresh and the keypress would otherwise slip past.
static void Jump_MenuStore(edict_t *ent, pmenuhnd_t *hnd)
{
	PMenu_Close(ent);
	ent->client->update_chase = true;
	Jump_CmdStore(ent);
}

static void Jump_MenuRecall(edict_t *ent, pmenuhnd_t *hnd)
{
	PMenu_Close(ent);
	ent->client->update_chase = true;
	Jump_CmdRecall(ent, 1);
}

static void Jump_MenuRestart(edict_t *ent, pmenuhnd_t *hnd)
{
	PMenu_Close(ent);
	ent->client->update_chase = true;
	Jump_RestartRun(ent);
}

static void Jump_MenuJoinPractice(edict_t *ent, pmenuhnd_t *hnd)
{
	PMenu_Close(ent);
	ent->client->update_chase = true;
	Jump_JoinTeam(ent, jump_team_t::practice);
}

static void Jump_MenuJoinRanked(edict_t *ent, pmenuhnd_t *hnd)
{
	PMenu_Close(ent);
	ent->client->update_chase = true;
	Jump_JoinTeam(ent, jump_team_t::ranked);
}

static void Jump_MenuJoinSpectator(edict_t *ent, pmenuhnd_t *hnd)
{
	PMenu_Close(ent);
	ent->client->update_chase = true;
	Jump_JoinTeam(ent, jump_team_t::spectator);
}

static void Jump_MenuFollowPlayer(edict_t *ent, pmenuhnd_t *hnd)
{
	PMenu_Close(ent);

	jump_client_t *jc = Jump_ClientData(ent);
	if (!jc)
		return;

	if (jc->team != jump_team_t::spectator)
		Jump_JoinTeam(ent, jump_team_t::spectator);

	if (ent->client->chase_target)
		Jump_FreeFollower(ent);
	else
		GetChaseTarget(ent);
}

static void Jump_MenuFollowView(edict_t *ent, pmenuhnd_t *hnd)
{
	PMenu_Close(ent);
	Jump_CmdEyecam(ent);
}

static void Jump_MenuOpenMapVote(edict_t *ent, pmenuhnd_t *hnd)
{
	PMenu_Close(ent);
	Jump_OpenMapMenu(ent);
}

static void Jump_MenuExtendTime(edict_t *ent, pmenuhnd_t *hnd)
{
	PMenu_Close(ent);
	ent->client->update_chase = true;
	Jump_CmdTimeExtend(ent);
}

static void Jump_MenuOpenHelp(edict_t *ent, pmenuhnd_t *hnd)
{
	PMenu_Close(ent);
	Jump_OpenHelpMenu(ent);
}

static void Jump_OpenHelpMenu(edict_t *ent)
{
	PMenu_Open(ent, jump_help_menu, -1, JUMP_MENU_ENTRIES, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

// Which readout a row is about. They differ in who can draw them, and that is
// what decides how a row behaves rather than anything cosmetic:
//
//   speedo, strafe  both halves can draw it, so every player gets a working row
//   cgaz            the overlay only - the bar cannot draw a moving angular
//                   zone at any price - so a stock client's row has nothing to
//                   set and says so instead of lying
enum class jump_readout_kind_t
{
	speedo,
	strafe,
	cgaz
};

// What a readout is currently set to, as one of JUMP_READOUT_*. For a client
// running the mod's own half that is the cvar it reported; for a stock client
// there is only the server's copy, so on/off is all it can be.
static int Jump_MenuReadoutState(const jump_client_t *jc, jump_readout_kind_t kind)
{
	if (!jc)
		return JUMP_READOUT_OFF;

	if (jc->client_hud)
	{
		switch (kind)
		{
		case jump_readout_kind_t::speedo: return jc->client_speed;
		case jump_readout_kind_t::strafe: return jc->client_strafe;
		case jump_readout_kind_t::cgaz: return jc->client_cgaz;
		}
	}

	// No overlay, so no CGaz to report on at all.
	if (kind == jump_readout_kind_t::cgaz)
		return JUMP_READOUT_OFF;

	const bool on = kind == jump_readout_kind_t::speedo ? jc->server_speedo : jc->server_strafebar;

	return on ? JUMP_READOUT_ON : JUMP_READOUT_OFF;
}

static const char *Jump_MenuReadoutLabel(int state)
{
	if (state == JUMP_READOUT_OFF)
		return "Off";

	return state == JUMP_READOUT_CENTRED ? "Centred" : "On";
}

// Applies a readout state to whichever half owns it for this player.
//
// For one of our clients the cvar is the setting, and the server cannot write it
// directly - it publishes the state it wants in JUMP_STAT_HUD_REQUEST and the
// overlay applies it to its own cvars. The local copy is written straight away
// too, so the row relabels this frame rather than waiting for the round trip; the
// client's report then confirms it and clears the request.
//
// For a stock client there is no cvar and no overlay to carry the request, so the
// server's own flag is both the setting and the whole story.
static void Jump_MenuSetReadout(edict_t *ent, jump_client_t *jc, jump_readout_kind_t kind, int state)
{
	if (jc->client_hud)
	{
		switch (kind)
		{
		case jump_readout_kind_t::speedo: jc->client_speed = state; break;
		case jump_readout_kind_t::strafe: jc->client_strafe = state; break;
		case jump_readout_kind_t::cgaz: jc->client_cgaz = state; break;
		}

		// All three ride in the one stat, so a request always carries the whole
		// set - the one just picked and whatever the others are already set to.
		int seq = Jump_HudRequestSeq(jc->hud_request) + 1;

		if (seq > JUMP_HUD_REQUEST_SEQ_MAX)
			seq = 1; // 0 means "nothing pending", so it is never a live sequence

		jc->hud_request =
			Jump_EncodeHudRequest(seq, jc->client_speed, jc->client_strafe, jc->client_cgaz);

		Jump_Log("options -> %s: request %d, speed=%d strafe=%d cgaz=%d", Jump_DisplayName(ent), seq,
				 jc->client_speed, jc->client_strafe, jc->client_cgaz);
		return;
	}

	// A stock client has no overlay, so CGaz has nothing to set - and its row is
	// unpickable for exactly that reason, so this should never be reached.
	if (kind == jump_readout_kind_t::cgaz)
		return;

	Jump_Log("options: no client hud reported, setting the server's %s to %d",
			 kind == jump_readout_kind_t::speedo ? "speedo" : "strafebar", state);

	if (kind == jump_readout_kind_t::speedo)
		jc->server_speedo = (state != JUMP_READOUT_OFF);
	else
		jc->server_strafebar = (state != JUMP_READOUT_OFF);
}

// The same click the eyecam and jumpers toggles make. PMenu has no select sound
// of its own, and the row's label does not change until the next frame, so
// without this a pick on a readout row feels like it missed.
static void Jump_MenuClick(edict_t *ent)
{
	gi.local_sound(ent, CHAN_AUTO, gi.soundindex("misc/menu3.wav"), 1, ATTN_NONE, 0);
}

static void Jump_MenuUpdateOptions(edict_t *ent)
{
	pmenuhnd_t *hnd = ent->client->menu;

	if (!hnd)
		return;

	const jump_client_t *jc = Jump_ClientData(ent);

	Jump_MenuSetRow(
		hnd->entries[JUMP_OPT_SPEEDO],
		G_Fmt("Speedometer: {}",
			  Jump_MenuReadoutLabel(Jump_MenuReadoutState(jc, jump_readout_kind_t::speedo)))
			.data(),
		PMENU_ALIGN_LEFT, Jump_MenuCycleSpeedo);

	// The mark is drawn by the bar for everybody, so unlike the rows around it
	// this one needs no handshake and works the same whatever client you are on.
	Jump_MenuSetRow(hnd->entries[JUMP_OPT_TAKEOFF],
					G_Fmt("Takeoff Mark: {}", (jc && jc->server_takeoff) ? "On" : "Off").data(),
					PMENU_ALIGN_LEFT, Jump_MenuToggleTakeoff);

	Jump_MenuSetRow(
		hnd->entries[JUMP_OPT_STRAFE],
		G_Fmt("Strafe Meter: {}",
			  Jump_MenuReadoutLabel(Jump_MenuReadoutState(jc, jump_readout_kind_t::strafe)))
			.data(),
		PMENU_ALIGN_LEFT, Jump_MenuCycleStrafe);

	// CGaz is overlay-only, so a stock client's row has nothing to offer. Say why
	// rather than showing a switch that would do nothing, and drop the SelectFunc
	// so the cursor skips it - the same idiom the locked store rows use.
	if (jc && jc->client_hud)
		Jump_MenuSetRow(
			hnd->entries[JUMP_OPT_CGAZ],
			G_Fmt("CGaz: {}", Jump_MenuReadoutLabel(Jump_MenuReadoutState(jc, jump_readout_kind_t::cgaz)))
				.data(),
			PMENU_ALIGN_LEFT, Jump_MenuCycleCgaz);
	else
		Jump_MenuSetRow(hnd->entries[JUMP_OPT_CGAZ], "CGaz: needs the DLL", PMENU_ALIGN_LEFT, nullptr);

	// Named for what picking it does, not for the state it is in, because the
	// state is on the same row - "Hide Players: On" is unambiguous in a way that
	// "Show Players: Off" is not.
	Jump_MenuSetRow(hnd->entries[JUMP_OPT_JUMPERS],
					G_Fmt("Hide Players: {}", (jc && !jc->show_jumpers) ? "On" : "Off").data(), PMENU_ALIGN_LEFT,
					Jump_MenuToggleJumpers);

	// The way back to a clean screen. It exists because the rerelease archives
	// these cvars whether or not you ever touched them, so a player who has run an
	// older build never sees a changed default - this is how they get it without
	// editing system.cfg by hand.
	Jump_MenuSetRow(hnd->entries[JUMP_OPT_RESET], "Turn All Readouts Off", PMENU_ALIGN_LEFT,
					Jump_MenuResetReadouts);
}

// The handlers below leave the menu open and let it relabel in place, which is
// what makes a settings screen usable - the map pager does the same. PMenu_Update
// marks the menu dirty and the next client frame re-sends it.
static void Jump_MenuCycleSpeedo(edict_t *ent, pmenuhnd_t *hnd)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return;

	const bool on = Jump_MenuReadoutState(jc, jump_readout_kind_t::speedo) != JUMP_READOUT_OFF;

	Jump_MenuSetReadout(ent, jc, jump_readout_kind_t::speedo, on ? JUMP_READOUT_OFF : JUMP_READOUT_ON);
	Jump_MenuClick(ent);
	PMenu_Update(ent);
}

// No handshake and no client copy: the bar draws the mark for everyone, so this
// is the one readout row that is a plain server-side flag.
static void Jump_MenuToggleTakeoff(edict_t *ent, pmenuhnd_t *hnd)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return;

	jc->server_takeoff = !jc->server_takeoff;
	Jump_MenuClick(ent);
	PMenu_Update(ent);
}

// Off - On only. The strip has no second form to offer, and no bar-side twin
// either, so this row is only ever reached by a client that can draw it.
static void Jump_MenuCycleCgaz(edict_t *ent, pmenuhnd_t *hnd)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc || !jc->client_hud)
		return;

	const bool on = Jump_MenuReadoutState(jc, jump_readout_kind_t::cgaz) != JUMP_READOUT_OFF;

	Jump_MenuSetReadout(ent, jc, jump_readout_kind_t::cgaz, on ? JUMP_READOUT_OFF : JUMP_READOUT_ON);
	Jump_MenuClick(ent);
	PMenu_Update(ent);
}

static void Jump_MenuCycleStrafe(edict_t *ent, pmenuhnd_t *hnd)
{
	jump_client_t *jc = Jump_ClientData(ent);

	if (!jc)
		return;

	// Off - On - Centred, but only for a client that can draw the centred one.
	// The status bar has no way to anchor a bar in the middle, so for a stock
	// client the third state would be indistinguishable from the second and the
	// cycle would look broken.
	const int state = Jump_MenuReadoutState(jc, jump_readout_kind_t::strafe);
	int		  next;

	if (state == JUMP_READOUT_OFF)
		next = JUMP_READOUT_ON;
	else if (state == JUMP_READOUT_ON && jc->client_hud)
		next = JUMP_READOUT_CENTRED;
	else
		next = JUMP_READOUT_OFF;

	Jump_MenuSetReadout(ent, jc, jump_readout_kind_t::strafe, next);
	Jump_MenuClick(ent);
	PMenu_Update(ent);
}

static void Jump_MenuToggleJumpers(edict_t *ent, pmenuhnd_t *hnd)
{
	Jump_CmdJumpers(ent);
	PMenu_Update(ent);
}

static void Jump_MenuResetReadouts(edict_t *ent, pmenuhnd_t *hnd)
{
	Jump_ResetReadouts(ent);
	Jump_MenuClick(ent);
	PMenu_Update(ent);
}

static void Jump_MenuOpenOptions(edict_t *ent, pmenuhnd_t *hnd)
{
	PMenu_Close(ent);
	Jump_OpenOptionsMenu(ent);
}

static void Jump_OpenOptionsMenu(edict_t *ent)
{
	PMenu_Open(ent, jump_options_menu, JUMP_OPT_SPEEDO, JUMP_MENU_ENTRIES, nullptr, Jump_MenuUpdateOptions);
}

void Jump_OpenMainMenu(edict_t *ent)
{
	jump_client_t	 *jc = Jump_ClientData(ent);
	const jump_team_t team = jc ? jc->team : jump_team_t::spectator;

	if (team == jump_team_t::spectator)
	{
		PMenu_Open(ent, jump_spectator_menu, JUMP_SPEC_PRACTICE, JUMP_MENU_ENTRIES, nullptr,
				   Jump_MenuUpdateSpectator);
		return;
	}

	// Start on Restart Run, which is also the first selectable row: it is what
	// a player in the map opens this menu for far more often than anything
	// else. It does mean a reflexive attack press on open throws away the run
	// in progress - accepted, because the in-game menu only ever opens on a
	// keypress. The unprompted one on join and map change is the spectator
	// menu, which has no such row.
	PMenu_Open(ent, jump_ingame_menu, JUMP_GAME_RESTART, JUMP_MENU_ENTRIES, nullptr, Jump_MenuUpdateInGame);
}

// A team change swaps which of the two menus applies, so one left open has to
// be rebuilt rather than carry on running the wrong update function against a
// handle built from the other template. Submenus are left alone - the map list
// and the vote UI do not vary by team.
void Jump_RefreshMainMenu(edict_t *ent)
{
	const pmenuhnd_t *hnd = ent->client->menu;

	if (!hnd)
		return;

	if (hnd->UpdateFunc != Jump_MenuUpdateInGame && hnd->UpdateFunc != Jump_MenuUpdateSpectator)
		return;

	PMenu_Close(ent);
	Jump_OpenMainMenu(ent);
}

static void Jump_MenuReturnToMain(edict_t *ent, pmenuhnd_t *hnd)
{
	PMenu_Close(ent);
	Jump_OpenMainMenu(ent);
}

// ---------------------------------------------------------------------------
// Map list
// ---------------------------------------------------------------------------

static void Jump_MenuUpdateMaps(edict_t *ent)
{
	pmenuhnd_t *hnd = ent->client->menu;

	if (!hnd)
		return;

	jump_menu_page_t *page = (jump_menu_page_t *) hnd->arg;

	std::vector<std::string> maps = Jump_CollectVotableMaps();

	const int total = (int) maps.size();
	const int pages = total ? (total + JUMP_MENU_MAPS_PER_PAGE - 1) / JUMP_MENU_MAPS_PER_PAGE : 1;

	if (page)
	{
		// A page can go stale if the map list shrank since it was opened.
		if (page->offset >= total)
			page->offset = (pages - 1) * JUMP_MENU_MAPS_PER_PAGE;
		if (page->offset < 0)
			page->offset = 0;
	}

	const int offset = page ? page->offset : 0;
	const int current_page = (offset / JUMP_MENU_MAPS_PER_PAGE) + 1;

	if (pages > 1)
		Jump_MenuSetRow(hnd->entries[0], G_Fmt("Vote for a Map ({}/{})", current_page, pages).data(),
						PMENU_ALIGN_CENTER, nullptr);
	else
		Jump_MenuSetRow(hnd->entries[0], "Vote for a Map", PMENU_ALIGN_CENTER, nullptr);

	Jump_MenuSetRow(hnd->entries[1], "", PMENU_ALIGN_CENTER, nullptr);

	for (int i = 0; i < JUMP_MENU_MAPS_PER_PAGE; i++)
	{
		pmenu_t	  &row = hnd->entries[JUMP_MENU_FIRST_MAP + i];
		const int  index = offset + i;

		if (index >= total)
		{
			Jump_MenuSetRow(row, "", PMENU_ALIGN_LEFT, nullptr);
			continue;
		}

		const bool is_current = !Q_strcasecmp(maps[index].c_str(), level.mapname);

		Jump_MenuSetRow(row, is_current ? G_Fmt("{}  (Playing)", maps[index].c_str()).data() : maps[index].c_str(),
						PMENU_ALIGN_LEFT, is_current ? nullptr : Jump_MenuSelectMap);

		// The row remembers which map it is; the label may be decorated.
		Q_strlcpy(row.text_arg1, maps[index].c_str(), sizeof(row.text_arg1));
	}

	Jump_MenuSetRow(hnd->entries[JUMP_MENU_PREV], offset > 0 ? "< Previous Page" : "", PMENU_ALIGN_LEFT,
					offset > 0 ? Jump_MenuPrevPage : nullptr);

	const bool has_next = offset + JUMP_MENU_MAPS_PER_PAGE < total;

	Jump_MenuSetRow(hnd->entries[JUMP_MENU_NEXT], has_next ? "> Next Page" : "", PMENU_ALIGN_LEFT,
					has_next ? Jump_MenuNextPage : nullptr);

	Jump_MenuSetRow(hnd->entries[JUMP_MENU_ENTRIES - 2], "", PMENU_ALIGN_CENTER, nullptr);
	Jump_MenuSetRow(hnd->entries[JUMP_MENU_CLOSE], "Return", PMENU_ALIGN_LEFT, Jump_MenuReturnToMain);

	if (total == 0)
		Jump_MenuSetRow(hnd->entries[JUMP_MENU_FIRST_MAP], "No maps configured", PMENU_ALIGN_LEFT, nullptr);
}

static void Jump_MenuSelectMap(edict_t *ent, pmenuhnd_t *hnd)
{
	if (!hnd || hnd->cur < 0 || hnd->cur >= hnd->num)
		return;

	// Copy before closing: the entries are freed with the menu.
	char map[MAX_QPATH];
	Q_strlcpy(map, hnd->entries[hnd->cur].text_arg1, sizeof(map));

	PMenu_Close(ent);
	ent->client->update_chase = true;

	if (!map[0])
		return;

	Jump_StartMapVote(ent, map);
}

static void Jump_MenuPrevPage(edict_t *ent, pmenuhnd_t *hnd)
{
	jump_menu_page_t *page = hnd ? (jump_menu_page_t *) hnd->arg : nullptr;

	if (!page)
		return;

	page->offset -= JUMP_MENU_MAPS_PER_PAGE;

	if (page->offset < 0)
		page->offset = 0;

	PMenu_Update(ent);
}

static void Jump_MenuNextPage(edict_t *ent, pmenuhnd_t *hnd)
{
	jump_menu_page_t *page = hnd ? (jump_menu_page_t *) hnd->arg : nullptr;

	if (!page)
		return;

	page->offset += JUMP_MENU_MAPS_PER_PAGE;
	PMenu_Update(ent);
}

static void Jump_OpenMapMenu(edict_t *ent)
{
	jump_menu_page_t *page = (jump_menu_page_t *) gi.TagMalloc(sizeof(*page), TAG_LEVEL);
	page->offset = 0;

	PMenu_Open(ent, jump_map_menu, -1, JUMP_MENU_ENTRIES, page, Jump_MenuUpdateMaps);
}

// ---------------------------------------------------------------------------
// Vote in progress
// ---------------------------------------------------------------------------

static void Jump_MenuUpdateVote(edict_t *ent)
{
	pmenuhnd_t *hnd = ent->client->menu;

	if (!hnd)
		return;

	if (!Jump_VoteActive())
	{
		Jump_MenuSetRow(hnd->entries[0], "The Vote Has Ended", PMENU_ALIGN_CENTER, nullptr);

		for (int i = 1; i < JUMP_MENU_CLOSE; i++)
			Jump_MenuSetRow(hnd->entries[i], "", PMENU_ALIGN_CENTER, nullptr);

		Jump_MenuSetRow(hnd->entries[JUMP_MENU_CLOSE], "Close", PMENU_ALIGN_LEFT, Jump_MenuClose);
		return;
	}

	int yes = 0, no = 0, needed = 0;
	Jump_VoteTally(yes, no, needed);

	Jump_MenuSetRow(hnd->entries[0], "Vote in Progress", PMENU_ALIGN_CENTER, nullptr);
	Jump_MenuSetRow(hnd->entries[1], "", PMENU_ALIGN_CENTER, nullptr);
	Jump_MenuSetRow(hnd->entries[2], G_Fmt("{} called:", Jump_VoteCaller()).data(), PMENU_ALIGN_CENTER, nullptr);
	Jump_MenuSetRow(hnd->entries[3], Jump_VoteDescription(), PMENU_ALIGN_CENTER, nullptr);
	Jump_MenuSetRow(hnd->entries[4], "", PMENU_ALIGN_CENTER, nullptr);
	Jump_MenuSetRow(hnd->entries[5], G_Fmt("{} seconds left", Jump_VoteSecondsLeft()).data(), PMENU_ALIGN_CENTER,
					nullptr);

	const bool voted = Jump_HasVoted(ent);

	Jump_MenuSetRow(hnd->entries[6], voted ? "Yes" : "[ Yes ]", PMENU_ALIGN_LEFT, Jump_MenuVoteYes);
	Jump_MenuSetRow(hnd->entries[7], voted ? "No" : "[ No ]", PMENU_ALIGN_LEFT, Jump_MenuVoteNo);

	Jump_MenuSetRow(hnd->entries[8], "", PMENU_ALIGN_CENTER, nullptr);
	Jump_MenuSetRow(hnd->entries[9], G_Fmt("Yes {}   No {}   Need {}", yes, no, needed).data(), PMENU_ALIGN_CENTER,
					nullptr);

	for (int i = 10; i < JUMP_MENU_CLOSE; i++)
		Jump_MenuSetRow(hnd->entries[i], "", PMENU_ALIGN_CENTER, nullptr);

	Jump_MenuSetRow(hnd->entries[JUMP_MENU_CLOSE], "Close", PMENU_ALIGN_LEFT, Jump_MenuClose);
}

static void Jump_MenuVoteYes(edict_t *ent, pmenuhnd_t *hnd)
{
	PMenu_Close(ent);
	ent->client->update_chase = true;
	Jump_CmdVote(ent, true);
}

static void Jump_MenuVoteNo(edict_t *ent, pmenuhnd_t *hnd)
{
	PMenu_Close(ent);
	ent->client->update_chase = true;
	Jump_CmdVote(ent, false);
}

static void Jump_OpenCastMenu(edict_t *ent)
{
	PMenu_Open(ent, jump_vote_menu, -1, JUMP_MENU_ENTRIES, nullptr, Jump_MenuUpdateVote);
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

// Map vote submenu, or cast UI if a vote is already running.
void Jump_OpenVoteMenu(edict_t *ent)
{
	if (Jump_VoteActive())
	{
		Jump_OpenCastMenu(ent);
		return;
	}

	Jump_OpenMapMenu(ent);
}

// The menu hangs off the `inven` command, conventionally bound to TAB, so this
// is what the key actually reaches. Toggling is expected: press once to open,
// again to dismiss.
void Jump_CmdMenu(edict_t *ent)
{
	if (ent->client->menu)
	{
		PMenu_Close(ent);
		ent->client->update_chase = true;

		// First dismissal, so say how to get it back. %bind:...% is resolved by
		// the client against its own binding - which is the point, since the
		// player may not have `inven` on TAB at all.
		jump_client_t *jc = Jump_ClientData(ent);

		if (jc && !jc->menu_hint_shown)
		{
			gi.LocClient_Print(ent, PRINT_CENTER, "%bind:inven:Open menu%{}", " ");
			jc->menu_hint_shown = true;
		}

		return;
	}

	if (Jump_VoteActive())
	{
		Jump_OpenCastMenu(ent);
		return;
	}

	Jump_OpenMainMenu(ent);
}
