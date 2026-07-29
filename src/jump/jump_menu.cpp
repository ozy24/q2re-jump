// [Jump] The TAB menu: pick a map to vote for, or answer the vote in progress.
//
// Built on the stock PMenu system (ctf/p_ctf_menu.h), which the engine already
// drives for us: invnext/invprev move the cursor, invuse selects, and inven
// closes. The only wiring needed is opening it.
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

// Rows 0-1 are the title block, the last row is Close, and two rows above that
// are the pager. Everything between is maps.
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
static void Jump_MenuUpdateMaps(edict_t *ent);

static void Jump_MenuVoteYes(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuVoteNo(edict_t *ent, pmenuhnd_t *hnd);
static void Jump_MenuUpdateVote(edict_t *ent);

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
	{ "Close", PMENU_ALIGN_LEFT, Jump_MenuClose }, // 17
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
	Jump_MenuSetRow(hnd->entries[JUMP_MENU_CLOSE], "Close", PMENU_ALIGN_LEFT, Jump_MenuClose);

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

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void Jump_OpenVoteMenu(edict_t *ent)
{
	if (Jump_VoteActive())
	{
		PMenu_Open(ent, jump_vote_menu, -1, JUMP_MENU_ENTRIES, nullptr, Jump_MenuUpdateVote);
		return;
	}

	jump_menu_page_t *page = (jump_menu_page_t *) gi.TagMalloc(sizeof(*page), TAG_LEVEL);
	page->offset = 0;

	PMenu_Open(ent, jump_map_menu, -1, JUMP_MENU_ENTRIES, page, Jump_MenuUpdateMaps);
}

// TAB is bound to `inven`, so this is what the key actually reaches. Toggling
// is expected: press once to open, again to dismiss.
void Jump_CmdMenu(edict_t *ent)
{
	if (ent->client->menu)
	{
		PMenu_Close(ent);
		ent->client->update_chase = true;
		return;
	}

	Jump_OpenVoteMenu(ent);
}
