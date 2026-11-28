# scripts/

Offline tooling. **Nothing here runs inside the plugin** — these produce the generated tables and baked
assets that the build then compiles in, so they are run by hand when the game data or an asset changes.

Two very different toolchains used to sit side by side; the Ghidra ones moved down a level:

| | What | How it runs |
|---|---|---|
| `*.py` (23) | **Generators** — turn Windower resources or a reference sheet into a `*_gen.h` table or a `.raw` atlas | `python scripts/<name>.py` |
| `*.ps1` / `*.sh` (7) | Asset baking (window skin, icons, capitalisation pass) | run directly |
| `tidy.ps1` | **clang-tidy**, avec les drapeaux qui le font marcher sur ce projet — outil LOCAL, pas une etape de CI | `.\scripts	idy.ps1 [fichier]` |
| `check_gen_drift.py` | Runs every `*_gen.h` generator into a temp folder and diffs it with the tree : SAME / DRIFT / CANNOT RUN / CRASH, exit 1 on DRIFT or CRASH. Paths come from `genpaths.py` (`WINDOWER_RES`, `WINDOWER_ADDONS`, `AIOHUD_GEN_OUT`) | `python scripts/check_gen_drift.py [--keep] [name...]` |
| `verify_release.ps1` | Verifie une release **publiee** comme la voit l'updater d'un joueur. Branche en CI apres la publication | `.\scripts\verify_release.ps1 [-Tag v1.0.71]` |
| [`ghidra/`](ghidra/README.md) (32 `.java`) | **Ghidra headless scripts** — static analysis of `FFXiMain`. Different tool, different workflow | `analyzeHeadless ... -scriptPath scripts/ghidra -postScript <Script>.java` |

## clang-tidy : outil local, delibrement PAS en CI

`clang-tidy` est deja livre avec VS BuildTools (rien a installer). `.\scripts	idy.ps1` l'appelle avec les
trois drapeaux non evidents : `--header-filter` (sans lui, 7994 des 8018 avertissements viennent des en-tetes
systeme), `-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH` (clang 12 refuse la STL de VS 2022) et `-m32`.

**Evalue le 2026-07-25 sur les 5 fichiers les plus charges (~5000 lignes) : aucun bug reel trouve.** Les
constats `bugprone-*` etaient corrects mais sans consequence (indexation d'atlas en division entiere = voulue ;
arrondi dans un log de debug ; un depassement qui demanderait une texture de 536 millions de pixels).

C'est pourquoi il n'est PAS branche en CI : un garde-fou qui ne rapporte rien d'actionnable est ignore en deux
semaines, et un garde-fou ignore est pire qu'absent. Trois regles ont ete desactivees pour la meme raison
qu'elles produisaient l'essentiel du bruit sans rien apprendre (`-performance-no-int-to-ptr` : lire la memoire
du jeu depuis une adresse entiere EST le principe du projet ; `-modernize-use-nullptr` et
`-readability-make-member-function-const` : 182 editions de masse a comportement inchange). Le rapport est
passe de 158 a 68 constats sur ces memes fichiers.

**Ou il sert vraiment : le code NEUF.** Sur un fichier recent il rend 0 constat, donc tout ce qu'il signale sur
un fichier qu'on vient d'ecrire merite un regard. Le lancer apres avoir ajoute un module, pas sur l'existant.

## Rules that bite

- **`src/model/*_gen.h` are GENERATED — never hand-edit them.** Regenerate instead. A hand edit survives
  until the next run of the generator, then vanishes silently.
- **Windower's `res/` is an input, and it lives outside the repo.** Generators that read it take the path from
  the `WINDOWER_RES` environment variable (addons: `WINDOWER_ADDONS`), defaulting to this machine's install —
  one order for all of them, in `genpaths.py`. Set it on another machine rather than editing the script.
- **Output paths are resolved from the script's own location.** Three generators used to write to an absolute
  `…\plugins\_aiohud_re\src\model\` — a runtime folder renamed long ago that never held `src/` — so their
  tables were silently un-regenerable. Fixed 2026-07-25; keep the `ROOT = Path(__file__)…` pattern.
  The three **asset** tools below had the same dead paths, still live rather than in a comment, and were
  fixed the same way on 2026-07-26 (`$PSScriptRoot` / `BASH_SOURCE`). Both runnable ones were then verified
  the only way that proves a path fix: **re-run, output byte-identical to the shipped asset** — 48/48 window
  `.raw` unchanged in `git status`, and both `cap_*.bin` matching by SHA-256.
- **Regenerating can change game data, not just formatting.** After a `res/` update, `gen_mobskills.py` renames
  31 mob skills. Check `git diff` before committing a regeneration: a path fix should come back byte-identical.

## Generators

| Script | Produces | Reads Windower `res/` |
|---|---|---|
| `gen_action_status.py` | `src/model/action_status_gen.h` | yes |
| `gen_actions.py` | `src/model/spells_gen.h` | yes |
| `gen_buff_names.py` | `src/model/buffs_gen.h` |  |
| `gen_element_icons.py` | `assets/element_icons.raw` |  |
| `gen_chat_icons.py` | `assets/chat_icons.raw` | the Chat tabs' mother-of-pearl icons (sources : `assets/chat_icons_src/`) |
| `gen_enh_dur.py` | `src/model/enh_dur_listed_gen.h` | yes |
| `gen_geo_dur.py` | `src/model/geo_dur_gen.h` | yes |
| `gen_grimoire.py` | `—` |  |
| `gen_itemnames.py` | `src/model/itemnames_gen.h` | yes |
| `gen_job_track.py` | `src/model/job_track_gen.h` | yes |
| `gen_keyitems.py` | `src/model/keyitems_gen.h` | yes |
| `gen_mobskills.py` | `src/model/mobskills_gen.h` | yes |
| `gen_nms.py` | `src/model/nms_gen.h` | yes |
| `gen_overwrites.py` | `src/model/overwrites_gen.h` | yes |
| `gen_regen_dur.py` | `src/model/regen_dur_gen.h` | yes |
| `gen_resistances.py` | `src/model/resistances_gen.h` | yes |
| `gen_skillchain.py` | `src/model/skillchain_gen.h` | yes |
| `gen_song_dur.py` | `src/model/song_dur_gen.h` | yes |
| `gen_song_family.py` | `src/model/song_family_gen.h` | yes |
| `gen_tb_buffs.py` | `src/model/tb_buff_gen.h` | yes |
| `gen_tb_debuffs.py` | `src/model/tb_debuff_gen.h` |  |
| `gen_trusts.py` | `src/model/trusts_gen.h` | yes |
| `gen_weapon_icons.py` | `assets/weapon_icons.raw` |  |
| `gen_ws.py` | `src/model/weapon_skills_gen.h` | yes |
| `gen_astral.py` | the ASTRAL COMPASS kit (assets/astral_src/<palette>/*.png, manifest.json) -> the runtime textures assets/astral/<palette>_<piece>.raw (straight-alpha … | see its header |
| `gen_bst_icons.py` | Convert the BST box's icons into the RUNTIME folder, never into the repo (as the pet art: scripts/gen_bst_pet_art.py) | see its header |
| `gen_bst_pet_art.py` | Convert pet art for the BST content of the Pet box (src/ui/hud_bst.cpp) | see its header |
| `gen_bst_pets.py` | Generate src/model/bst_pets_gen.h from addons/BST_JUG_PETS_DATABASE.md : every jug pet (NQ and HQ names) with its Ready moves, for the BST box's PREVI… | see its header |
| `gen_currencies.py` | src/model/currencies_gen.h : the client's own row order and labels of Status -> Currencies (mode 1, packet 0x113, 99 rows) and Currencies 2 (mode 2, p… | see its header |
| `gen_edit_icons.py` | the Edit Layout drawer's icons (eye shown / hidden, padlock locked / open) | see its header |
| `gen_geo_icons.py` | Convert the GEO content's images (the Pet box, src/ui/hud_geo.cpp) into the repo's assets\geo\, shipped by deploy.bat / package.bat | see its header |
| `gen_geo_spells.py` | generate src/model/geo_spells_gen.h from res/spells.lua : every Geomancy spell named "Geo-X" or "Indi-X", with its id, its name, its element and the S… | see its header |
| `gen_job_icons.py` | Build assets/job_icons.raw -- the job-emblem atlas the party and player badges sample | see its header |
| `gen_logo.py` | bake the AioHUD lockup into the raw BGRA the plugin loads, plus the metrics the header needs | see its header |
| `gen_map_markers.py` | the minimap MARKER atlas from the v3 sheets (assets/markers_src/v3_sheet1..3.png, transparent background) -> assets/map_markers.raw : a 512x256 straig… | see its header |
| `gen_metal_grain.py` | the Dial's METAL MATERIAL (the user 2026-11-25 : "now the quality of the metal's texture") : a tileable 256 x 256 detail layer laid over the flat meta… | see its header |
| `gen_moon_dial.py` | the moon-phase dial's face : assets/moon_dial_src/moon_dial.png (a parchment disc, a continuous midnight-blue band with the eight golden phases on it … | see its header |
| `gen_moon_phases.py` | the moon pod's eight phase icons, cut UPRIGHT out of the starry dial plate assets/moon_dial_src/moon_dial.png (its moons on one circle, new at the bot… | see its header |
| `gen_mounts.py` | src/model/mounts_gen.h from Windower res/mounts.lua : mount id -> English name | see its header |
| `gen_net_icons.py` | the CONNECTION BOX icons : assets/net_icons_src/net_icons.png (row 1 : the FILLS -- envelope, speech bubble, bag, sword, the 8-dot spinner ; row 2 : t… | see its header |
| `gen_plate_icons.py` | the NAME PLATE icons (the glyphs FFXI draws before a name over a character : status, linkshell, bazaar, Mentor, nations | see its header |
| `gen_pointers.py` | builds assets/pointers.raw : the selectable cursor styles (Party > Cursor style) | see its header |
| `gen_ready_moves.py` | Generate src/model/ready_moves_gen.h from the BST-HUD addon's addons/BST-HUD/ready_move_mappings.lua and scripts/ready_move_icons.txt | see its header |
| `gen_sky_frames.py` | the SKY MODULE's twelve scenes (the minimap's sky wheel, src/ui/minimap.cpp) : assets/sky_frames_src/sky_frames.png (one hand-inked sheet on papyrus, … | see its header |
| `gen_smn_art.py` | Convert the player's avatar and spirit pictures (the SMN content of the Pet box, src/ui/hud_smn.cpp) into the repo's assets\smn\, shipped by deploy.ba… | see its header |
| `gen_smn_avatars.py` | generate src/model/smn_avatars_gen.h : every avatar and elemental spirit a Summoner calls, with its element, its picture (assets\smn\<img>.raw, script… | see its header |
| `gen_spell_elem.py` | generate src/model/spell_elem_gen.h from Windower res/spells.lua : the ELEMENT of every spell that has one of the eight (0 Fire 1 Ice 2 Wind 3 Earth 4… | see its header |
| `gen_status_icons.py` | the PlayOnline status icons, redrawn in HD (assets/status_icons_src/status_icons_grid.png, a 3 x 3 grid on a transparent background, made from the cli… | see its header |
| `gen_weather_icons.py` | the WEATHER's 8 element icons, in HD, for the minimap's weather orbs ONLY (the user 2026-11-25 : "specific to the weather" ; every other element icon … | see its header |

`gen_grimoire.py` emits no table of its own — it prints the SCH grimoire interval/charge figures used to fill
`docs/game-data/buffs-and-timers/grimoire-sch.md` by hand.

## Build and repo tools

| Script | What it does |
|---|---|
| `build_inc.py` | INCREMENTAL build of build\AioHud.dll : one .obj per source, recompiled only when the source, a project header it includes (/showIncludes), the flags … |
| `check_repo.py` | the static checks a commit should not be able to lose |
| `shipped_commands.py` | The //aio commands that SHIP, read from the one table that dispatches them (src/plugin/aio_verbs.h) |

## Asset tools (`.ps1` / `.sh`)

These bake binaries the plugin loads at runtime, from sources kept in `assets/*_src/` (`research/art_src/` only for the retired `gen_flow.ps1`).

| Script | Reads | Produces | Needs |
|---|---|---|---|
| `gen_window_skin.sh` | `assets/window_src/0/<theme>/*.dds` | `assets/window/<theme>/{corner,hframe,vframe,bg}.raw` (9 themes × 4 ; retired skins live in `assets/window_src/_retired/`, outside the loop) | python + ImageMagick |
| `conv_caps.ps1` | `assets/caps_src/*.png` | `assets/cap_{front,back}.bin` (the fiole caps, `ui/liquid_bars.cpp`) | — |
| `gen_buff_atlas.ps1` · `gen_gil_icon.ps1` · `gen_th_icon.ps1` · `patch_buff_icon.ps1` | icon sources | `assets/*.raw` | — |
| `gen_flow.ps1` *(script retire du depot)* | *(32 `Flow0_*.png`, **not in this repo**)* | `research/art_src/FlowX_00..95.png` | — |

`gen_flow.ps1` is a **historical** art tool: nothing loads its frames today (the fiole liquid is drawn
procedurally), and its 32 input frames lived in an old Windower addon folder that no longer exists. It is kept
because it documents how the 96 surviving frames were made, and it now **fails with that explanation** instead
of silently pointing at a dead disk. Pass `-SrcDir` if you ever recover the originals.

## verify_release.ps1 : le seul controle qui regarde l'artefact, pas les entrees

Tout le reste de la CI verifie ce qui ENTRE dans une release — l'arbre compile, la suite passe, le changelog
porte la bonne version. Ce script est le seul a verifier ce qui en SORT, depuis l'exterieur, exactement comme
l'updater d'un joueur le voit : il interroge `/releases/latest` (pas le tag — GitHub y pointe la release
publiee en DERNIER, pas la plus recente en semver), telecharge les deux assets, recalcule l'empreinte, prouve
qu'une archive alteree est bien refusee, et ouvre le zip pour verifier qu'il contient les trois fichiers sans
lesquels une mise a jour ne peut pas aboutir.

**Pourquoi il existe.** La v1.0.71 a ete publiee avec un controle d'integrite incapable de reussir : la
comparaison lisait `(Invoke-WebRequest ...).Content`, qui vaut un TABLEAU D'OCTETS sur PowerShell 5.1 des que
la reponse n'est pas d'un type texte reconnu — et GitHub sert les assets en `application/octet-stream`. Le
`-replace` s'appliquait donc octet par octet et produisait « 49 56 99 52 ... », les codes ASCII des chiffres
hexadecimaux. La comparaison ne pouvait jamais correspondre : **toutes** les mises a jour suivantes auraient
ete refusees pour « checksum mismatch ».

Le code avait ete relu, sa syntaxe validee, et la logique de hachage exercee sur un fichier local. Rien de
tout cela ne pouvait voir le defaut, parce qu'il vivait dans ce que GitHub renvoie sur le reseau. Seul
l'artefact reel pouvait le reveler — et ce script l'a trouve a sa premiere entree veritable, avant qu'aucun
joueur n'ait telecharge la release.

Il verifie aussi une chose qu'on oublie facilement : **la compatibilite avec les clients d'AVANT**. Chaque
joueur execute le script de la version PRECEDENTE au moment ou il se met a jour, et celui-la filtre
`-like 'AioHud-*.zip'` sans exclusion, en prenant le premier resultat. Il tombe encore sur le zip et jamais
sur le sidecar uniquement parce que `-like` ancre la fin du motif — renommer un asset romprait ca en silence,
pour tous ceux qui ne sont pas encore a jour.

Sortie non nulle en cas d'echec, donc la CI l'execute apres `gh release create` : un job rouge signifie que la
release n'est pas installable. Il ne demande aucun jeton (l'API des releases est publique), donc il tourne a
l'identique sur une machine de dev et sur un runner.

## See also

- [`../docs/reverse-engineering/`](../docs/reverse-engineering/README.md) — how to find what these tables encode
- [`../docs/game-data/`](../docs/game-data/README.md) — what each reversed table means
