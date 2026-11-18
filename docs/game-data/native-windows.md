---
title: The client's own windows — the menu manager, layout records, and how AioHUD hides / moves them
summary: Every native FFXI window (party, alliance, chat, help bar, every menu) is a named menu in one manager. Its definition list, the layout record a window is created from, the client setter and "move-to" function, the handler refreshes, and the traps found while proving all of it live (2026-11-15).
source: model/native_ui.{h,cpp} (shipped) ; src/plugin/aiohud_probes.cpp mlist/mmove/mhold/mdefs/mslot/mtrace/mspy/mall2 (dev probes) ; Ghidra on re/ffximain_dump.bin (June) and re/ffximain_dump_20261115.bin
---
# The client's own windows

> **Sondes citees ici.** Les commandes `//aio` nommees dans cette page ne sont pas toutes livrees.
> `mlist`, `mmove`, `mhold`, `mdefs`, `mslot`, `mtrace`, `mspy`, `mall2`, `mcap`, `mcapt`, `mitems`, `inputget`, `inputfind` : sondes de developpement (`src/plugin/aiohud_probes.cpp`, non suivi par git) — cela repond sur la machine de dev, **jamais dans une release**.
> Ce qu'une release porte vraiment : la page *Interface du jeu* de `//aio config`, et `//aio doctor` (ligne `gameui`) quand quelque chose ne va pas.

The wall every FFXI UI addon hit ("removing the original party list does not seem possible" -- XivParty on
Windower ; an Ashita addon that hid it lost the game's target cursor). AioHUD gets past it because it reads and
calls the client directly, and because it draws its own cursor. Proven live on 2026-11-15 ; the full session log
is the investigation note (`docs/notes/native-windows-investigation-2026-11-15.md`, gitignored).

## 1. One manager, two lists

Every native window is a **menu object** whose definition carries `"menu    "` at **def+0x46** and its 8-char
name at **def+0x4E**. One manager owns them :

```
manager = FM_MENU_PTR address - 0x54            (FM_MENU_PTR = the focused-menu slot, ffximain-statics.md)
*(manager + 0x00) -> list of OPEN menu objects   (FUN_05DBDB20 find-open-by-name)
*(manager + 0x1C) -> list of every loaded DEFINITION (FUN_05DBDF40 find-def-by-name) -- 409 on the live client
node : +0x00 next, +0x10 value (object or definition), byte +0x14 dead
```

The manager is never assumed : `native_ui.cpp` requires >= 50 tagged definitions on its +0x1C list before it
acts. Open / close by name : FUN_05DBE260(name, show, ?) / FUN_05DBE820(name) (June addresses).

**Menu object** (0xD4 bytes) : +0x04 def, +0x0C handler, +0x10 state (0xE = closed-kept), +0x34 flags,
**+0x3A x / +0x3C y / +0x3E right / +0x40 bottom (short, screen px)**, +0x6E closing.
The +0x8C x,y,w,h copy is **garbage on raid1** -- read +0x3A.

**Names that matter** : `partywin` (party -- NOT `ptw0`..`ptw6`, which never appear live), `raid1` / `raid2`
(alliance), `targetwi` (target bar, anchored ABOVE partywin by the client), `logwindo` / `logwin2` (chat),
`inline` (chat input), `query` (NPC answer choices), `helpwind` / `titlewin` (help bar / menu name), `menuwind`
(main menu), `subwindo` (MP cost / recast case), `equip`, `inventor`, `persona`, `iteminfo`, `magic`,
`mgcmenu`... The 409-definition map with every record is `docs/notes/native-menu-defs-2026-11-15.txt`.

## 2. Writing the rect moves nothing -- the client SETTER does

Writing +0x3A.. changes nothing on screen : the frame and text are child items with their own positions.
The client's setter moves a window AND its children :

```
FUN_0545BA30  (2026-11-15 ; June FUN_05D7A4D0)   thiscall(obj, x, y, w, h, 1, 0, 0)
AOB 83 EC 08 56 8B F1 8D 4E 14 E8 ?? ?? ?? ?? 85 C0 89 44 24 08 C7 44 24 04 00 00 00 00 0F 84   (unique)
```

## 3. A window that REOPENS is placed from its LAYOUT RECORD

A one-shot move is lost when the client rebuilds a window (`inline` at every keystroke, the main menu, a party
size change). Creation (FUN_05D77E70) and re-layout (FUN_05D7A820 ; it calls the setter from FFXiMain+0x11BE91)
read the window's **layout record** :

```
rec = **(def + 0x08)      +0x02 x, +0x04 y, +0x0A w, +0x0C h (512x448 VIRTUAL screen),
                          +0x13 anchor : 0 top-left, 1 right (x += W-512), 2 bottom (y += H-448), 3 both
screen size used for the anchors = the shorts at manager+0x80 (lo W, hi H), as FUN_05D7A820 reads them
```

Shift the record and the window **opens** at the new place -- no pop at the old spot. All 409 definitions are
shifted without opening any menu. Menus share a few slots (right column 384,48 anchor 1 = 118 menus incl.
`menuwind` ; top-left panels 16,48 / 130,48 / 16,240 ; big lists 16,96 anchor 2).

**Building a window** (FUN_054593D0, from its definition) : the frame from def+0x08 ; then one child per ENTRY,
`entry i = *(def+0x04) + (i-1)*0x14` for i <= the short at **def+0x18** (FUN_0545C770) ; then the pointing HAND
from **def+0x0C** -- an animated sprite set named `"anc     anc_s   "` (FUN_0545B2B0 / B3B0 cycle its frames),
placed on the entry under the cursor (FUN_0545B2C0). def+0x0C is NOT the scrollbar (the ASCII read of 2026-11-15).

**The scrollbar** is `obj+0x88` (0x2C bytes : x, y, h, ...), made and placed by FUN_054599C0 from the window's
OWN rect : x = right - 8, y = top, h = bottom - top. The setter re-puts it at x = right - 6, y = top (a 2 px
difference). A list's scrollbar offset under a menus offset is therefore not explained statically yet -- capture :
`//aio mcap magic` logs rect vs scrollbar.

## 4. Windows placed by their SCREEN's code : the "move-to" function

`persona`, `inventor` and `iteminfo` ignore their record : the Equipment screen puts them at 16,48 / 314,48 /
130,240 itself (the inventory alone has 17 placement modes). They go through ONE function (~40 callers) :

```
FUN_0545A5A0  thiscall(obj, x, y) -- "move this menu to x,y" ; sets the setter, then calls handler slot 5
AOB 83 EC 0C 66 8B 44 24 14 53 55 8B 6C 24 18 56 8B   (unique) ; it calls the setter from FFXiMain+0x11A611
```

`native_ui.cpp` hooks it (5-byte JMP over its first 8 bytes, a trampoline replays them) and adds the window's
offset BEFORE it runs : no frame is ever drawn at the old place. `logwin2` goes through it every frame.

A per-frame "net" that corrects after the fact was tried first : it always leaves a one-frame pop the player
sees, and Windower's render slot 5 is NOT a pre-draw hook (same thread, after the game's draw, like slot 6).

## 5. Handler refreshes -- content that caches its origin

After the setter, some windows still draw their CONTENT from a copy of the origin taken when they opened :
- **query** (NPC choices) : the QUESTION ("Enter the premise?") is drawn at controller+0x540/+0x542 (+4,+7) ;
  the prompt text sits in a static message buffer (now FFXiMain+0x48A2B8, flag +0x7F0) parsed by FUN_05D12520
  (text before 0x0B = question, 0x07-separated = choices). Handler **vtable slot 5** (+0x14) refreshes the copy
  -- it is what the client itself calls on every re-show (FUN_05DBE260).
- **titlewin** (menu name) : slot 5 is EMPTY ; the text origin is copied ONCE by **slot 1** (FUN_05492220 :
  rect -> handler+0x14, text lines -> +0x9C/+0xA0, +0xFC/+0x100 ; also resets a mode +0x448 and timer +0x44C).
  Re-run slot 1 after moving it.

## 6. Hiding = parking

A hidden window gets +3000 px on y through the same two paths : it keeps working (party cursor and `<stpt>`,
chat log, NPC choices), it is simply never on screen. Moving `partywin` also moves `targetwi` (the client
anchors it above) -- hiding the party hides the game's target bar. The top-right corner is `netstat` (connection
indicator, 384,16 anchor 1) and `polwindo` (POL message icon) : their own option, never the menus offset.

**The input line docks under AioHUD's chat box** (UiConfig::chDock) : `inline` and its channel variants (not
`passinpu`) get the offset that puts them at the box's bottom-left (above its top when there is no room below).
Records are in UI UNITS, not backbuffer px -- the UI is scaled (a 929-high UI on a 1400-px backbuffer, 2026-11-15) :
the UI size is the pair of shorts at manager+0x80, and px -> units = W / backbufferW, H / backbufferH.

**The input line's TEXT** (2026-11-15, Ghidra + live) is not in any memory a search finds as letters (bytes, UTF-16
and every stride 3..32 were scanned with the line open : nothing). The `inline` handler's draw (vtable slot 4,
FUN_054F95A0) ASKS for it every frame : `(*(handler+0x1C))->vtbl[4](buf, 0x168)` on an "FsTextInput" object,
thiscall (ecx = the object, callee pops), returns the length. The text carries the caret as `7F FF`, may start
`7F FE`, an auto-translate word as `7F 69 .. 7F 64`, the Tab candidate as `7F 66 ..`, and ends with one space after
an end-of-line caret. The line's CHANNEL is handler+0x2C (picked from the self-target menu's Chat -> Right, or
set by a typed /p /l ...) : 2 say, 3 shout, 4 tell, 5 party, 6 linkshell, 11 unity, 12 linkshell 2 (live).
`native_input_read` calls exactly that (getter + vtable checked inside FFXiMain's image) ;
UiConfig::chDock 2 parks `inline` off-screen and the Chat box draws the text -- 3 unreadable frames and the line
comes back docked under the box (never type blind).

**Only while a character is logged in.** The title, login and character-select screens are menus too (their
scrollbars moved under a menus offset, 2026-11-15) : the engine applies nothing until `Hud::logged_in()` (a player
poll succeeded, and has not failed for ~2 s -- a zone does not count as a logout) and puts everything back ~2 s
after a logout.

## 7. Traps, each paid for once

- **Two command collisions** : `//aio ... partywin` is eaten by the shipped `//aio party` handler, `menuwind` by
  the `menu` probe. Probe aliases `pt`, `main`.
- **A record shift outlives the plugin.** It is GAME memory : a reload lost the probe's undo list and the main
  menu stayed 300 px low until a fix. The shipped engine restores on `//unload` AND writes
  `data\native_ui_undo.txt` while anything is applied ; the next load restores from it (a crash cannot strand
  the player's windows).
- **Learn a native position only from an on-screen value.** A hold learned its own off-screen target as
  "native" and `mback` then parked the chat at y=6044.
- **Object addresses are REUSED.** An entry kept after its window closed forced its old target on a new window
  (Equipment drifted after a few open/close). Key on the record, forget what is gone.
- **The +0x8C rect copy** fed the setter garbage for raid1 and threw the alliance window off-screen.

## 8. The main menu (`menuwind`) -- static findings, 2026-11-15 (fresh dump, base 0x05340000)

- **Entry ACTIONS** : FUN_0548FD80 = slot 5 of a controller vtable at 0x05677524, `(code, index)`. Code 5 = an entry
  was validated ; index 1..15 opens : 1 `missionm`, 2 `/quest`, 3 `/keyitem`, 4 `mogcont` (Mog House checks first),
  5 `bazaar`, 6 `mcrmenu` (macros), 7 `configwi`, 8 FUN_05558AD0, 9 `commenu`, 10 `/clock`, 11 FUN_054A1F30,
  12 FUN_054A1FC0, 13-15 forwarded to the controller's slot 6. Menus open through FUN_0549F920(name, 1, 0) (open by
  name), text entries through the client's chat-command function (`/quest` is literally typed). Codes 3/4/7/0x1C
  re-show `menuwind`, 6 closes it.
- **LABELS are not in FFXiMain** : the strings come from the `menu    windowps` resource (a string table,
  `*res + id*4`). The table right after the `"menu    menuwind"` string (0x056B2BB4) lists the ids (0x11B, 0x11C,
  0x121, 0x122, 0x11D...) ; FUN_05490E10 (a menuwind-side panel, vtable 0x05676000) copies 27 of those string
  pointers into its object at +0x38.. (two id tables).
- **ENTRIES live in the DEFINITION** (proven live, `//aio mitems`; the general rule is section 3 : *(def+4), count
  def+0x18) : menuwind's array sits at def+0xC4, 15 entries x 0x14 : +0x00 the entry's own layout record (x/y inside the window), +0x04 ?, +0x08 HELP text, +0x0C LABEL,
  +0x10 flags. Readable without opening the menu. Order in the array is NOT screen order -- the record's y is :
  1 Status, 2 Equipment, 3 Magic List, 4 Items, 10 Synthesis (y=70), 5 Abilities, 6 Party, 7 Trade, 8 Search,
  9 Linkshell, 11 Region Info, 12 Map ; 13-15 have no text (page arrows / hidden).
  With the client's rule : all 409 definitions read, 306 have entries, 2323 entries, 963 with text (dump :
  docs/notes/native-menu-items-2026-11-15.txt). Some menus draw their label as an image -- label empty, help text
  present (configwi : "Adjust the filter settings for text in the log window.").
- **A list re-places itself RELATIVE to its current spot** (FUN_05563DE0 : rect via FUN_0545A400, then
  move-to(x, y + delta), call at FFXiMain+0x223E23) : the move-to hook added the menus offset a SECOND time (magic
  list at +2 dx, 2026-11-15 capture), and the list's scrollbar stayed one offset behind.
  The culprit is **FUN_054A0D60** (STDCALL(holder, list) -- ret 8 ; hooking it as cdecl crashed the client --, 3 callers) : it places a list beside its parent menu with
  x = the list's CURRENT x, a computed y, calls move-to (FFXiMain+0x160E86), then writes the scrollbar ITSELF from its
  own numbers (old right - 8, the y it asked). Engine fix : FUN_054A0D60 is hooked too (7-byte prologue) -- during it,
  move-to adds only the y offset ; after it, the scrollbar is put back by the client's rule (right - 8, top).
  FUN_05563DE0's move-to call (return FFXiMain+0x223E28) is relative on both axes : no offset added. Both are found
  by AOB and verified by their CALL to move-to ; missing -> logged, the rest still works. Trace : NUIDIAG lines.
  Lists filled at runtime (spells, items) are not in the definition.
- **CURSOR** = open menu object **+0x4C**, the 1-based index into that array (Synthesis selected = 10) ;
  **+0x08** = the live item object under the cursor. No greyed entry was available to capture.
- **def+0x0C** = the pointing hand's sprite set (section 3), not the scrollbar.
- **Two pages, two menus** (live capture MENULOG 2026-11-18) : page 1 `menuwind`, page 2 **`socialme`** (Missions,
  Quests, Key Items, View House, Set Bazaar, MacroPalette, Config, Help Desk, Communication (entry 9, y=150), Current
  Time (entry 10, y=134), Shut Down, Log Out). Left / right closes one and opens the other at the SAME rect, the cursor
  keeping its number. Both are entry-children menus (section 9's submenu shape) : entries 1-12 the rows (child +0x10
  state : 3 / 0xB on the picked one, 0 else), 13 / 14 the page arrows (15 px wide, state 0xF), 15 hidden (state
  0x10). The client draws its labels as IMAGES that differ from the definition's texts ("Magic", "Bazaar", "Macros").
  The rect sits top RIGHT (x 2199-2311 of a 2327-wide UI, y 48) with titlewin / helpwind over it.
  AioHUD mirrors both (Game UI -> "Main menu", `NU_MAIN`, `native_main_menu()` + `mainmenu_draw`), the same fallback
  as the action menu (3 unreadable frames -> the client's own back).
- **What Magic opens from it** (live 2026-11-18) : `mgcmenu` (8 entries, labels are IMAGES -- the help texts give
  White, Black, Songs, Ninjutsu, Summoning, Blue, Geomancy, Trust), drawn by the client IN PLACE of the main menu ; then
  the spell list `magic` (the action menu's own list, at the same place : above the chat) with the sort options in the
  menu's corner : **`mgcsortw`** (Auto-sort, Manual Sort ; X or numpad + gives it the cursor, which the client puts back
  on the option used last) or **`blusortw`** for Blue Magic (+ a 3rd row, Blue Magic : set the spells). Auto-sort asks
  **`sortyn`** (Yes / No) -- placed by its CODE, not under a parked parent ; the same window confirms the inventory's
  and the Mog House's sorts. **Manual sort** : the list handler's **+0x6C** = 0 off, 1 on, 2 a spell held ; **+0x70** =
  the held one's index in the list ; validating another row puts the held one ABOVE it (capture MSORT).
- **Status** opens `statcom2` (13 entries, labels are images : named from their help texts) in place of the menu, and
  beside it the client's `persona` (the stats, 16,16) and `level` (Master Level / Exemplar, 16,208).
- **Status -> Profile** (`profile`, 130,48 ; no entries -- a panel) : its flag, rank bar and icons are drawn where it
  OPENED and do not follow a later move -> it must be BORN parked (its record, `g_profDataOk`). Its data, measured
  2026-11-18 on two characters (PROFLOG / PROFCMP) : the FM_PW_BLOCK mirror holds the WHOLE 0x061 body from
  block -0x10 -- nation pkt 0x50, home point 0x4A, rank 0x46, rank points 0x48 (cap 0xFFF), **Mastery Rank = pkt
  0x64 & 0x0F** (Tetsouo 0x07 = 7, Kaories 0x15 = 5) ; race = self entity +0xEF (3 Elvaan male).
  **Mentor + Monstrosity** (Ghidra 2026-11-18, the profile draw FUN_05488F90, handler vtable 0x05675998 ; notes in
  docs/notes/profile-monstrosity-re-2026-11-18.md) : packet **0x063 order 3** copies 0xD4 bytes from its +0x08 into a
  FFXiMain static ; **Gladiator rank = (u32 @+0x0C >> 2) & 3**, named by the client's own table ("Gla", "CG", "HCG") ;
  **Prestige = u16 @+0x14** ; a gate byte (block + 0x1C0) set once orders 3 and 4 arrived -- 0 : the client draws no
  Monstrosity. **Mentor = byte *(player ptr) + off** (packet 0x01B +0x65 : 0 none, 1..3 the flag ; the next byte is the
  Mastery Rank). All found by SIGNATURE in `mon_res()` (the addresses moved between the June and November clients),
  adopted only when prestige = rank + 8 and gate = rank - 4 + 0x1C0 hold ; resolved live at the first try.
- **Status -> Job Levels / Master Levels** (Ghidra 2026-11-18, docs/notes/job-levels-re-2026-11-18.md) : `joblevel`
  (placed by code through move-to, at 130,48 ; the Mog House job change REUSES it at 16,300 with a cursor -- so it is
  ours only while `statcom2` is up) and `masterle` (its record). Two columns of jobs in id order, row by row ; main job
  gold, support steel blue ; unlocked jobs / mastered jobs. 252 wide, ((n + 1) / 2) * 14 + 22 high. Data : the player
  object P = *(&P) kept by the 0x01B handler -- levels P+0x1A660+j, mastered mask u32 P+0x1A678, master levels
  P+0x1A67C+j, the raw block (main +4, sub +7, unlocked mask +8) P+0x1A61C -- all from signatures in `jobs_res()`,
  adopted when the same &P, ML = mastered + 4 and levels = block + 0x44 hold. User-confirmed in game.
- **Status -> Combat / Magic Skills** (Ghidra 2026-11-18, docs/notes/skills-re-2026-11-18.md) : `btlskill` (ids 1..31)
  and `mgcskill` (32..47), placed by their records ; two columns row by row, the level right-aligned, BLUE when capped ;
  a skill is hidden only when its value is exactly 0x8000 (a job's unusable skills read 0x8010 and show "16" -- the
  client's own rule, kept). Data : packet 0x062 +0x80 (64 u16 : level & 0x7FFF, bit 15 capped) copied to a static at
  FM_PW_BLOCK + 0x60 (both clients). User-confirmed in game.
- **Status -> Crafts / Merit Points / Job Points / Play Time** (Ghidra 2026-11-18, docs/notes/status-panels-a-re-2026-11-18.md
  ; Crafts and the Merit panel user-confirmed in game 2026-11-18) : `trdskill` = the same 0x062 array, ids 48..62, rank v & 0x1F, level (v >> 5) & 0x3FF,
  bit 15 capped, hidden at 0xFFFF ; one column name / rank / "(level)". Merit Points opens the menu `merit1` and the
  panel `levmerit` (Limit Points "lp/10000", merits "n/max P" -- FM_PW_MERIT) ; Job Points opens the list `merit2ca` +
  `levmeri2` (the JP block = FM_PW_MERIT + 0xA). Play Time opens NO window : it types `/playtime` (a chat line).
- **Status -> Currencies / Currencies 2** (docs/notes/status-panels-b-re-2026-11-18.md ; user-confirmed in game 2026-11-18) :
  ONE list `evitem` in mode 1 / 2 (mode 0 = an NPC's item pick, left to the client) ; it REQUESTS its data (0x10F /
  0x115, retried every 30 refreshes) and draws an empty frame until 0x113 / 0x118 fill it (nothing kept in a static).
  Generic list fields : rows +0x20 (u32 value first, 8 bytes), cursor +0x30, top +0x38, count +0x40, visible +0x48 ;
  mode +0x88, loaded +0x8A. Row labels : the client's TABLE1 / TABLE2 -> src/model/currencies_gen.h.
  AioHUD draws ONE searchable list of every row (all 99 / 97 values are in the rows array) in place of the stat panels
  (the client hides Profile / stats then) ; typing while it has the focus filters it (aio::currencies_key, the plugin's
  key slot ; the numpad and arrows stay the game's).
  Alter Ego (`fe_main`, faith data from incoming 0x08E) is mapped in the notes, not redrawn yet.
- **Status -> Unity** (docs/notes/status-panels-b / unity-info / unity-lists-search-re-2026-11-18.md ; user-confirmed in
  game 2026-11-18) :
  - `ut_menu` (Unity Info / Ranking Info / Unity Lists, labels are images) opens OVER statcom2 : a main-menu submenu
    (MAIN_SUB_W ; entry_read prefers the focused one, else the one listed LAST). `ut_point` (Unity Points "n/99999" :
    0x061 +0x58 = FM_PW_BLOCK + 0x48, bits 0..4 the Unity, 5..9 rank, 10..26 points) is shown in the PARTY's Cost box.
  - Unity Info / Ranking Info = `quest00` (Records of Eminence's window) in handler mode +0x68 == 1 / 2 (never reset on
    close : ours only while ut_menu is open). The Unity's leader = d_msg cat 0xC #402 + id. Rankings : incoming 0x063
    order 7, two 0x1000-byte lists ; score of a Unity = T[2][id] / T[1][id] (dwords at list + (k*0x20 + id)*4),
    competition ranking. Personal / Partial Personal Evaluation = 0x061 +0x5C high / low u16. Loading : the stats-refresh
    flag bit 0 clear or a list's loaded byte 0. Addresses self-healed from three AOBs with cross-checks (unity_res).
    Chat setting rows = entry children 1 Active / 2 Inactive ; the setting chosen at handler +0x6E (the client sends
    0x118 itself). Ranking Results / Partial = `quest01` (mode 5 / 6), its 11 rows computed from the same lists.
  - Unity Lists = `arealist` in mode +0x6C == 4 (12 rows : own Unity, the 11 leaders) then `scresult` (the people
    found) and `scoption` (a player's options). Both lists are the client's multi-column TEXT LIST (vtable 0x0567B878) :
    handler +0x08 its window, +0x18 visible, +0x1E top, +0x20 count, +0x38 rows (0x54 B, record at +0x48), +0x3C the
    empty-list text ; cursor row = window +0x4C - 1 + top ; codes 0x16 / 0x17 scroll one row. scresult records : type
    at +0, the header (type 1, " [Searching]" -> " [N]") first, players (type 0) with their entry at +4 (name +4, jobs
    +0x20 / +0x21, levels +0x22 / +0x23, ML +0x24, zone +0x28, flags +0x30 : 0x4000 anonymous). The search runs on the
    search server (no map packet) : driven only through the client's own validate. The main menu's Search reuses these
    windows : not redrawn yet (ours only under ut_menu).
- **Status -> Job Points / Alter Ego / Merit Points** (docs/notes/job-points-, celestite-, merit-cat-re-2026-11-18.md ;
  user-confirmed in game 2026-11-18). Every list here is the client's multi-column TEXT LIST (see Unity Lists) and is
  redrawn from its own row CELLS (type / value / colour) -- the numbers and the greyed / maxed colours are the client's.
  Names and descriptions come from the client's d_msg files, read once by AioHUD itself (EN 55694 the JP table, 55674
  the JP gifts, 55683 the faith table, 55686 the merit table ; native_ui.cpp dmsg_*), never from its lazy string cache.
  - Job Points : `merit2ca` (row id = job) -> `jbpcat` (tab +0x58 : categories 0x35..0x38 / gifts 0x39 / 0x3A ; codes
    3 / 4 swap tabs). JP / CP / spent : FM_PW_MERIT + 0x0A (flag byte FM_PW_MERIT - 2 & 4). `levmeri2` is BORN parked
    (its numbers are drawn where it opened). bluehelp (the description) parked while ours shows.
  - Alter Ego : `fe_main` submenu ; `fp_cat` -> `fp_cat2` (cells 0x49..0x4C) ; points u16 P + 0x2D614 (0x08E, loaded
    bit 4) via the client's accessors (AOB F / G) ; `fp_info` born parked. Change Celestite : `fe_equip` (slots : 0x0E
    badge, 0x19 Celestite ; +0x58 type, +0x54 picked) and `fe_inven` (0x4D icon, 0x19 name, client colours) ; code 8
    switches the lists ; Celestite n = item 0x72F0 + n (name / description from the item DAT).
  - Merit Points : `merit1` / `merit2` / `merit3` submenus (merit2's greyed entry = the current mode, shown selected) ;
    `meritcat` (+0x50 the category ; cells 0x1E name, 8 level + 1, 0x1F cost + 1, header rows id -1 with cell 7 = the
    group cap). Spent per category : the 0x08C list in P (+0x2D000 / count +0x2CFFE, AOB MLIST_GET ; entries {u16 2i,
    u8 cost, u8 level}, cat = id >> 6) ; caps : file 0x59 (12-byte entries, +3 cat, +4 cap ; Weapon Skills +5 per key
    item 3224 / 3225). `meritinf` born parked.
  - Status's own test (inStatusNow) accepts merit1 / merit2 / merit3 as the submenu : they are main-menu submenus too.
- **The main menu's Equipment** (docs/notes/equipment-re- / iteminfo-page2-re-2026-11-18.md ; user-confirmed in game
  2026-11-18) :
  - `equip` = the 4 x 4 grid, BORN parked (Attack / Defense / the 8 resists are drawn where it opened). Cursor = window
    +0x4C, the entry 1..16, column-major ; packet slot s -> entry {1,5,9,13,2,3,7,12,16,6,8,10,14,11,15,4}[s]. Worn
    items : read_equipment ; each slot's bag at handler +0x34 + s*4 (8, 10..16 = Mog Wardrobe 1, 2..8 : the badge).
    Attack / Defense / resists : FM_PW_BLOCK + 0x20 / + 0x22 / + 0x24 (s16 x 8, drawn column-major : Fire / Ice ...).
    Encumbrance : party().encumbrance() (0x01B), the Equip Viewer's red cross.
  - `inventor` in its equipment mode : a TEXT LIST, row 0 = "Storage Options" (record -1 : opens `mnstorag`, a main-menu
    submenu : Inventory, Mog Wardrobe 1..8), item rows : id +0x2C, count +0x30, the client's colour +0x0C (green worn,
    grey refused, orange held). Drawn as tall as the grid's box. The client sends 0x050 itself.
  - `money` (Current Gil) redrawn from read_player_gil. `iteminfo` / `itemxinf` (the card, the compare card -- LB /
    Ctrl, it TAKES the focus) : while Equipment is up, a record rule parks them as the client places them (it re-places
    them at every item and page : a park after the fact flashed them).
  - The card's PAGE 2 : the client rebuilds, every update, in iteminfo / itemxinf's handler : +0x24 the item, +0x1B8
    the augment text, +0x2C the "<...>" line (charges / rank / path / RP), +0x278 "<Item Level:n>". AioHUD's card shows
    them all on one page (it grows) : no Y / numpad - needed. The client's text : 81 60 = '~' (and the other
    full-width signs, item_text_detail::decode).
  - `//aio eqpreview` : every cell / row state on the real items (badges 1..8, locked, empty, the row colours).
- **Reload with a window parked** : move_open no longer parks an open window that is already parked (nor unparks one
  already on screen) -- a reload with Status open parked the stat panels by name, then the first apply parked them a
  second time (the box read 3000 too low, on the chat).
- **Status's stat panels** `persona` (16,16 ; also the Equipment screen's) and `level` (16,208) : BORN parked like the
  Profile, drawn at the client's own size from the same 0x061 mirror (max HP / MP 0x04 / 0x08, jobs 0x0C, attributes
  0x14 / 0x22, Su 0x52, iLv 0x54, Master Level 0x65, Exemplar 0x68 / 0x6C) + the live HP / MP / TP.

## 9. The target ACTION menu (`playermo`) -- 2026-11-16 (captures + screenshots + static, base 0x05340000)

What Enter on a target opens. ONE menu for every target ; AioHUD mirrors it (Game UI -> "Action menu", `NU_ACT`,
`native_action_menu()` + `ui/hud_actmenu.cpp`) : the client's own window is parked (section 6) and keeps its input.

- Handler vtable FFXiMain+0x334418, init FUN_0546C1A0. **Rows = handler+0x1C, u16 COMMAND CODES ; count = byte
  handler+0x53.** "Check" is IN the list (an earlier "appended last" reading was wrong).
- **Cursor** = obj+0x4C, 1-based screen row. **Rect** = obj+0x3A (x0, y0, x1, y1, UI units ; the UI screen size is
  manager+0x80). The client grows the menu upward from its bottom-left.
- Codes (each proven by a capture whose rows matched a screenshot of the same menu) :

  | code | row | seen in |
  |---|---|---|
  | 1 | Attack | mob, not engaged : 1 8 7 25 9 11 |
  | 3 | Disengage (submenu) | engaged : 18 8 7 25 9 3 11 |
  | 5 | Chat (submenu) | player : 5 8 7 25 9 19 10 |
  | 7 / 8 / 9 / 25 | Abilities / Magic (submenu) / Items / Trust | all |
  | 10 / 11 | Check (player / mob) | |
  | 12 | Treasure (the pool) | appended only while items are in the treasure pool |
  | 18 | Switch Target | engaged |
  | 19 | Trade | player |

  Unknown yet : 2 4 6 13-17 20-24 26 27 (Invite, Treasure, Request, Fish... : capture + screenshot when met ;
  the mirror shows an unknown code as `#n`, never a guess).
- Code table **DAT_056B0278**, stride 0xC, codes 1..0x1B : +0 u16 label IMAGE index, +2 byte image list (0 / 1 =
  handler+0x14 / +0x18), +4 u16 text id (Attack 31, Disengage 32, Chat 23, Abilities 24, Magic 25, Items 26,
  Check 29 / 30, Switch Target 37, Trade 38, Trust 718), +8 a function. The NAMES are not in the exe : the labels
  are images, their texts come from a DAT.
- Submenus are SEPARATE menus (seen 2026-11-16) : `chatctrl` (Chat), `magselec` -> `magic` (Magic), `abiselec` ->
  `ability` / `mount` (Abilities). While one is open `playermo` stays open but loses the focus.
- **Parking a parent parks its submenus** : the client places each submenu (and `subwindo`) beside its parent, so
  with `playermo` parked they were all born HIDE_DY below the screen ("on n'a pas les sous-menus", 2026-11-16).
  `rescue_children()` (native_ui.cpp) brings back, through the setter, any open window we did not hide that sits
  that far below ; live : abiselec, chatctrl, magselec, magic, subwindo each logged "brought back on screen".
- **Every menu event goes through FUN_054596F0(menu object, event)** (thiscall, ret 4 ; found by its unique 21-byte
  prologue `51 55 56 8B F1 57 83 7E 10 0E 75 09 5F 5E 33 C0 5D 59 C2 04 00`). It plays the sound, moves the cursor
  (events 1-4 = arrows) or validates (5), cancels (6), takes the cursor as already set (9 = the MOUSE hover : the
  client writes obj+0x4C first), then forwards (event, cursor) to the handler's slot 6 (playermo : FUN_0546D110 ;
  5 -> FUN_0546CC80(code), the command runner). Live 2026-11-16 (`//aio mhook`, all from FFXiMain+0x119964) :
  32 open / refresh, 1 key move, 9 hover, 5 validate (then 29, 32), 6 back from a submenu, 2 cursor re-fit.
  The mirror's mouse = the client's : hover writes obj+0x4C then event 9, a click adds event 5 (native_ui.cpp).
- **Entry submenus** (`magselec`, `abiselec` ; mirrored with NU_ACT, `native_sub_menu()`) : their rows are the open
  object's ENTRY children (obj+0x14, child short type 1) in list order -- the cursor obj+0x4C counts THAT order
  (1-based), the screen order is the child rect's y (+0x34). child+0x10 = state : 0 normal, 1 validated, 3 under the
  cursor, 5 refused, 0x10 skipped (abiselec's 6th entry). Labels from the definition's help texts : magselec White /
  Black / Songs / Ninjutsu / Summoning / Blue / Geomancy ; abiselec Job abilities / Weapon skills / Ranged attack /
  Pet commands / Mount. No arrow on their rows (each opens its LIST). Placement, as the client : `magselec` BESIDE
  playermo at the same height, `abiselec` IN PLACE of it (same left, bottom anchored, its own height).
  `chatctrl` (Chat) has no help texts : not mirrored until its rows are named from a screenshot.
- **Chat's extras** (its draw FUN_0546E290 ; found by signature, globals read from the code's operands) : Tell =
  handler+0x14 (-1 the target, -2 the last tell received = tellObj+0x10E, n = the history, tellObj+0x8C + n*0x10,
  count +0x88 ; tellObj from FUN_0546E610's operand) ; Linkshell 1/2 = the item worn per the inventory's
  [+0x9808+n] index / [+0x980F+n] bag, record (bag*0x51+index)*0x2C + 0x9868 (513 shell / 514 pearl / 515 sack),
  colour nibbles at ext+6/7, name decoded from ext+9 by the client's FUN_0550A580 ; Unity = the id global & 0x1F,
  the leader's name = text table 0xC, 0x1A2 + id (FUN_054944F0, cdecl). Left / right on Tell cycles the name.
- **Abilities swaps at once** (trace 2026-11-16 : abiselec readable the very frame playermo loses the focus) -- the
  mirror draws no cross-fade there.
- **A delta carries a drift forever** (2026-11-16) : with everything native the open `partywin` sat at y -1844 = its
  place (record 298 + H-448 = 1156) minus HIDE_DY, so every Party toggle after that was INVERTED. The HUD-like
  windows (party, alliance, chat, help, cost, net) are placed straight from their record, so `heal_hud()` puts any
  open one that is off its absolute place (x + W-512 anchored right, y + H-448 anchored bottom) by half a park or
  more back there, at every walk (once a second) -- logged "was at ... its record says ... put back".
- **The expanded log `fulllog`** (live probe 2026-11-16 ; opened in game by X then Enter) : one open object, focused,
  rect 16,48-745,1210 at a 2327x1226 UI ; handler vtable FFXiMain+0x334C68. Handler fields seen changing :
  +0x1C counts DOWN as it scrolls up (0x47 at open : the top line's index), +0x20 counts UP from 0 (lines scrolled
  back from the newest), +0x54 = the TAB (X cycles them ; seen in order 8 3 4 1 2 7 10 9 6 5 0 -> 11 tabs, the value
  is a category id, not the screen order), each tab switch pulses +0x60 bit 0 and +0x64 = 2 ; +0x24 byte flags
  (bit 0 / bit 8 around the switches and the close). Tab ids (named by the user, in X order) : 0 Windows 1,
  8 Windows 2, 3 Say, 4 Tell, 1 Party, 2 Linkshell, 7 Linkshell 2, 10 Assist E, 9 Assist J, 6 Unity, 5 Shout.
  Parked with NU_CHAT ; AioHUD's Discussion box unfolds in its place (native_fulllog(), hud_chat.cpp FULL_TABS).
- **The lists `magic` and `ability`** share one list class (draw FUN_05563560 ; handler h : +0x40 count, +0x38 top,
  +0x48 rows shown, +0x30 cursor, +0x20 8-byte items whose int0 = a slot). Row id : `magic` = u16 at
  (spellSlots + 0x80 + slot*2) (FUN_0556DD40) ; `ability` = u16 at (abilSlots + 0x160 + slot*2) (FUN_05565730 ; the
  same 46 bytes but the displacement), an index into the 0x30-byte ability records (abilRecs global, FUN_05565770) --
  weapon skills 0..255, job abilities 512 + id. Both globals are read from the functions' operand (+33), found by
  signature. `ability` serves Job abilities, Weapon skills and Pet commands (h+0x64 = the category, the table at
  DAT_056C7221 stride 3) ; FUN_055641A0 opens it by its name "menu    ability" (Ghidra, 2026-11-16).

## 10. The NPC windows -- event dialogue (`rem4line`) and answers (`query`) -- static, 2026-11-17 (base 0x05340000)

Static reading (Ghidra + raw disassembly on `re/ffximain_dump_20261115.bin`), **then CONFIRMED live 2026-11-17**
(dev probe `//aio npclog`, Curio Vendor Moogle + Greeter Moogle, EN client, FFXiMain at 0x05AB0000) : both vtables
match ; the feeder carries the message (mode 150) ; `query` gives the question, the choices with their values, the
selected row, the scroll and the answer.

**The rendered text is GLYPHS, not bytes** : one u16 per character, code = ASCII - 0x20 (space = 0x0000, `C` =
0x23, `?` = 0x1F). Codes >= 0x5F are controls : a speaker line starts `FEFF FEFF` ; a continuation line starts
`0060 FEFF`. Every glyph string carries its LENGTH separately (the buffer after it is stale memory, never 0-ended) :
the dialogue line node +0x208 (u16, the controls included), the question +0x36 (u16), a choice +0x106 (u8).
The feeder message +0x42 is the raw SOURCE text instead (bytes, `07` = a line break).

**The menu REGISTRY** (0x056B3320 .. 0x056B6BD8, 330 entries x 0x2C) : `"menu    <name>"` (0x20 bytes), then the
address of the GLOBAL that holds the window's handler, then two words of parameters. It names a handler for 330 of
the 409 definitions (the rest -- `eventlog` among them -- are opened by nobody in the exe). A handler class keeps its
own "open" / "close" statics at vtable[-2] / vtable[-1], and again at slots 18 / 19 ; slot 4 is the draw, slot 6
the events, slot 10 the per-frame update. Generic list (`FUN_05467990` head, `FUN_054679b0` next, `FUN_05467910`
append) : list +0 head, +4 tail, +0x10 count ; node +0 next, +4 prev, +0x10 value, byte +0x14 dead.

**The event dialogue is `rem4line` (log window 1) / `rem4li2` (log window 2)** -- 16,298 366x70, anchor 0, the log's
own place. Globals 0x058B9384 / 0x058B9388 ; handler vtables 0x05674A50 / 0x05674AA0, both derived from the log
window base class 0x05674A08 (ctor FUN_05477780 ; +0x64 = the PARENT log handler, `logwindo` 0x05962AF4 / `logwin2`
0x05962AF8, whose +0x20 points back). Built at startup by FUN_054A6E20-ish code (0x054A6E47).
- **Routing** : the function that writes every log line (FUN_05469096, the log view's writer) sends a line of chat
  MODE 0x96 / 0x97 (150 / 151) to the two dialogue feeders INSTEAD of the log (two FUN_05467D80 calls, return).
  150 / 151 are the NPC event dialogue (151 = 0x97 = "type -0x69", no indent on the next page).
- **Feeder** (0x38 bytes ; globals 0x05823308 / 0x0582330C, = rem4line+0x68) : +0x1C the CURRENT message, +0x20 the
  page's wait flags, +0x28 int64 countdown (frames), +0x30 the window handler, +0x34 first-page flag, then a queue
  (+0x00 list) of messages. Enqueue FUN_05467DA0 ; the first queued message opens the window (handler slot 18).
- **Message** (0x848, FUN_05467FF0) : +0x04 15 dwords of style (byte +0x04 = the mode 0x96 / 0x97), +0x42 the raw
  text (0x7E6), +0x828 the read cursor, +0x82C the 2 prompt chars, +0x83C the feeder.
- **Paging** (FUN_054680F0) : the text is cut at `7F xx` codes -- `7F 31` wait for Enter (flags 0x8001 ; the same
  `7F 31` ends every record of the log store files, section 4 of chat-log.md), `7F 32` / `7F 33` 0x4002 / 0x4001,
  `7F 34` / `35` / `36` + 1 byte = auto-advance after n seconds (x60 frames), `7F 37` 1, `7F 38` + u16 = a timer.
  More than 4 display lines in one page are split with the prompt re-inserted.
- **Lines in the window** : FUN_05477A70 appends one node per display line (0x248 bytes : +0x04 the rendered text,
  0x200, as glyphs, controls kept ; +0x208 its LENGTH in glyphs (live) ; +0x20C 15 dwords of style) to handler+0x14 (count +0x24) ;
  handler+0x58 = the first line shown, +0x4E = the lines of the current page, at most 4 drawn (FUN_05477F40, 16 px
  apart, from rect +0x30). +0x56 = the "more" prompt is shown. +0x60 (short) = auto-advance : the event script's
  UI op 5 sets / clears it (FUN_053F0A00 case 5) ; slot 10 (FUN_054780D0) then validates by itself.
- **Events** (slot 6, FUN_05477FF0 -> FUN_05478030) : any event drops the 4 lines shown ; 5 on a manual page = next
  page (FUN_05467D10 -> FUN_05467F20) ; 6 while an event runs (byte 0x057C6C20 == 1) ENDS the dialogue (clears the
  feeder, 0x057C93B8 = 2, closes). Message done -> next queued, or close (slot 19) and 0x057C93B8 = 0.
- **`spoolmsg` / `splmsg2`** (globals 0x058B938C / 0x058B9390, vtables 0x05674B90 / 0x05674BE0, 16,298 366x70) : a
  separate stack of up to 4 TIMED lines (node 0x150 : +0x04 text, +0x104 style, +0x140 timer), placed on the parent
  log window, chosen by the writer when the line's style byte +2 == 2. Likely the lines shown while the log is
  hidden ; not proven.

**The answers are `query`** -- 16,208 366x88, anchor 2 (above the log). Global 0x058B9618 (also 0x057BE598 while an
event asks), handler 0x54C bytes, vtable 0x056763C8 (base 0x05675F08, shared with `gmtell` 0x05676410) ; built and
opened by FUN_054A0070 from the event script (FUN_053F25F0).
- **Text** : the prompt buffer **0x057CA2B8** (= FFXiMain+0x48A2B8, the June note above) : text before 0x0B = the
  QUESTION, then 0x07-separated CHOICES, `7F 34..36 / 80 84 86 8C 92` skip 3 bytes, `7F 38` 4.
- **Handler** : +0x14 the choice list (count +0x24), node 0x108 : +0x04 the rendered text (0x80 glyphs), +0x104 u16 the
  choice VALUE (1..n), +0x106 u8 its LENGTH in glyphs, +0x107 1 (live : "Nothing for now." = 16 -> 0x0110) ; +0x30
  the SELECTED choice (0-based), +0x32 the first row shown (3 rows), +0x34 widest, +0x36 / +0x38 the question's
  length (u16) / glyphs (live : 20 "What would you like?"), +0x540 / +0x542 its origin, +0x548 the answer (0xFF =
  cancelled), +0x54A a style (event UI op 6).
- **Events** (slot 6, FUN_0548F9E0) : 1 / 0x17 down, 2 / 0x16 up, 3 / 4 page, **5 validate** (selected = +0x32 + row
  - 1, then FUN_0548F7A0 answers), **6 cancel** (in an event : answer 0xFF), **9 hover** (row = obj+0x4C, as
  playermo), 0x1B scrollbar. Same driver as the action menu : FUN_054596F0(obj, event).
- Draw (slot 4, FUN_0548F7F0) : the question at (+0x540 + 4, +0x542 + 7), then 3 choices from +0x32.

**Live 2026-11-17, the mirror (Game UI -> "NPC dialogue + answers", NU_NPC ; hud_actmenu.cpp npc_draw) :**
- An answer's CONTROL glyphs carry its colour : every ITEM of a shop list starts `FEFE FEFF` and the client draws it
  green ; plain answers ("Back.", "Nothing.") carry none and are white (Greeter Moogle, 15 / 15 items).
- The client shows 3 answers ; AioHUD shows the same page (+0x32 top) and drives it with events 9 / 1 / 2 / 5.
- **`iteminfo`** (global 0x058B93BC, 117 refs : the one item-description window of the whole client) opens WITH a
  shop list's answers, as their child : parked with the query, so brought back by the rescue pass -- or left parked
  while AioHUD draws its own card. **Its handler +0x24 (u16) = the item shown** (11 / 11, then 15 / 15 items).
- The card is drawn from the item's DAT record (model/item_text.h ; the same DATs and proofs as the gear icons) :
  +0x04 u16 flags (= Windower res flags, 3000 / 3000 : 0x8000 Rare, 0x4000 Ex) ; a string table at +0x1C (general),
  +0x30 (armour), +0x3C (weapon) -- u32 count, then (u32 offset, u32 kind) pairs, a kind-0 text at table + offset +
  0x1C : name, log name, plural, description ; armour / weapon : +0x10 level, +0x12 slots, +0x14 races, +0x18 jobs ;
  weapon +0x20 DMG, +0x22 Delay, +0x26 skill, enchantment +0x2C charges / +0x2E equip delay / +0x30 reuse ; armour
  enchantment +0x20 / +0x22 / +0x24. The client's title is the LOG name, first letter raised ("Ark tachi").

Other event windows named by the registry : `eventtim` (0x058B9338, an event timer), `msgline` / `msglist`
(0x059A4C70 / 74), `evitem` / `evitem01` (0x059A838C / 90, item pickers), `sortyn` (0x059A805C), `hnyesno`.

## 11. The auto-translate picker (`fep`) -- static, NOT YET CONFIRMED LIVE, 2026-11-17 (base 0x05340000)

What Tab in the input line opens (live : the focus goes `inline` -> `fep`, and the rescue pass brings it back from
under the parked chat, so it is placed on the input line, section 6). "FEP" = the Japanese IME front-end : the same
window lists the Windows IME's candidates when an IME is open, and the client's own AUTO-TRANSLATE candidates
otherwise. Probe : `//aio feplog` (dev, `aiohud_probes.cpp`), 10 min, logs FEPLOG lines on change.

- **Registry** : `fep` global 0x059A3F54, params a=0x0081000A b=0x735, record 0,0 96x134 anchor 0 (the record is not
  used : it is placed by code). Built at load by FUN_054FA440 (with `inline` 0x059A3F50) : handler 0x2224 bytes, ctor
  FUN_054D0410(IN, inline+0x14), **vtable 0x05677A38** (slot 1 open FUN_054D0500, 4 draw FUN_054D0B20, 5 clamp-to-
  screen FUN_054D05E0, 6 events FUN_054D0650, 10 update FUN_054D0970, 16 notifications FUN_054D0D90).
- **IN = handler+0x14 = the input system** (global 0x059A3EF8, 0xF2F8 bytes, vtable 0x05678BE0 ; the `FsTextInput`
  of section 6 is the same family). Its candidate state : **byte +0x8C81 = FEP mode**, **+0x8C84 = SELECTED (0-based)**,
  **+0xEC28 = count** (max 0x3FC), candidates 0x18 bytes at **+0x8C88** (an object : vtbl+0xC / +0x10 give its text ;
  +0x14 an id), **+0xEC2C..+0xEC2F = the category PATH** (4 bytes, 0 0 0 0 = top level), +0xEC30 a move counter.
- **Filled** (Tab, FUN_054F72E0 key 9 -> FUN_054F84B0(prefix, len)) : IN+0x8C81 = 1 ; the PREFIX = the word typed
  before the caret (IN vtbl[25]) is saved in **0x059A3F10** (0x40 chars ; the dump holds "east") ; the dictionary
  object searches it inside the current path (FUN_054FD060 / FUN_054FD430 ; empty prefix at top level = the
  CATEGORY list ; no match -> one row "エラー" / "メモリーエラー") ; then FUN_054CEDC0 appends PLAYER NAMES that
  start with it (a name list, yourself, the 18 party / alliance members). Then opens `menu    fep`.
- **The rows the window draws** are a COPY rebuilt every frame by slot 10 (`IN vtbl[6](handler+0x18)`) : list
  handler+0x18 : **count handler+0x1C** (max 60), items 0x10 at **handler+0x20 + i*0x10** (or *(handler+0x21E8) when
  byte handler+0x21F8 is set, a long list) : item **+0x04 char* text (BYTES, not glyphs ; `7F xx` / SJIS pairs kept),
  +0x08 its length, +0x0C the candidate's id** ; the text buffer is handler+0x3E0 (0x1E00). A text whose length
  passes its first NUL has a SECOND part : drawn `first 06 second`.
- **Draw** (slot 4) : header "sel+1/count" ("%d/%d", "0/0" when empty) at (x+4+0x23, y+2) and an SJIS "▲" (81 A3)
  at (x+9, y+2) ; then the PAGE : **7 rows, from (sel / 7) * 7**, 16 px apart from y+2+16 ; a row wider than
  width-8 is cut with "..." ; colour 0x80808080 for the text, **0x80404040 (grey) after the 06** ; the cursor is put
  on menu entry **sel % 7 + 2** (FUN_0545A240). Placed at the input caret : x = anchor vtbl[0]() + caret offset,
  y = anchor vtbl[1]() (handler+0x2208 / +0x220C) ; width handler+0x2220 = widest row + 12, >= 102, <= 140
  or 200 (by the client mode at *(0x05797BD8)+8, probably the language) ; height +0x2222.
- **Events** (slot 6 ; ignored while obj+0x56 > 0) -> IN slot 3 (op) -> in FEP mode a KEY : **1 -> Tab (next,
  wraps)**, **2 -> Shift+Tab (previous, wraps)**, **5 -> Enter (validate)**, **0x1E -> Escape**, **9 = hover** : the
  row is the cursor (obj+0x4C, rows 2..8) ; the handler steps next / previous until sel % 7 + 2 == that row (once per
  frame, flag handler+0x2201) ; 0xC / 0x11 = validate. 3 / 4 (left / right) and 0x12 go to DIK codes the FEP mode does
  not seem to use. Keyboard in the line itself : Tab / Down next, Shift+Tab / Up previous, PageUp / PageDown -7 / +7.
- **Validate** (FUN_054F81D0) : on a CATEGORY (its record's first byte set) -> the path IN+0xEC2C = the category's
  4 bytes, the list is rebuilt with an EMPTY prefix and `fep` re-opens (a category level, then its phrases) ; on a
  phrase -> inserted into the line (IN vtbl[24]) and FEP mode ends. **Escape** : at a sub-level -> back to the TOP
  with the saved prefix (not one level up) ; at the top -> FEP closed, IN+0x8C84 = 0. The inserted form is section 6's
  `7F 69 .. 7F 64` (not traced statically here).

Live capture must settle : the row text format (plain ASCII ? `7F` codes ? what the second part after NUL is --
the category name ?), whether IN+0xEC2C changes on entering a category, whether the hover row really is obj+0x4C =
sel % 7 + 2 (entry 1 = the header ?), and the item +0x0C id.

- **The mount list `mount`** (Abilities -> Mount) -- LIVE capture 2026-11-17 (`MOUNTPROBE`, 16 mounts) : the same list
  class (h +0x40 count 0x10, +0x44 capacity 0x40, +0x48 rows shown 0x0C, +0x30 cursor, +0x34 the previous cursor, +0x38
  top 0 -> 4 as it scrolled, +0x20 the 8-byte items), but an item's int0 is the MOUNT ID itself (res/mounts.lua :
  1 Raptor .. 15 Xzomit ; the capture's ids 1 2 3 4 5 6 7 8 A D E F), no slot table between. `mount_read`,
  `native_ui.cpp` ; names from `mounts_gen.h` (`scripts/gen_mounts.py`).

- **The Items list `inventor`** (the action menu's Items : the usable ones, the client's filter) -- LIVE captures
  2026-11-17 (WINPROBE, INVPROBE 1-3), corrected 2026-11-18 (INVLOG : the whole list by name, both boxes side by side,
  after "j'ai voulu manger miso ramen +1 ca utilise abdhals seal" -- the top was read 2 rows high and the list 4 short).
  NOT the list class above. h+0x38 -> the rows, 0x54 bytes each : +0x2C the item id (u16), +0x30 the count (0 on a
  non-stack, Maze Tabula), +0x28 its item-container entry ; **h+0x20 = (the page top's max << 16) | the count of rows**
  (0x000C0016 : 22 rows, top 0..12, 10 shown) ; h+0x24 = (?? << 16) | the cursor's row IN THE PAGE (1-based) -- its
  high word (18 of those 22) is NOT the count ; h+0x18 = (page top << 16) | rows shown (0 at the list's top) ;
  h+0x1C = (the same << 16) | its end. No
  submenu : picking an item opens the target cursor. The same window serves the main menu's inventory, the equipment
  screen and trades : `inv_read` reads it only while `playermo` is open, and it is never parked by name -- it is born
  under the parked action menu and `rescue_children` leaves it (and its `iteminfo`) there while AioHUD draws them.

## 12. The action menu's spell / ability ICONS -- where they come from (2026-11-17)

The client draws NO per-spell picture in its lists, and its DATs hold none : a scan of every DAT (ROM..ROM9, 0.2-40 MB)
for icon-named image records finds only the 640-record status sheet (`sts_icon`, ROM/119/57 and its copies). The res
`icon_id` of a spell / ability is NOT an index into that sheet (see buffs-and-timers/timers.md).

AioHUD's redrawn lists therefore take, in order (`hud_actmenu.cpp`, `list_icon` / `list_fallback`) :
1. **Windower's Timers pack**, read from the user's install : `<windower>\plugins\icons\spells\NNNNN.png` (spell id) and
   `...\abilities\NNNNN.png` (RECAST id ; the shared recasts 0 and 254 have one per job : `NNNNN.JJ.png`, JJ = main job).
   Dated 2015-08-07 : 87 of 957 spells and 46 of 626 abilities have no file (GEO Indi-/Geo-, most RUN, recent Trusts).
2. **A Trust without a file** : Windower's Trust pictures follow the res `icon_id`, `1000 + 9 * weapon + colour`
   (weapons shield / sword / bow / staff / club / harp / fist ; colours red / blue / green / white / orange / purple /
   pink / cyan / grey -- 37 of 38 icon_ids consistent over the 105 files). A sibling Trust with the same icon_id lends
   its file ; else the picture is REBUILT : the same weapon in another colour, recoloured with the luminance ramp of
   the wanted colour read on another weapon (Ygnas 1043, Monberaux 1039, AAHM / AAEV 1008).
3. **The status the action gives** (`SpellRow::status` / `AbilRow::status`, from res ; a Geo- spell takes its Indi-
   twin's), drawn from the status atlas.
4. **A weapon skill** (Windower has none) : an ELEMENTAL one (magical / hybrid) its DAMAGE element, from a table taken
   from BG Wiki (`scripts/gen_ws.py`, `WSRow::elem`) -- res' own `element` field is not it (Viper Bite, Rudra's Storm
   carry one and are physical ; Savage Blade and Shining Blade share 6) and neither is the skillchain property
   (Shining Blade : Light damage, Scission property) ; a physical one its weapon's damage type (`WSRow::skill` ->
   Slashing / Piercing / Blunt, `assets/weapon_icons.raw`). Elements from `assets/element_icons.raw`.
5. **A job ability** with no file and no status : its JOB's emblem (`assets/job_icons.raw`) in the role colour -- the
   job found by name in `job_track` (main job, then support) ; the submenu CATEGORY rows (Waltzes, Rune Enchantment,
   Ward, Effusion) by a small table.
6. AioHUD's drawn glyph (a Trust with no picture : the party glyph).
