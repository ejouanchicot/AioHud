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
  | 18 | Switch Target | engaged |
  | 19 | Trade | player |

  Unknown yet : 2 4 6 12-17 20-24 26 27 (Invite, Treasure, Request, Fish... : capture + screenshot when met ;
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
