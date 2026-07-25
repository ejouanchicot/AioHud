# scripts/

Offline tooling. **Nothing here runs inside the plugin** — these produce the generated tables and baked
assets that the build then compiles in, so they are run by hand when the game data or an asset changes.

Two very different toolchains used to sit side by side; the Ghidra ones moved down a level:

| | What | How it runs |
|---|---|---|
| `*.py` (23) | **Generators** — turn Windower resources or a reference sheet into a `*_gen.h` table or a `.raw` atlas | `python scripts/<name>.py` |
| `*.ps1` / `*.sh` (7) | Asset baking (window skin, icons, capitalisation pass) | run directly |
| `tidy.ps1` | **clang-tidy**, avec les drapeaux qui le font marcher sur ce projet — outil LOCAL, pas une etape de CI | `.\scripts	idy.ps1 [fichier]` |
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
  the `WINDOWER_RES` environment variable, defaulting to this machine's install. Set it on another machine
  rather than editing the script.
- **Output paths are resolved from the script's own location.** Three generators used to write to an absolute
  `…\plugins\_aiohud_re\src\model\` — a runtime folder renamed long ago that never held `src/` — so their
  tables were silently un-regenerable. Fixed 2026-07-25; keep the `ROOT = Path(__file__)…` pattern.
- **Regenerating can change game data, not just formatting.** After a `res/` update, `gen_mobskills.py` renames
  31 mob skills. Check `git diff` before committing a regeneration: a path fix should come back byte-identical.

## Generators

| Script | Produces | Reads Windower `res/` |
|---|---|---|
| `gen_action_status.py` | `src/model/action_status_gen.h` | yes |
| `gen_actions.py` | `src/model/spells_gen.h` | yes |
| `gen_buff_names.py` | `src/model/buffs_gen.h` |  |
| `gen_element_icons.py` | `assets/element_icons.raw` |  |
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

`gen_grimoire.py` emits no table of its own — it prints the SCH grimoire interval/charge figures used to fill
`docs/game-data/buffs-and-timers/grimoire-sch.md` by hand.

## See also

- [`../docs/reverse-engineering/`](../docs/reverse-engineering/README.md) — how to find what these tables encode
- [`../docs/game-data/`](../docs/game-data/README.md) — what each reversed table means
