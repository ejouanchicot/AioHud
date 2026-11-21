// native_ui.h -- hide / offset the CLIENT's own windows (party, alliance, chat, help bar, menus).
//
// FFXI draws every native window as a named MENU (def name "menu    xxxxxxxx" at def+0x46) owned by one menu
// manager. The client places a window in exactly two ways, and both are covered BEFORE the window is drawn :
//   1. from its LAYOUT RECORD when it is created / re-laid out  -> we shift the record (x/y in a 512x448 virtual
//      screen, anchor at +0x13) ;
//   2. by its SCREEN's code through the "move this menu to x,y" function (Equipment's inventory, item info...)
//      -> that function is hooked and the window's offset added to the coordinates.
// Windows already open when an offset changes are moved once through the client's own setter, then their handler
// is asked to refresh the origins it caches (the menu name in titlewin, the NPC question in query).
//
// A HIDDEN window is simply offset far below the screen : it keeps working (party cursor, chat log, NPC choices),
// it is just never visible. Nothing is permanent : turning an option off, //unload, and a crash recovery at the next
// load all put every record and every open window back. Investigation + proofs :
// docs/game-data/native-windows.md.
#pragma once

namespace aio {

// The native windows the config can hide, as a bit mask (UiConfig::nuHide).
enum NativeUiBits {
    NU_PARTY  = 1 << 0,   // partywin (+ targetwi, which the client anchors above it)
    NU_ALLY   = 1 << 1,   // raid1, raid2
    NU_CHAT   = 1 << 2,   // logwindo, logwin2 and the one-line message strips on them (NPC text lives there too)
    NU_HELP   = 1 << 3,   // helpwind (description bar) + titlewin (current menu name) -- AioHUD draws its top bar (native_topbar)
    NU_COST   = 1 << 4,   // subwindo : the MP-cost / recast case above the party (AioHUD's Cost box replaces it)
    NU_NET    = 1 << 5,   // netstat (connection indicator) + polwindo (POL message icon), top-right corner
    NU_ACT    = 1 << 6,   // playermo : the target ACTION menu (Enter on a target) -- AioHUD draws its own (hud_actmenu)
    NU_NPC    = 1 << 7,   // rem4line / rem4li2 (NPC dialogue) + query (its answers) -- AioHUD draws them above its chat
    NU_MAIN   = 1 << 8,   // menuwind / socialme : the MAIN menu's two pages -- AioHUD draws its own (hud_actmenu)
    NU_MACRO  = 1 << 9,   // mcr1pall / mcr2pall (+ their long forms) : the Ctrl / Alt macro bar -- AioHUD draws its own
    NU_TRADE  = 1 << 10,  // trade (your side) + gift (the partner's) : the player-to-player trade -- AioHUD draws its own
                          // ... and `handover` (trading to an NPC : the same panel, your column only)
    NU_DIALOG = 1 << 11,  // the Yes / No confirms (comyn, merityn, resyn*, shopbuy, shopsell) -- AioHUD draws its own
    NU_BUFF   = 1 << 12,  // buff : your status icons under the top bar -- AioHUD draws its own (the party's icon set)
};
static const int NU_ALL = 0x1FFF;   // every NativeUiBits bit (the config line's mask, the change detector)

// The YES / NO CONFIRMS (NU_DIALOG ; docs/notes/dialogs-loot-inspect-re-2026-11-20.md s.1) : ONE client class backs every
// generic confirm -- the question (+0x1C), the title (+0x34, also pushed to the top bar), 2 entries (1 Yes, 2 No ; the
// client puts the cursor on No unless the asker keeps it), whether Escape may answer No (+0x39).
struct NativeDialog {
    bool open, focused;
    char name[9];
    char text[400];        // the question, decoded (colour / control codes dropped ; "\n" kept)
    char title[64];
    int  cursor;           // 1 Yes, 2 No
    bool cancelOk, hold;   // Escape answers No ; a "hold to confirm" answer (the client's gauge)
    short rx0, ry0, rx1, ry1; int uiW, uiH;
};
const NativeDialog& native_dialog();
bool native_dialog_hover(int entry);   // 1 Yes / 2 No : the client's cursor there
bool native_dialog_pick(int entry);    // ... and validated (the asker's callback answers)

// The PLAYER TRADE (NU_TRADE ; docs/notes/trade-re-2026-11-20.md, live 2026-11-20 : both offers, names, cursor, states).
// Slot 0 = the gil (count), 1..8 the items. States : 0 none, 1 asked, 3 trading, 4 OK pressed (the name turns orange),
// 5 cancelled ; any item change by either side puts both back to 3.
struct NativeTradeSlot { unsigned short id; unsigned count; };
struct NativeTrade {
    bool open, focused;          // `trade` is up / the player drives it (not the inventory picker or a quantity box)
    int  cursor;                 // 1..8 a slot, 9 OK, 10 Cancel, 11 the gil ; 0 none
    int  own, peer;              // the two sides' states (4 = OK pressed)
    char me[17], partner[33];
    NativeTradeSlot mine[9], theirs[9];
    short rx0, ry0, rx1, ry1; int uiW, uiH;   // `trade`'s rect (parked : where the client put it)
    bool giftUp; short gx0, gy0, gx1, gy1;    // `gift` (the partner's side) : its rect -- the client stacks it under `trade`
    bool giftFocused; int giftCursor;         // the player browses the partner's side (its item card) : 1..8, 0 none
    // the client's boxes it opens, parked while the trade is AioHUD's : the QUANTITY (itemctrl : value / max) and the GIL
    // (moneyctr : value, max, the digit being edited 1 / 10 / 100 ...)
    bool qtyUp; int qtyVal, qtyMax;
    bool gilUp; unsigned gilVal, gilMax, gilDigit;
    // where the client puts them (UI units, parked : where they would be) -- AioHUD draws its own there : itemctrl (the
    // quantity, its item's card beside it), moneyctr (the gil) and `money` (Current Gil, beside the gil box)
    short qx0, qy0, qx1, qy1, mx0, my0, mx1, my1, wx0, wy0, wx1, wy1;
    bool moneyUp; unsigned myGil; bool myGilOk;
    bool npc; int npcIndex;      // `handover` : trading to an NPC (your side only ; the NPC's entity index)
};
const NativeTrade& native_trade();
// The trade's end, read every frame (even with the windows gone) : the P state (7 = ended) and the last 0x022 type
// (9 = completed) -- AioHUD's summary of what was given / received
struct NativeTradeEnd { int state, lastType; };
const NativeTradeEnd& native_trade_end();
// The trade's boxes, driven the client's way (trade-re s.7) : box 0 = the quantity (itemctrl : 1 -, 2 +, 3 = 1, 4 = max,
// 5 validate), box 1 = the gil (moneyctr : 1 -, 2 +, 3 / 4 the digit, 5 validate) ; false when that box is not up.
bool native_trade_box_event(int box, int ev);
// The gil typed by the player (the user 2026-11-20 : "gil au clavier oui") : written into moneyctr's value (+0x28),
// clamped to its max (+0x24) -- the client's own Enter / validate then sends it. False when the gil box is not up.
bool native_trade_set_gil(unsigned v);
// The partner's side took the focus (the client's "down" from your bottom row, only there to show its item cards) :
// sent back to yours with the client's own event 8 (the user 2026-11-20 : "on veut etre bloque dans ce cadre")
bool native_trade_keep_own();
// Drive it the client's way : the cursor onto an entry (1..11) / validate it (a slot : remove its item / pick one ;
// 9 OK ; 10 Cancel ; 11 the gil box). False when `trade` is not up and focused.
bool native_trade_hover(int entry);
bool native_trade_pick(int entry);

// The TOP BAR (NU_HELP ; docs/notes/top-bar-re-2026-11-20.md, live 2026-11-20) : `titlewin` (the menu name, left) and
// `helpwind` (the help line). Each is a handler whose help line holds 4 text slots (0 base, 1 the hovered menu entry, 2
// the open screen's text, 3 a server tip) ; the client shows the highest non-empty one, or a spinner while `busy`
// (waiting for the server : Search, Region Info). Plain text (the client's single-byte / SJIS ; no colour codes).
struct NativeTopLine {
    bool up;              // its window is open (in game : always, unless the client tore the UI down)
    bool busy;            // a request is pending : the client draws its spinner, not the text
    char text[0xCC];      // what the client shows ("" = nothing) ; an over-wide text's two trailing spaces trimmed
};
struct NativeTopBar { bool ok; NativeTopLine title, help; };   // ok : both read this frame
const NativeTopBar& native_topbar();

// The MACRO BAR (NU_MACRO ; docs/notes/macro-bar-re-2026-11-20.md, live 2026-11-20). The client opens it while Ctrl (1) or
// Alt (2) is held (or toggled by a pad button). Its data object holds the CURRENT set only : 20 macros (0..9 Ctrl,
// 10..19 Alt), the book and set, the 40 book titles.
struct NativeMacro {
    bool ok;              // the data reads (else : nothing to draw)
    int  open;            // 0 none, 1 the Ctrl bar, 2 the Alt bar
    int  hand;            // the bar's hand (mouse / pad) 1..10, 0 none
    int  lastRun[2];      // the slot last run from the Ctrl / Alt bar, 0-based (-1 none)
    int  book, set;       // 0-based (the client shows "#set+1")
    char bookTitle[17];
    char name[20][9];
    bool longBar;         // the game's long layout (Config) : 862 wide
    short rx0, ry0, rx1, ry1; int uiW, uiH;   // the open bar's rect, UI units (parked : where the client put it)
};
const NativeMacro& native_macro();

// The CONNECTION box `netstat` (NU_NET ; docs/notes/netstat-re-2026-11-20.md, static 2026-11-20). The client's network
// meter : a quality score 0..100 (every 3 s : -6 when the server has been silent >= 10 s, else +12) drawn as a signal
// (0 none, < 30, < 60, >= 60), the bytes / s received and sent ; and the PlayOnline mailbox / GM notice counts.
struct NativeNet {
    bool ok;
    int  score;           // 0..100
    unsigned recvBps, sentBps;
    unsigned gapMs;       // ms between the last two server packets
    int  mail, gm;        // POL messages waiting, GM notices
    int  pingMs;          // the round-trip, ms (the last ~2 s averaged) ; -1 not measured yet (docs/notes/ping-re-2026-11-20.md)
};
const NativeNet& native_net();

// The client's BUFF icons (`buff`, NU_BUFF ; WINLIST 2026-11-20) : a HUD window under the top bar, its record at
// 142,48 (units) -> 382,52 : 240 wide, 4 high -- the icons are drawn past its rect. Read for its place only : the
// icons themselves are the player's buffs AioHUD already has (GameState::buffs). Un-parked rect, UI units.
// THE CURSOR (X / numpad + : the client's buff-cancel cursor ; BUFCUR live 2026-11-20, 3 buffs, right x3) : while
// `buff` holds the focus, handler+0x30 = the cursor's index in the GAME's order (0..n-1, it wraps), +0x34 the previous
// one, +0x40 the count. Validate (event 5) on it = the client's own cancel (it refuses what cannot be cancelled).
struct NativeBuffWin { bool open; short rx0, ry0, rx1, ry1; short uiW, uiH; bool focused; int cursor, count; };
const NativeBuffWin& native_buffwin();
bool native_buff_hover(int idx);   // the client's cursor to that GAME index (the mouse over an icon)
bool native_buff_pick(int idx);    // ... and validate : the client cancels it if it can
// AioHUD's ORDER (the party's sort) : order[i] = the GAME index of the icon drawn i-th. Set by the UI each frame it draws ;
// the left / right the client receives for `buff` then walk THIS order (BUFMAP 2026-11-20 : the game's own order is not
// the drawn one -> "si je fais droite ca va a gauche").
void native_buff_set_order(const int* order, int n);

// The NPC windows (NU_NPC ; docs/game-data/native-windows.md section 10, live 2026-11-17) read once a frame :
// the event DIALOGUE `rem4line` (log 1) / `rem4li2` (log 2) and the ANSWERS `query`. Their text is GLYPHS (u16 per
// character, ASCII - 0x20 ; codes >= 0x5F are controls), decoded here to ASCII with the controls dropped.
struct NativeNpc {
    bool talk;                 // the dialogue shows lines
    int  nLines;               // 1..4
    char line[4][200];
    bool lead[4];              // the line starts a speech ("Name : text") -- the client's FEFF FEFF ; else a continuation
    bool query, queryFocused;  // the answers are up / the player drives them
    char question[200];
    int  n, sel, top;          // choices, the selected one (0-based), the first the client shows (it shows 3)
    char choice[32][100];
    unsigned short ctl[32][4]; // the CONTROL glyphs (>= 0x5F) each choice carries, first 4 -- the client colours some
                               // answers (a shop list's items in green, "Check/Exchange points." in white) ; being mapped
    unsigned itemId;           // the client's item description 'iteminfo' is up WITH the answers : the item it shows
                               // (handler+0x24, u16 -- live 2026-11-17, 11 / 11 shop items) ; 0 none
    bool fail;                 // a window was unreadable : the CLIENT's own are shown -> AioHUD draws none (never twice)
};
const NativeNpc& native_npc();
// Drive them the client's way (FUN_054596F0(obj, event)) : hover / pick a choice (0-based ; false when the answers are not
// up and focused, or the step fails), and "next page" on the dialogue (event 5, what Enter does on a page that waits).
bool native_npc_hover(int choice);
bool native_npc_pick(int choice);
bool native_npc_next();
// Where AioHUD drew them this frame (screen FRACTIONS ; w <= 0 : not drawn). The client's windows that open WITH the
// answers (the item description of a shop list, 'iteminfo') follow the parked query off-screen : they are brought back
// BESIDE this box, not to the client's place, which AioHUD's box now covers (2026-11-17).
// `card` : AioHUD draws that item's card itself -> the client's description stays parked (not brought back at all).
void native_npc_box(float x, float y, float w, float h, bool card = false);

// The AUTO-TRANSLATE picker `fep` (Tab in the input line ; docs/game-data/native-windows.md section 11, live
// 2026-11-17 //aio feplog). The client places it at its input line's caret : with that line parked "in AioHUD's
// chat box" it opens off-screen, so AioHUD draws it (read while the input line is docked). Handler h = obj+0x0C :
// h+0x1C the rows, their 0x10-byte items at h+0x20 (or *(h+0x21E8) when byte h+0x21F8 is set), item +4 char* text
// (cp932 ; a category is wrapped in the client's 【】), +8 its length ; the input system *(h+0x14) : byte +0x8C81
// the picker is on, +0x8C84 the selected row. The client shows 7 rows from (sel / 7) * 7.
struct NativeFep {
    bool open, focused;
    bool picking;              // the client's auto-translate MODE is on (input +0x8C81) -- also in the frame its window
                               // closes and reopens on a category ; off as soon as a phrase is chosen / Escape
    bool fail;                 // open but unreadable this frame : the client's own is brought back (never nothing)
    int  count, sel, top, rows;
    char row[7][160];          // UTF-8, the category brackets removed
    char sub[7][64];           // the part the client greys (after a 0x06 / a NUL inside the length) ; "" none
    bool cat[7];               // a category (validating it lists its phrases)
    char prefix[40];           // what the player typed before Tab (the filter) ; "" the category list
};
const NativeFep& native_fep();
// Drive it the client's way : hover a row (0-based list index, on the shown page : event 9 with obj+0x4C = page row + 2),
// pick (hover + event 5 = Enter), step (+1 next = Tab, -1 previous = Shift+Tab).
bool native_fep_hover(int idx);
bool native_fep_pick(int idx);
bool native_fep_step(int d);
// AioHUD drew the picker this frame : the client's stays parked (else the rescue pass brings it back)
void native_fep_drawn();

// The client's ACTION menu ("playermo"), read once a frame by native_ui_tick while NU_ACT is on (rule 6 : widgets read
// this snapshot, never the memory). Rows = the handler's u16 COMMAND CODES (handler+0x1C, count byte handler+0x53),
// cursor = obj+0x4C (1-based SCREEN row), rect = obj+0x3A (x0,y0,x1,y1 in the client's UI units, the parking offset
// removed), uiW/uiH = the UI screen (manager+0x80). Codes proven 2026-11-16 (docs/game-data/native-windows.md) :
// 1 Attack, 3 Disengage, 5 Chat, 7 Abilities, 8 Magic, 9 Items, 10 / 11 Check (player / mob), 18 Switch Target,
// 19 Trade, 25 Trust.
struct NativeMenu {
    bool open;           // the menu is open and its rows read
    bool focused;        // it is the menu the player drives (false while one of its submenus is open)
    int  n;              // rows
    unsigned short code[16];
    int  cursor;         // 1..n
    short rx0, ry0, rx1, ry1;
    int  uiW, uiH;
};
const NativeMenu& native_action_menu();
// Drive the action menu the way the CLIENT's own mouse does (live 2026-11-16, //aio mhook : every key / hover / click
// goes through FUN_054596F0(menu object, event), thiscall, which plays the sound and tells the handler) :
//   hover : cursor (obj+0x4C) = row, then event 9      pick : hover, then event 5 (validate -- what Enter / a click does)
// Rows are 1-based. False (and nothing done) when the menu is not open and focused, the row is out of range, or the
// client function is not found (its signature is checked ; a miss is retried every 5 s, never given up).
bool native_action_hover(int row);
bool native_action_pick(int row);

// The action menu's SUBMENUS whose rows are their definition's entries -- "magselec" (Magic : White / Black / Songs /
// Ninjutsu / Summoning / Blue / Geomancy) and "abiselec" (Abilities : Job abilities / Weapon skills / Ranged attack /
// Pet commands / Mount) and "chatctrl" (Chat : Say / Tell / Party / Linkshell / Linkshell 2 / Unity / Shout) -- parked with NU_ACT and redrawn the same way. Rows = the open object's ENTRY children
// (obj+0x14 list, child short type 1) : `entry` = its index in that list (what the cursor obj+0x4C counts, 1-based),
// `state` = child+0x10 (0 normal, 1 validated, 3 under the cursor, 5 refused, 0x10 skipped -> left out), shown in
// screen order (child rect y). Live 2026-11-16 (docs/game-data/native-windows.md section 9).
struct NativeEntryMenu {
    bool open, focused;
    char name[9];
    int  n;
    unsigned char entry[32];   // 1-based entry index of each shown row, top to bottom (Search's job picker : 22)
    unsigned char state[32];
    char label[32][24];        // each shown row's label as the client's DEFINITION writes it (ASCII ; "" when it has none --
                               // some menus draw theirs as images)
    int  cursor;               // 1-based entry index under the cursor
    short rx0, ry0, rx1, ry1;
    int  uiW, uiH;
    unsigned entMarks;         // the main menu's Party windows : bit e = entry e is ON / checked (party-re-2026-11-19.md)
    int  entMarkKind;          // 0 none, 1 toggles (Seek Party, Autogroup, Request : "on"), 2 a choice (Languages, Alarm Type), 3 the field being edited (scchar)
    // Search (search-re-2026-11-19.md) : each criterion's CURRENT value, by entry (Refine Search ; the level fields of
    // scchar / scmlev) -- entVal -1 = not set ; entVal2 the range's max ; entName the name criterion
    int  entVal[32], entVal2[32];
    char entName[16];
};
const NativeEntryMenu& native_sub_menu();
// The MAIN menu (NU_MAIN ; docs/game-data/native-windows.md section 8) : page 1 `menuwind` (Status ... Map), page 2
// `socialme` (Missions ... Log Out) -- the same entry-children shape as the submenus, 12 rows a page. name says the page.
const NativeEntryMenu& native_main_menu();
bool native_main_hover(int entry);
bool native_main_pick(int entry);
// what the main menu opens that AioHUD redraws too : Magic's categories `mgcmenu` (then the spell list, native_list)
const NativeEntryMenu& native_main_sub();
bool native_main_sub_hover(int entry);
bool native_main_sub_pick(int entry);
// ... and the spell list's SORT options `mgcsortw` (Auto-sort / Manual Sort ; X or numpad + gives it the cursor), open
// with the list -- the client puts it in the menu's corner (live 2026-11-18, OPTWIN)
const NativeEntryMenu& native_main_sort();
bool native_main_sort_hover(int entry);
bool native_main_sort_pick(int entry);
// ... and Auto-sort's confirmation `sortyn` (Yes / No) -- only while the sort options are up : the same window confirms
// the inventory's and the Mog House's sorts, those stay the client's
const NativeEntryMenu& native_main_yn();
bool native_main_yn_hover(int entry);
bool native_main_yn_pick(int entry);
// ... and Status -> Profile (`profile`, a panel : no entries) : open, where the client put it (parked : where it would
// be), and its data (game_mem.h ProfileMem). AioHUD draws it only while `data.ok` ; else the client's stays.
struct NativeProfile {
    bool  open;
    short rx0, ry0, rx1, ry1;
    int   uiW, uiH;
    bool  ok;
    int   nation, race, home, rank, rankPts, mastery, title;
    // Mentor : 0 none ("--"), 1..3 the flag (bronze / silver / gold, packet 0x01B +0x65) ; -1 not resolved (row hidden)
    int   mentor;
    // Monstrosity (packet 0x063 order 3, mirrored in a FFXiMain static) : shown only when the client would (its gate)
    bool  monOk;
    char  gladName[8];         // the client's OWN name for the rank ("Gla", "CG", "HCG")
    int   prestige;
};
const NativeProfile& native_profile();
// Status's two stat panels : `persona` (also the Equipment screen's) and `level` -- open, where the client put them
// (parked : where they would be) ; their data (PersonaMem) is read by the UI's model call below, once a frame.
struct NativePanel { bool open; short rx0, ry0, rx1, ry1; int uiW, uiH; };
const NativePanel& native_persona();
const NativePanel& native_level();
// the data both panels show, read once a frame while one of them is open (ok false : the client's panels stay)
struct PersonaMem;
const PersonaMem& native_persona_data();
// Status -> Job Levels (`joblevel`) / Master Levels (`masterle`) : two columns of jobs, the client's own data (packet
// 0x01B, kept in its player object -- Ghidra 2026-11-18, docs/notes/job-levels-re-2026-11-18.md). Only while Status's
// list is up : the Mog House job-change screen reuses `joblevel` with a cursor, and that one stays the client's.
struct NativeJobs {
    bool ok;
    int  mjob, sjob;
    unsigned char lvl[23], ml[23];   // by job id 1..22
    unsigned unlocked, mastered;     // bit j = job j
};
const NativePanel& native_joblevel();
const NativePanel& native_masterle();
const NativeJobs& native_jobs();
// Status -> Combat Skills (`btlskill`, ids 1..31) / Magic Skills (`mgcskill`, ids 32..47) : the client's skill array
// (packet 0x062 +0x80, 64 u16 : the level in the low 15 bits, bit 15 = capped ; exactly 0x8000 = not shown) -- a
// static right after the 0x061 mirror (FM_PW_BLOCK + 0x60 in both clients ; Ghidra 2026-11-18, docs/notes/skills-re).
struct NativeSkills { bool ok; unsigned short v[64]; };
const NativePanel& native_btlskill();
const NativePanel& native_mgcskill();
const NativeSkills& native_skills();
// Status -> Crafts (`trdskill`) : the same array, ids 48..62 -- rank v & 0x1F, level (v >> 5) & 0x3FF, bit 15 capped ;
// exactly 0xFFFF = not shown (Ghidra 2026-11-18, docs/notes/status-panels-a-re-2026-11-18.md)
const NativePanel& native_trdskill();
// Status -> Merit Points : `levmerit`, the Limit Points / merits panel beside the menu (FM_PW_MERIT : 0x063 order 2)
struct NativeMerits { bool ok; int lp, merits, maxMerits; };
const NativePanel& native_levmerit();
const NativeMerits& native_merits();
// Status -> Unity : `ut_point`, the Unity Points box under Unity's menu (its draw FUN_0548D910 : "%d/%d", the max a
// constant 99999). Data : the 0x061 mirror, packet 0x061 + 0x58 = FM_PW_BLOCK + 0x48 -- bits 0..4 the Unity, 5..9 the
// rank, 10..26 the points (docs/notes/status-panels-b-re-2026-11-18.md B4)
struct NativeUnity { bool ok; int unity, rank, points, maxPoints; };
const NativePanel& native_utpoint();
// Status -> Unity -> Unity Info : `quest00` (the Records of Eminence window) in its mode 1 (handler +0x68 == 1 -- never
// reset on close : ours only while ut_menu is up too). docs/notes/unity-info-re-2026-11-18.md :
//   the Unity       0x061 + 0x58 & 0x1F (FM_PW_BLOCK + 0x48) -> its leader's name (d_msg cat 0xC #402 + id)
//   Ranking (list 0) / Partial Ranking (list 1) : incoming 0x063 order 7, 0x1000 bytes a list ; score of a Unity =
//                   T[2][id] / T[1][id] (dwords at list + (k * 0x20 + id) * 4), rank = 1 + the Unities scoring more
//   Personal / Partial Personal Evaluation : 0x061 + 0x5C, its high / low u16
//   loading         the stats-refresh flag bit 0 clear, or a list's loaded byte 0 : "Preparing data."
//   chat settings   entry children 1 Active / 2 Inactive ; the setting chosen at handler +0x6E (1 Active)
struct NativeUnityInfo {
    bool open, focused, loading;
    int  mode;                   // 1 Unity Info, 2 Ranking Info (its two rows : Ranking Results / Partial)
    int  unity;                  // 1..11, 0 none
    int  rank[2], pts[2];        // Ranking Results / Partial : rank 0 = none ("--")
    int  personal, partial;
    int  chat;                   // 1 Active, 0 Inactive
    NativeEntryMenu rows;        // the chat setting rows (entries 1 / 2), their cursor
    short rx0, ry0, rx1, ry1; int uiW, uiH;
};
const NativeUnityInfo& native_unity_info();
// Ranking Info -> Ranking Results / Partial Ranking Results : `quest01` (handler mode 5 / 6), the 11 Unities sorted by
// their score (list 0 / 1, the same T[2] / T[1] as Unity Info), ties sharing a rank. The rows are computed here ; the
// client's cursor (its list class : +0x30 cursor, +0x38 top, +0x40 count) only says which row is under its hand.
struct NativeUnityRank {
    bool open, focused, loading;
    int  list;                   // 0 Ranking Results, 1 Partial
    int  n;                      // rows (11 once loaded)
    int  id[11], score[11], rank[11];
    int  cursor;                 // the client's row under its hand, -1 unknown
    short rx0, ry0, rx1, ry1; int uiW, uiH;
};
const NativeUnityRank& native_unity_rank();
// Status -> Unity -> Unity Lists (docs/notes/unity-lists-search-re-2026-11-18.md). Both windows are the client's
// multi-column TEXT LIST (vtable 0x0567B878) : handler +0x08 its window, +0x18 visible rows, +0x1E top, +0x20 count,
// +0x38 rows (0x54 B : +0x48 the record) ; the cursor row = window +0x4C (1-based, on screen) - 1 + top.
//  arealist  mode +0x6C == 4 : the Unity to list -- 12 rows, r = 0 your own ("Unity"), r = 1..11 the leaders
//  scresult  the people found : row 0 the header (" [Searching]" -> " [N]"), then the players (record +4 : the entry)
// Ours only while ut_menu is up (the main menu's Search reuses both : not yet).
struct NativeAreaList { bool open, focused; int mode, n, top, vis, cursor; short rx0, ry0, rx1, ry1; int uiW, uiH;
    // Search -> Area (mode 0) / Monipulators (mode 3) : the rows from the client's top, up to 12 (search-re s.7) -- arVal :
    // > 0 a zone, 0 All Areas, -1 the current region, <= -2 a region group (-(2 + group)) ; arLabel : the row's own text
    // when a cell holds one ; arCount : a number cell (a group's zone count), -1 none
    int arRows, arTop; int arVal[12], arCount[12]; char arLabel[12][32]; };   // arTop : the first row shown (12 show whenever the list has them)
const NativeAreaList& native_arealist();
struct NativeSearchRow {
    unsigned char type;          // 0 a player, 1 the header, 2 a group divider
    char name[16];
    unsigned char mjob, sjob, mlvl, slvl, ml;
    bool hasJob, mlShown;
    unsigned short zone;
    unsigned char lsRank;        // the Linkshell member list : 1 holder, 2 sack, 3 pearl (0 not read)
    unsigned char race;          // entry +0x1D (4 bits ; the client's race sprite) -- mapping checked by the NATIVEUI link12 log
    unsigned flags;              // 0x4000 anonymous, bit 0 mentor, 8 / 0x2000 the name recoloured (meanings mostly inferred)
};
struct NativeSearch {
    int  ctx;                     // 0 Unity Lists (ut_menu), 1 the Party's (scresult : Find Member, Member List, View Request), 2 Level Sync (levelsyn), 3 the main menu's Search, 4 a Linkshell's members (link12)
    char lsName[24]; unsigned lsRgb;   // ctx 4 : the linkshell listed, its colour
    bool open, focused, counted;  // counted : the header says " [N]" (the search is over)
    int  n, top, vis, cursor;     // rows (the header included), the client's page, its cursor row (-1 none)
    int  total;                   // the server's total (the header's N)
    char empty[64];               // the client's text when there is no row ("No applicable entries." / "Search failed.")
    NativeSearchRow row[16];      // rows top .. top + vis - 1
    short rx0, ry0, rx1, ry1; int uiW, uiH;
};
const NativeSearch& native_search();
// Status -> Job Points (docs/notes/status-panels-a-re-2026-11-18.md section 4) : `merit2ca`, the job list (a TEXT LIST :
// each row's id at row +0x48 = the job ; +0x54 the job chosen, jbpcat open over it), and `levmeri2`, the job's CP / JP.
// Data : the 0x063 order 5 mirror = FM_PW_MERIT + 0x0A, valid while the flags byte (FM_PW_MERIT - 2) & 4 :
// +4 + 6j Capacity Points, +6 + 6j Job Points, +8 + 6j spent.
struct NativeJobPoints {
    bool open, focused, ok;      // ok : the JP mirror reads
    int  n, top, vis, cursor;    // the rows, the client's page, its cursor row (-1 none)
    int  job[32];                // each row's job
    int  chosen;                 // the job validated (jbpcat open over the list), 0 none
    int  capPts[24], jobPts[24], jpSpent[24];   // (not cp / jp / spent : the replay types fields BY NAME -- CharSheet has a cp)
    unsigned char lvl[24];       // job levels (a job under 99 is greyed, as the client)
    NativePanel box;             // levmeri2's rect
    short rx0, ry0, rx1, ry1; int uiW, uiH;
};
const NativeJobPoints& native_job_points();
// A job validated : `jbpcat` over the list (docs/notes/job-points-re-2026-11-18.md 3), a TEXT LIST in two tabs (+0x58) :
// 0 its categories (cells : 0x35 the category id, 0x36 level + 1, 0x37 max + 1, 0x38 next cost + 1 -- 0 = not drawn),
// 1 its gifts (0x39 the gift + 1, 0x3A its threshold). The client's colours say greyed (0x80404040) / maxed (0x802060A0).
struct NativeJpRow { int id, lvl, max, cost, gift, thr; unsigned colName, colLvl, colCost, colThr; };
struct NativeJpCat {
    bool open, focused;
    int  job, tab, n, top, vis, cursor;
    NativeJpRow row[16];         // rows top .. top + vis - 1
    short rx0, ry0, rx1, ry1; int uiW, uiH;
};
const NativeJpCat& native_jp_cat();
bool native_jp_cat_tab(int dir);   // the client's left / right : the other tab
// The client's own texts for Job Points, read from its d_msg files (EN : 55694 the JP table, 55674 the gifts) -- loaded
// once, retried on a budget. kind 0 a job, 1 a category, 2 a gift of `job` ; desc false the name, true its description.
// 0 while not loaded / unknown. Bytes as the file has them (0x0A a line break ; 81 9A a star).
const char* native_jp_text(int kind, int id, bool desc, int job = 0);
// Status -> Alter Ego (docs/notes/status-panels-b-re-2026-11-18.md C) : `fe_main` (a submenu of 7), its entry 1 opens
// `fp_cat` (the categories : HP/MP, Attributes, Skills) then `fp_cat2` (a category's abilities), two TEXT LISTS, with
// `fp_info` (the points "n/1000"). Cells : 0x49 the name (a faith d_msg id), 0x4A level + 1, 0x4B max + 1, 0x4C next cost
// + 1 (0 blank) ; the client's colours say greyed / maxed. Points : u16 P + 0x2D614 (incoming 0x08E), loaded bit 4 of
// the flag byte -- both from the client's own accessors (AOB F / G).
struct NativeFaith {
    bool open, focused;
    int  level;                  // 0 the categories (fp_cat), 1 a category's abilities (fp_cat2)
    int  n, top, vis, cursor;
    NativeJpRow row[16];         // id = the name's value (the faith d_msg id / 2) ; lvl / max / cost as Job Points'
    int  rowId[16];              // each row's id (row +0x48)
    bool pointsOk; int points;
    short rx0, ry0, rx1, ry1; int uiW, uiH;
};
const NativeFaith& native_faith();
// Alter Ego -> Change Celestite (fe_main entries 2..7 : docs/notes/celestite-re-2026-11-18.md) : two TEXT LISTS side by
// side -- `fe_equip` (the type's slots : cell 0x0E the Lv badge, 0x19 the Celestite set, row +0x48 = n, 0 empty ; +0x54
// the slot picked, +0x58 the type 0..5, +0x50 may change = in the Mog House) and `fe_inven` (the Celestite owned : 0x4D
// icon, 0x19 name in the client's colour -- orange picked, green set in this type, grey not changeable ; +0x50 the row
// picked). Celestite n = item 0x72F0 + n (its name / description / icon from the item DAT).
struct NativeCelList { bool open, focused; int n, top, vis, cursor; int cel[16]; int badge[16]; unsigned col[16]; short rx0, ry0, rx1, ry1; int uiW, uiH; };
struct NativeCelestite { bool open; int type, picked, pickedRow; bool mayChange; NativeCelList equip, inven; };
const NativeCelestite& native_celestite();
// Status -> Merit Points -> Categories : `meritcat` (docs/notes/merit-cat-re-2026-11-18.md), a TEXT LIST in two levels (+0x50
// = 0 the categories ; else the merits of category +0x50). Category rows : row id = the category. Merit rows : cell 0x1E
// = i (cat * 32 + k), 8 = level + 1, 0x1F = next cost + 1 (<= 1 : blank) ; a job category has a HEADER row per group
// (row id -1, cell 7 = the group's cap). Names / descriptions : d_msg EN 55686, string 2 * cat or 2 * i (+1). The
// "Combo Total/Max" : the levels of the 0x08C list (P + 0x2D000, ids 2i) whose id >> 6 == cat, over the category's cap
// (file 0x59, the entry whose +3 == cat, its +4 ; Weapon Skills +5 per key item 3224 / 3225, max 100).
struct NativeMeritRow { bool header; int id, lvl, cost, cap; unsigned colName, colLvl, colCost; };
struct NativeMeritCat {
    bool open, focused;
    int  cat;                    // 0 the category list
    int  n, top, vis, cursor;
    NativeMeritRow row[16];
    int  meritsLeft;             // -1 unknown
    int  comboCat, spent, cap, spent2, cap2;   // the category under the hand (category list) or shown ; cap -1 unknown ; group 2 (jobs)
    short spentAll[96], capAll[96];            // every category's spent / cap (one walk of the list ; cap -1 unknown)
    short rx0, ry0, rx1, ry1; int uiW, uiH;
};
const NativeMeritCat& native_merit_cat();
// The main menu's Equipment (docs/notes/equipment-re-2026-11-18.md) : `equip` (the 4 x 4 grid : window +0x4C = the entry
// 1..16, column-major ; slot s -> entry {1,5,9,13,2,3,7,12,16,6,8,10,14,11,15,4}[s]), BORN parked (its numbers are drawn
// where it opened) ; `inventor` in its equipment mode (a TEXT LIST : row 0 the bag's header, record -1 ; item rows : id
// +0x2C, count +0x30, the client's colour +0x0C -- green worn, grey refused, orange held) ; Attack / Defense / the 8
// resists from the 0x061 mirror (FM_PW_BLOCK + 0x20 / + 0x22 / + 0x24..). The client sends 0x050 itself.
struct NativeEqRow { bool header; unsigned short id; int count; unsigned col; };
struct NativeEquip {
    bool open, focused;          // the grid
    int  cursor;                 // its entry 1..16
    unsigned short id[16];       // worn, by packet slot (0 main .. 15 back) ; idsOk : read
    unsigned char  ward[16];     // its wardrobe 1..8 (0 : the inventory / unknown) -- the grid's badge
    bool idsOk, statsOk;
    int  atk, def; short res[8];
    short rx0, ry0, rx1, ry1; int uiW, uiH;
    bool lopen, lfocused;        // the item list of a slot
    int  ln, ltop, lvis, lcursor;
    NativeEqRow lrow[16];
    short lx0, ly0, lx1, ly1;
    NativePanel money;           // the client's "Current Gil" box (its rect) ; gil / gilOk : the player's gil
    bool compare;                // `itemxinf` is up (LB / Ctrl on a list row) : the row's item over the one worn in the slot
    unsigned gil; bool gilOk;
};
const NativeEquip& native_equip();
// The main menu's ITEMS (docs/notes/items-menu-re-2026-11-18.md) : `inventor` opened from the main menu (handler +0x90 == 1),
// a TEXT LIST : the first `s16 handler +0x54` rows are the Temporary Items (colour 0x80806040), then the inventory in the
// client's view order ; cells : id +0x2C, count +0x30, the client's colour +0x0C (blue enchanted ready, green worn, gold
// usable, white, grey refused, orange held in Manual Sort). The Recycling Bin (`bank` open) stays the client's for now.
struct NativeItemRow { unsigned short itemId; int itemCount; unsigned itemCol; bool tempRow;
    bool hasPrice; int price; unsigned priceCol; };   // a shop's price cell (price -1 : not asked yet) ; hasPrice false elsewhere
struct NativeItems {
    bool open, focused;
    int  itN, itTop, itVis, itCursor, tempRows;   // the whole list, its page, the cursor (-1 none), the temporary rows
    NativeItemRow itRow[16];                     // the page
    int  invUsed, invCap;                        // the inventory's items / its size (invCap 0 : unknown)
    short rx0, ry0, rx1, ry1; int uiW, uiH;
    // the RECYCLING BIN (itmsort2 -> 3) : `bank` on bag 0x11 (handler +0x58 -> its delegate, bag at +8) ; the inventory
    // beside it is the client's, every row grey (nothing goes in by hand). A row validated : retrieved (0x029, the client)
    bool binMode, binFocused;
    int  binN, binTop, binVis, binCursor, binCap;
    NativeItemRow binRow[16];
    short bx0, by0; int binUiW, binUiH;
    // the QUANTITY box (`itemctrl`, a stack retrieved from the bin) : handler +0x22 the max, +0x23 the value -- the client's
    // keys change it, validate / cancel are the client's ; qtyOk : both read and plausible (else its own box stays)
    bool qtyOk, qtyFocused; int qtyVal, qtyMax;
    int  synMode;                                // Synthesis's pickers : 0 no, 1 the crystals (delegate K), 2 the materials (S, beside tskill1)
};
const NativeItems& native_items();
// The NPC SHOP (docs/notes/shops-re-2026-11-20.md, live 2026-11-21) : `shopmain` (1 Buy / 2 Sell, image labels), then `shop`,
// a TEXT LIST (rows +0x38, 0x54 B : cell c type +c, colour +0x08 + 4c, value +0x28 + 4c ; record +0x48) : col 0 the icon
// (the item), col 1 the name (its colour : grey = refused), col 2 the price + 1 (Sell : 0 until the client asked it).
// `itemctrl` the quantity. Ours under NU_NPC and only while `shopmain` is up -- the bazaar / guild shops reuse `shop` and stay
// the client's. The client does every packet (0x083 buy, 0x084 / 0x085 sell).
struct NativeShop {
    bool mainOpen, mainFocused; int mainCursor;   // shopmain : 1 Buy, 2 Sell
    bool open, focused, sell;
    int  n, top, vis, cursor;
    NativeItemRow row[16];                       // the page (hasPrice set)
    unsigned gil; bool gilOk;
    bool qtyOk, qtyFocused; int qtyVal, qtyMax;
    short mx0, my0, rx0, ry0; int uiW, uiH;
};
const NativeShop& native_shop();
bool native_shopmain_hover(int entry);
bool native_shopmain_pick(int entry);
// The main menu's SYNTHESIS (docs/notes/synthesis-re-2026-11-19.md) : `cmbmenu` (Crystal Synthesis / History, a main-menu
// submenu) ; `tskill1` = the grid (handler : +0x08 its window, +0x14 the crystal's bag index, +0x20 its id, slot i :
// +0x15+i bag index (s8 < 0 in History = not owned), +0x22+2i id, +0x36+i quantity ; +0x70 mode 0 Items-Use / 1 Crystal
// Synthesis / 2 History ; window +0x4C : 1..8 the slots, 9 Synthesize, 10 Cancel) ; `cmbhlst` = History (a TEXT LIST,
// record = a node : +6 the result item, +9 bit 7 favourite) ; `cmbhwin` the favourite on / off. Synthesize sends 0x096 at
// once (the client's checks), no Yes / No.
struct NativeSynth {
    bool open;                                   // cmbmenu up (the screen)
    bool gridOpen, gridFocused, synAllowed;      // tskill1 ; Synthesize not refused
    int  gridCursor, gridMode;
    unsigned short crystalId; bool crystalHave;
    unsigned short slotId[8]; unsigned char slotQty[8]; bool slotMiss[8];
    short gx0, gy0; int gUiW, gUiH;
    bool histOpen, histFocused; int hN, hTop, hVis, hCursor;
    unsigned short hItem[16]; bool hFav[16];
    short hx0, hy0; int hUiW, hUiH;
};
const NativeSynth& native_synth();
// Search -> Edit your comment : the client's `comment` editor (docs/notes/search-comment-re-2026-11-19.md). Handler : +0x14
// the line typed (0..2), +0x16 the mode (2 = the search comment ; 0 / 1 bazaar, 3 linkshell message), +0x17 the genre
// (major << 4 | sub), the text = 7 sets x 3 lines x 0x29 B from +0x1A (set = the mode) ; +0x378 the input object while a
// line is typed (its display copy +0x3A0, length +0x690, active +0x694 -- the same FsTextInput as the chat line : Tab
// opens `fep`). No buttons : Enter / Down = next line, Up = previous, Escape = save and close (sends 0x0E0 if changed).
struct NativeComment {
    bool open, focused, typing;
    int  cmMode, cmGenre, cmLine;
    char cmText[3][44];        // the committed lines, raw (ASCII / SJIS / FD .. FD auto-translate)
    char cmLive[0x170]; int cmLiveN;   // the line being typed, raw (7F FF = the caret)
    short rx0, ry0, rx1, ry1; int uiW, uiH;
};
const NativeComment& native_comment();
// The main menu's LINKSHELL (docs/notes/linkshell-re-2026-11-19.md) : `link5` = a TEXT LIST of every linkshell ITEM carried
// (row record = row+0x48 -> [0] bag index, [1] container, [2] kind 0 unopened / 1 shell / 2 sack / 3 pearl / 4 broken,
// [3..] the LS name) ; the client's colour +0x0C (green equipped, grey refused). The item : P + 0x9868 + (container * 0x51
// + index) * 0x2C (+0 id, extdata +0x10 : +0 LS id, +6 / +7 the colour nibbles R G / B, +8 rank). Equipped LS n (1, 2) :
// index u8 P+0x9808+n, container u8 P+0x980F+n. Picking a row opens link2 (shell) / link3 (sack) / link4 (pearl) /
// link13 (not equipped) / link1 (unopened) ; link12 = the members (scresult's records, search type 3).
struct NativeLsRow { unsigned short itemId; unsigned char kind, slot; unsigned col, rgb; char lsName[24]; };
struct NativeLinkshell {
    bool open, focused;
    int  n, top, vis, cursor;
    NativeLsRow row[16];
    short rx0, ry0, rx1, ry1; int uiW, uiH;
};
const NativeLinkshell& native_linkshell();
bool native_synth_grid_hover(int entry);   // tskill1 : 1..8 a slot, 9 Synthesize, 10 Cancel
bool native_synth_grid_pick(int entry);
// The client's item card's PAGE 2 (docs/notes/iteminfo-page2-re-2026-11-18.md) : `iteminfo` / `itemxinf` rebuild, every
// update, the item's augment text (handler +0x1B8), its "<...>" line (+0x2C : charges, or rank / path / RP / trial) and
// "<Item Level:n>" (+0x278), for the item at +0x24. Read once a frame ; AioHUD's card shows them all on one page
// (no Y / numpad - needed). Decoded to ASCII (elements spelled out, '\n' kept).
struct ItemExtra { unsigned id; char aug[256]; char line[128]; char ilvl[64]; };
const ItemExtra* native_item_extra(unsigned id);   // 0 when neither card shows that item
bool native_equip_hover(int entry);
bool native_equip_pick(int entry);
const char* native_ability_text(unsigned id, bool desc);   // the client's ability id (traits 0x600 + t) -> d_msg 55701 name / 55733 description
const char* native_merit_text(int index, bool desc);   // d_msg 55686 string 2 * index (+1)
bool native_celestite_hover(int list, int row);   // list 0 fe_equip, 1 fe_inven : the other list given the focus first
bool native_celestite_pick(int list, int row);
const char* native_faith_text(int id, bool desc);    // faith d_msg (EN 55683) string 2 * id (+1 its description)
bool native_textlist_hover(const char* name, int row);   // the client's cursor to that row (scrolling its page first)
bool native_textlist_pick(const char* name, int row);
// a player's options (scoption : Tell, PlayOnline message, Invite, Express desire to join, Cancel request) : an entry menu
const NativeEntryMenu& native_scoption();
bool native_scoption_hover(int entry);
bool native_scoption_pick(int entry);
bool native_unity_info_hover(int entry);
bool native_unity_info_pick(int entry);
const NativeUnity& native_unity();
// Status -> Currencies / Currencies 2 : the list `evitem` in its mode 1 / 2 (mode 0 = an NPC's item pick : the client's)
// -- the generic list class : rows +0x20 (8 bytes, u32 value first), cursor +0x30, top +0x38, count +0x40 (0 while
// LOADING : the client asks the server, 0x10F / 0x115, and fills it on the answer), visible +0x48 ; mode +0x88,
// loaded +0x8A (Ghidra 2026-11-18, docs/notes/status-panels-b-re-2026-11-18.md). Row r's label : currencies_gen.h.
struct NativeCurrencies {
    bool open, focused, loaded;
    int  mode, n, top, cursor, vis;
    int  value[8];               // the rows shown, top..top+vis-1
    int  all[99];                // EVERY row's value, 0..n-1 (the client holds the whole list : rows + r * 8)
    short rx0, ry0, rx1, ry1; int uiW, uiH;
};
const NativeCurrencies& native_currencies();

// What the client's Chat submenu (chatctrl) shows on the right of its rows (its draw FUN_0546E290, read 2026-11-16) :
//   Tell        the name the Tell will go to : handler+0x14 = -1 the current target (the HUD has its name), -2 the
//               last tell received, n >= 0 the n-th name of the tell history ; -3 nothing
//   Linkshell 1 / 2  the item worn (513 linkshell / 514 linkpearl / 515 pearlsack), its colour and the shell's name
//   Unity       the leader's name (the client's own text table 0xC, 0x1A2 + id)
// Filled while chatctrl is open ; `ok` false when the client's functions / globals were not found (a miss is retried).
struct NativeChatInfo {
    bool ok;
    int  tellSel;              // -3 none, -2 last received, -1 current target, >= 0 history
    char tell[20];             // the name for -2 / >= 0 (empty for -1 : the target's)
    int  lsItem[2];            // 0 = none worn
    unsigned lsArgb[2];        // the shell's colour, opaque
    char ls[2][24];
    int  unityId;              // 0 = none
    char unity[32];
};
const NativeChatInfo& native_chat_info();

// The SPELL list ("magic", opened from a Magic category), parked with NU_ACT and redrawn (live 2026-11-16 capture +
// the list base class's draw FUN_05563560) : handler h = obj+0x0C ; h+0x40 the spells listed, h+0x38 the first one
// shown (scroll), h+0x48 the rows shown, h+0x30 the one under the cursor ; h+0x20 = 8-byte items, item.int0 = a slot,
// the SPELL ID = u16 at (spellSlots + 0x80 + slot*2) (FUN_0556DD40 ; spellSlots read from its operand by signature).
// The ABILITY list "ability" (Job abilities / Weapon skills / Pet commands, FUN_05565730) is the same class : the row
// id = u16 at (abilSlots + 0x160 + slot*2), an index into the client's ability records (0x30 each) -- weapon skills
// 0..255, job abilities 512 + their id (2026-11-16, Ghidra ; menu name proven by "menu ability" in FUN_055641A0).
struct NativeList {
    bool open, focused;
    int  kind;                         // 0 the spell list "magic", 1 the ability list "ability", 2 the mount list "mount", 3 the items "inventor" (from the action menu)
    int  n, top, vis, cursor;          // 0-based list positions
    int  rows;                         // rows read below (top .. top + rows - 1)
    unsigned short id[32];             // the spell id (kind 0) / ability index (kind 1) / mount id (kind 2) / item id (kind 3) of each row shown
    unsigned short cnt[32];
    int  held;                 // manual sort : the HELD row's index in the whole list (it goes above the next row validated), -1 none            // kind 3 : each shown row's count
    int  allN;                         // EVERY spell of the list (its width is the widest of them, never the page's)
    unsigned short all[512];
};
const NativeList& native_list();

// The client's EXPANDED log "fulllog" (X then Enter in the game ; live probe 2026-11-16, docs/game-data/native-windows.md) :
// parked with NU_CHAT, AioHUD's Discussion box unfolds in its place. handler +0x54 = the tab (a category id :
// 0 Windows 1, 8 Windows 2, 3 Say, 4 Tell, 1 Party, 2 Linkshell, 7 Linkshell 2, 10 Assist E, 9 Assist J, 6 Unity,
// 5 Shout -- X cycles them in that order), +0x20 = lines scrolled back from the newest, +0x1C = the top line.
// logFocus : the client's SMALL log window holds the focus (selected, before X / Enter expands it) -- 0 "logwindo"
// (the chat), 1 "logwin2" (the battle log), -1 neither. The focused menu object's name is logged at each change
// ("NATIVEUI focus -> ...") so the window names are checked in game, not assumed.
struct NativeFullLog { bool open, focused; int tab, back, top; int logFocus; };
const NativeFullLog& native_fulllog();
bool native_sub_hover(int entry);   // cursor -> that entry, event 9 (the client's mouse hover)
bool native_sub_pick(int entry);    // hover + event 5 (validate)

// once per frame, MAIN thread (render6) : resolves, applies config changes, keeps records true. `inWorld` = a character
// is logged in : at the title / char-select screens every native window stays (or goes back) where the client puts it.
void native_ui_tick(bool inWorld);
void native_ui_shutdown();   // //unload : every record and every open window back where the client put it ; hooks removed
// one line per fact for //aio doctor (state, what is applied, why it is inactive)
int  native_ui_report(char out[][160], int maxOut);
// The client's chat input text (raw : 7F FF = caret, 7F 69 .. 7F 64 = auto-translate, 7F 66 = Tab candidate, 7F FE
// lead) into buf ; returns its length, -1 when the input line is not open, -2 when it is open but cannot be read.
// `mode` (optional) = the line's CHANNEL as the client shows it (handler+0x2C : the one picked with Right, or the
// one a typed /p /l ... selects) -- live 2026-11-15 : 2 say, 3 shout, 4 tell, 5 party, 6 linkshell, 11 unity, 12 linkshell 2.
int  native_input_read(char* buf, int cap, int* mode = 0);

// Region Info (2026-11-19) : the conquest 0x05E and the campaign / colonization 0x071 answers, kept as they arrive (the
// client itself copies them only while its own request is pending). Capture stage : each one is also dumped to the log.
void native_region_packet(int id, const unsigned char* b);
// The map's state (cnqframe) + the data, as received (docs/notes/region-info-re-2026-11-19.md s.2b-2d ; values matched
// against the client's panels 2026-11-19). `open` : the map is up ; `mode` : the panel the client shows (0 Conquest,
// 1 Besieged, 2 Campaign, 3 Colonization ; -1 none yet). The *Ok flags : that answer has arrived this session.
struct NativeRegion {
    bool open; int mode; int uiW, uiH;
    int curX, curY;                          // cnqframe handler +0x1C / +0x1E : the map cursor (the client moves its panels to the other half)
    int hover, home;                         // cnqframe handler +0x16C the hovered record id (5..23 a region ; -1 none), +0x170 the player's region
    bool conqOk; unsigned char conq[0xB4];   // 0x05E : +04 ranks, +05 alliance, +1A.. regions, +90 CP, +98 calculating, +9C Besieged present, +A0.. Besieged, +B0 imperial standing
    bool campOk; unsigned char camp[0x30];   // 0x071 type 2, its header : +0C allied notes, +10 controlled-area counts (4 x 5 bits)
    bool coloOk; unsigned char colo[0x40];   // 0x071 type 3 : +10 coalition ranks (6 nibbles), +38 bayld
    bool campAreaOk[26]; unsigned char campArea[26][12];   // 0x071 type 2's area records (pkt+9 the first, 13 a half) : dw0 bit 0 battle, 1-3 owner, 4-13 / 14-23 fortifications / resources, dw2 0-9 / 10-19 their maximums ; bytes 4..7 the influence gauges (x0.4 %)
    int stronghold;                          // Besieged : the stronghold the client's beseige2 shows (+0x3C : 0 Mamool Ja, 1 Halvung, 2 Arrapago ; -1 none)
};
const NativeRegion& native_region();
// DEV (//aio movediag, aiohud_probes.cpp) : log the next n move-to calls on the windows AioHUD offsets (NUIDIAG lines :
// caller, request, rect before / after) -- off otherwise ; 0 disarms
void native_ui_movediag(int n);
int  native_ui_winlist(int screenW, int screenH);   // every open window logged (WINLIST) -- the dev probe //aio winlist

} // namespace aio
