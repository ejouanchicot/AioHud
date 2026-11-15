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
};

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
