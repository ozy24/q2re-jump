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

#include <string>
#include <vector>

constexpr int JUMP_MENU_ENTRIES = 18;

// Rows 0-1 are the title block, the last row is Return/Close, and two rows
// above that are the pager. Everything between is maps.
constexpr int JUMP_MENU_FIRST_MAP = 2;
constexpr int JUMP_MENU_CLOSE = JUMP_MENU_ENTRIES - 1;
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

static void Jump_OpenCastMenu(edict_t *ent);
static void Jump_OpenMapMenu(edict_t *ent);

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

// In-game rows.
constexpr int JUMP_GAME_STORE = 3;
constexpr int JUMP_GAME_RECALL = 4;
constexpr int JUMP_GAME_RESTART = 5;
constexpr int JUMP_GAME_PRACTICE = 7;
constexpr int JUMP_GAME_RANKED = 8;
constexpr int JUMP_GAME_SPECTATE = 9;
constexpr int JUMP_GAME_VOTE = 11;
constexpr int JUMP_GAME_EXTEND = 12;

// Spectator rows.
constexpr int JUMP_SPEC_PRACTICE = 3;
constexpr int JUMP_SPEC_RANKED = 4;
constexpr int JUMP_SPEC_FOLLOW = 6;
constexpr int JUMP_SPEC_FOLLOW_VIEW = 7;
constexpr int JUMP_SPEC_VOTE = 9;
constexpr int JUMP_SPEC_EXTEND = 10;

static const pmenu_t jump_ingame_menu[JUMP_MENU_ENTRIES] = {
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 0  title
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 1  current team
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 2  blank
	{ "", PMENU_ALIGN_LEFT, Jump_MenuStore },			  // 3  save position
	{ "", PMENU_ALIGN_LEFT, Jump_MenuRecall },			  // 4  load position
	{ "", PMENU_ALIGN_LEFT, Jump_MenuRestart },			  // 5  restart run
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 6  blank
	{ "", PMENU_ALIGN_LEFT, Jump_MenuJoinPractice },	  // 7  practice
	{ "", PMENU_ALIGN_LEFT, Jump_MenuJoinRanked },		  // 8  ranked
	{ "", PMENU_ALIGN_LEFT, Jump_MenuJoinSpectator },	  // 9  spectate
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 10 blank
	{ "", PMENU_ALIGN_LEFT, Jump_MenuOpenMapVote },		  // 11 vote map
	{ "", PMENU_ALIGN_LEFT, Jump_MenuExtendTime },		  // 12 extend
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 13
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 14
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 15
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 16 blank
	{ "Close", PMENU_ALIGN_LEFT, Jump_MenuClose },		  // 17
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
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 11
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 12
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 13
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 14
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 15
	{ "", PMENU_ALIGN_CENTER, nullptr },				  // 16 blank
	{ "Close", PMENU_ALIGN_LEFT, Jump_MenuClose },		  // 17
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

static void Jump_MenuTeamRow(pmenu_t &entry, jump_team_t team, jump_team_t current, SelectFunc_t select)
{
	const char *name = Jump_TeamName(team);

	if (team == current)
		Jump_MenuSetRow(entry, G_Fmt("{}  (current)", name).data(), PMENU_ALIGN_LEFT, nullptr);
	else
		Jump_MenuSetRow(entry, G_Fmt("Join {}", name).data(), PMENU_ALIGN_LEFT, select);
}

// ---------------------------------------------------------------------------
// Main options menu
// ---------------------------------------------------------------------------

// Blanks everything from `first` up to the Close row, so each update function
// only has to write the rows it actually uses.
static void Jump_MenuClearTail(pmenuhnd_t *hnd, int first)
{
	for (int i = first; i < JUMP_MENU_CLOSE; i++)
		Jump_MenuSetRow(hnd->entries[i], "", PMENU_ALIGN_CENTER, nullptr);

	Jump_MenuSetRow(hnd->entries[JUMP_MENU_CLOSE], "Close", PMENU_ALIGN_LEFT, Jump_MenuClose);
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
	// comparable. There is no dim variant in the layout font - a row is either
	// normal or the bright alt colour the cursor uses - so unavailable rows are
	// marked by dropping their SelectFunc, which makes PMenu_Next skip straight
	// past them, and by saying in the row itself why they cannot be picked.
	if (team == jump_team_t::ranked)
	{
		Jump_MenuSetRow(hnd->entries[JUMP_GAME_STORE], "Save Position  (practice only)", PMENU_ALIGN_LEFT,
						nullptr);
		Jump_MenuSetRow(hnd->entries[JUMP_GAME_RECALL], "Load Position  (practice only)", PMENU_ALIGN_LEFT,
						nullptr);
	}
	else
	{
		Jump_MenuSetRow(hnd->entries[JUMP_GAME_STORE], "Save Position", PMENU_ALIGN_LEFT, Jump_MenuStore);

		const bool has_store = jc && !jc->stores.Empty();

		if (has_store)
			Jump_MenuSetRow(hnd->entries[JUMP_GAME_RECALL],
							G_Fmt("Load Position  ({} saved)", jc->stores.count).data(), PMENU_ALIGN_LEFT,
							Jump_MenuRecall);
		else
			Jump_MenuSetRow(hnd->entries[JUMP_GAME_RECALL], "Load Position  (none saved)", PMENU_ALIGN_LEFT,
							nullptr);
	}

	Jump_MenuSetRow(hnd->entries[JUMP_GAME_RESTART], "Restart run", PMENU_ALIGN_LEFT, Jump_MenuRestart);
	Jump_MenuSetRow(hnd->entries[6], "", PMENU_ALIGN_CENTER, nullptr);

	Jump_MenuTeamRow(hnd->entries[JUMP_GAME_PRACTICE], jump_team_t::practice, team, Jump_MenuJoinPractice);
	Jump_MenuTeamRow(hnd->entries[JUMP_GAME_RANKED], jump_team_t::ranked, team, Jump_MenuJoinRanked);
	Jump_MenuSetRow(hnd->entries[JUMP_GAME_SPECTATE], "Spectate", PMENU_ALIGN_LEFT, Jump_MenuJoinSpectator);

	Jump_MenuSetRow(hnd->entries[10], "", PMENU_ALIGN_CENTER, nullptr);
	Jump_MenuSetRow(hnd->entries[JUMP_GAME_VOTE], "Vote map", PMENU_ALIGN_LEFT, Jump_MenuOpenMapVote);
	Jump_MenuSetRow(hnd->entries[JUMP_GAME_EXTEND], "Extend time", PMENU_ALIGN_LEFT, Jump_MenuExtendTime);

	Jump_MenuClearTail(hnd, JUMP_GAME_EXTEND + 1);
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

	Jump_MenuSetRow(hnd->entries[JUMP_SPEC_PRACTICE], "Join Practice", PMENU_ALIGN_LEFT, Jump_MenuJoinPractice);
	Jump_MenuSetRow(hnd->entries[JUMP_SPEC_RANKED], "Join Ranked", PMENU_ALIGN_LEFT, Jump_MenuJoinRanked);
	Jump_MenuSetRow(hnd->entries[5], "", PMENU_ALIGN_CENTER, nullptr);

	Jump_MenuSetRow(hnd->entries[JUMP_SPEC_FOLLOW], ent->client->chase_target ? "Stop following" : "Follow player",
					PMENU_ALIGN_LEFT, Jump_MenuFollowPlayer);
	Jump_MenuSetRow(hnd->entries[JUMP_SPEC_FOLLOW_VIEW],
					G_Fmt("Follow view: {}", jc && jc->eyecam ? "first-person" : "third-person").data(),
					PMENU_ALIGN_LEFT, Jump_MenuFollowView);

	Jump_MenuSetRow(hnd->entries[8], "", PMENU_ALIGN_CENTER, nullptr);
	Jump_MenuSetRow(hnd->entries[JUMP_SPEC_VOTE], "Vote map", PMENU_ALIGN_LEFT, Jump_MenuOpenMapVote);
	Jump_MenuSetRow(hnd->entries[JUMP_SPEC_EXTEND], "Extend time", PMENU_ALIGN_LEFT, Jump_MenuExtendTime);

	Jump_MenuClearTail(hnd, JUMP_SPEC_EXTEND + 1);
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

	// Start on the team they are not on: it is the one row here they might
	// have opened the menu to press, and it keeps the cursor off Restart run,
	// which would otherwise throw away a run to a reflexive attack press.
	const int cur = (team == jump_team_t::practice) ? JUMP_GAME_RANKED : JUMP_GAME_PRACTICE;

	PMenu_Open(ent, jump_ingame_menu, cur, JUMP_MENU_ENTRIES, nullptr, Jump_MenuUpdateInGame);
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
		Jump_MenuSetRow(hnd->entries[0], G_Fmt("Vote for a map ({}/{})", current_page, pages).data(),
						PMENU_ALIGN_CENTER, nullptr);
	else
		Jump_MenuSetRow(hnd->entries[0], "Vote for a map", PMENU_ALIGN_CENTER, nullptr);

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

		Jump_MenuSetRow(row, is_current ? G_Fmt("{}  (playing)", maps[index].c_str()).data() : maps[index].c_str(),
						PMENU_ALIGN_LEFT, is_current ? nullptr : Jump_MenuSelectMap);

		// The row remembers which map it is; the label may be decorated.
		Q_strlcpy(row.text_arg1, maps[index].c_str(), sizeof(row.text_arg1));
	}

	Jump_MenuSetRow(hnd->entries[JUMP_MENU_PREV], offset > 0 ? "< Previous page" : "", PMENU_ALIGN_LEFT,
					offset > 0 ? Jump_MenuPrevPage : nullptr);

	const bool has_next = offset + JUMP_MENU_MAPS_PER_PAGE < total;

	Jump_MenuSetRow(hnd->entries[JUMP_MENU_NEXT], has_next ? "> Next page" : "", PMENU_ALIGN_LEFT,
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
		Jump_MenuSetRow(hnd->entries[0], "The vote has ended", PMENU_ALIGN_CENTER, nullptr);

		for (int i = 1; i < JUMP_MENU_CLOSE; i++)
			Jump_MenuSetRow(hnd->entries[i], "", PMENU_ALIGN_CENTER, nullptr);

		Jump_MenuSetRow(hnd->entries[JUMP_MENU_CLOSE], "Close", PMENU_ALIGN_LEFT, Jump_MenuClose);
		return;
	}

	int yes = 0, no = 0, needed = 0;
	Jump_VoteTally(yes, no, needed);

	Jump_MenuSetRow(hnd->entries[0], "Vote in progress", PMENU_ALIGN_CENTER, nullptr);
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
	Jump_MenuSetRow(hnd->entries[9], G_Fmt("yes {}   no {}   need {}", yes, no, needed).data(), PMENU_ALIGN_CENTER,
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
