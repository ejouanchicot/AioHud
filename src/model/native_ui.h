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
    NU_HELP   = 1 << 3,   // helpwind (description bar) + titlewin (current menu name)
    NU_COST   = 1 << 4,   // subwindo : the MP-cost / recast case above the party (AioHUD's Cost box replaces it)
    NU_NET    = 1 << 5,   // netstat (connection indicator) + polwindo (POL message icon), top-right corner
    NU_ACT    = 1 << 6,   // playermo : the target ACTION menu (Enter on a target) -- AioHUD draws its own (hud_actmenu)
    NU_NPC    = 1 << 7,   // rem4line / rem4li2 (NPC dialogue) + query (its answers) -- AioHUD draws them above its chat
};

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
    unsigned char entry[16];   // 1-based entry index of each shown row, top to bottom
    unsigned char state[16];
    int  cursor;               // 1-based entry index under the cursor
    short rx0, ry0, rx1, ry1;
    int  uiW, uiH;
};
const NativeEntryMenu& native_sub_menu();

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
    int  kind;                         // 0 the spell list "magic", 1 the ability list "ability"
    int  n, top, vis, cursor;          // 0-based list positions
    int  rows;                         // rows read below (top .. top + rows - 1)
    unsigned short id[32];             // the spell id (kind 0) / ability index (kind 1) of each row shown
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

} // namespace aio
