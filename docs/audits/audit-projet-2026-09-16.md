---
title: Audit projet — 2026-09-16 (v1.0.91)
summary: Inspection complete de l'arbre : code mort, fichiers obsoletes, docs qui mentent, conformite aux 10 regles, premier passage de clang-tidy. Ce qui a ete corrige, ce qui est sain, et la dette qui reste chiffree.
source: mesures reproductibles (scripts/check_repo.py, scripts/check_gen_drift.py, clang-tidy, tests.bat, igtest)
---
# Audit projet — 2026-09-16 (v1.0.91)

Inspection de tout le depot, **par la mesure** : chaque constat ci-dessous vient d'une commande
reproductible, pas d'une lecture d'impression. Les faux positifs sont dits comme tels — un audit
dont 80 % des points sont du bruit ne se relit jamais.

## Perimetre mesure

| | |
|---|---|
| Code C++ suivi | **90 124 lignes** (103 `.cpp`, 111 `.h`), dont ~35 000 de tables generees |
| Repartition | `model` 56 223 · `ui` 21 134 · `gfx` 3 590 · `plugin` 1 684 · `include` 487 |
| Docs suivies | 87 `.md` (`docs/game-data/`, `docs/audits/`, `docs/songs/`) |
| Assets suivis | 1 473 fichiers (1 323 icones de gear) |
| Tests | 29 fichiers, **247 verifications** + 2 rejeux de session |
| Depot `dev/` (prive) | 11 501 lignes |

## Ce qui a ete CORRIGE

### 1. Des docs qui envoient le lecteur sur des commandes inexistantes
**24 pages vivantes** (`docs/game-data/`, `docs/songs/`) nommaient **106 fois** une commande `//aio`
absente d'une release : sondes de developpement (`aiohud_probes.cpp`, non suivi), sondes elaguees le
2026-09-14, ou commandes dont il ne reste aucun code. `CLAUDE.md` posait deja la regle — *« un remede
qu'un message livre nomme doit etre une commande livree, et cela vaut aussi pour une DOC »* — et
quatre recettes avaient ete traitees a la main ; les vingt-quatre autres non.
Chaque page porte desormais **une note unique** disant, commande par commande, ce qui est dev-only,
archive, ou definitivement mort.

### 2. Liens morts et citations perimees
- `song-duration.md` : deux liens remontaient de deux niveaux au lieu de trois — les deux cibles
  existent, les deux liens etaient casses.
- `cast-bar.md` : citait `scratchpad/gen_spells.py` pour une table que `scripts/gen_actions.py` genere.
- `bard.md`, `enhancing-magic.md` : renvoyaient vers un `red-mage.md` qui n'a jamais existe.
- `scripts/README.md` : listait `gen_flow.ps1` comme s'il etait encore la.

### 3. Regle 9 violee neuf fois
Neuf `ease()` de `config_page.cpp` passaient encore un uid **ecrit a la main** (`1`, `10+i`, `200+i`,
`320+i`, `410`, `700`, `900`, `901`, `904`) — exactement ce que la regle interdit, et `10+i`/`200+i`/
`320+i` sont la forme *« +i tombe sur l'id du voisin »* contre laquelle l'en-tete du toolkit met en
garde. Remplaces par `CTRL_ID`, ou `ctrl_uid_i(CTRL_ID, i)` dans une boucle. Aucun changement de
comportement : un uid ne fait que nommer un ressort.

### 4. `snap()` arrondissait a l'envers les coordonnees negatives
`(float)(int)(v + 0.5f)` tronque **vers zero** : `snap(-3.4)` rendait `-2`, un pixel entier d'ecart,
alors que la regle 1 du projet dit qu'un bord mal snappe devient flou. Corrige en arrondi
*away-from-zero* (l'idiome deja utilise ailleurs pour les pourcentages signes) : **les valeurs
positives sont inchangees**, donc les 1 268 appels existants gardent leur resultat exact.
Trouve par clang-tidy, qui n'avait jamais tourne sur ce depot.

### 5. Hygiene
- **63 fichiers `.obj` (5,9 Mo)** d'un ancien build trainaient a la racine (ignores par git, mais bien
  presents). Supprimes ; le `build.bat` actuel ne les recree pas — verifie.
- La revue ChatGPT et ses deux reponses sont passees de la racine a `docs/audits/`.
- Deux ternaires morts : `nm ? "" : ""` (zone tracker) et `fr ? "Job" : "Job"` (doctor HTML).

### 6. Un garde pour que ca ne revienne pas
`scripts/check_repo.py` (~1 s, lance par le hook de pre-commit) verifie les trois classes ci-dessus :
liens markdown morts, commandes non livrees citees sans la note, `ease()` a uid en dur.
**Prouve mordant** : reintroduire chacun des trois defauts, un a la fois, le fait passer au rouge, et
l'arbre redevient vert ensuite.

## Ce qui est SAIN (verifie, pas suppose)

| Verification | Resultat |
|---|---|
| Headers jamais inclus (code mort) | **0** |
| `.cpp` suivis non compiles | **0** hors `tests/` (normal : runner separe) |
| Tables generees | **21/21 reproduites a l'identique** (`check_gen_drift.py`) |
| Compilation `/W4 /WX` | **0 avertissement** |
| Regle 5 (lectures gardees) | 194 `safe_read` + 19 `__try`, **aucune** lecture brute de memoire jeu |
| Regle 10 (transitoire -> permanent) | chaque latch porte sa justification `rule10-ok:` |
| Regle 4 (device lost) | `on_device_lost` oublie (met a 0), `dispose()` Release |
| Debordements de tampon | **0** : les 11 `sprintf` avec `%s` ecrivent au plus 22 octets dans des tampons de 24 a 48 |
| Fichiers suivis orphelins | **0** (`skills.lua`, `aioicons.ps1`, `design/icons/…` tous references) |
| Payload | aucun `.pdb` dans `dist/`, `.gitignore` coherent |

### clang-tidy, premier passage
Le `.clang-tidy` du projet etait curate mais n'avait **jamais ete execute** (clang-tidy 12 est pourtant
installe avec les BuildTools VS2019). Resultat sur les 100 `.cpp` : **396 avertissements, 1 seul vrai
defaut** (`snap()`, corrige ci-dessus). Le reste est du bruit dont il faut se souvenir pour ne pas le
re-instruire a chaque audit :
- **162 `bugprone-incorrect-roundings`** : `(int)(x + 0.5f)` sur des grandeurs positives (tailles, rayons) ;
- **27 `bugprone-integer-division`** : `cell / JI_COLS` calcule une **ligne d'atlas** — la troncature est le but ;
- **2 `bugprone-suspicious-missing-comma`** : `"\xC3\x89""diter"` est la coupure standard d'une sequence hexadecimale ;
- **46 `pro-type-member-init`** : structs dont tous les champs sont affectes juste apres.

## Dette qui RESTE (chiffree, non traitee)

### Fonctions surdimensionnees — 23 au-dessus de 120 lignes avant le decoupage, 8 au-dessus de 400
*(apres le decoupage d'`on_action` : 25 au-dessus de 120 — ses trois plus gros etages entrent dans le
compte — et 7 au-dessus de 400. Le nombre total de lignes ne bouge pas ; la plus grosse fonction, si.)*

| lignes | fonction |
|---|---|
| ~~950~~ **24** | `PartyState::on_action` — **decoupee** en 10 etages (voir plus bas) |
| 717 | `Party::draw` (`party.cpp:535`) |
| 695 | `ConfigPage::draw` (`config_page.cpp:402`) |
| 657 | `Player::draw` (`player.cpp:255`) |
| 584 | `Target::draw` (`target.cpp:408`) |
| 544 | `Hud::doctor` (`hud.cpp:475`) |
| 434 | `Minimap::draw` (`minimap.cpp:398`) |
| 423 | `ConfigPage::draw_edit_layout` (`config_page.cpp:1172`) |

**`on_action` : FAIT** (meme journee). Elle etait deja decoupee *visuellement* en sections
independantes (`---- TREASURE HUNTER ----`, `---- HATE LIST ----`, `---- SKILLCHAINS ----`…) et
couverte par la suite + les deux rejeux, donc le decoupage se **verifiait**. Dix etages, corps repris
ligne pour ligne, signature reduite a ce que chaque etage lit vraiment (`/W4 /WX` refuse un parametre
inutile — il a d'ailleurs attrape mes deux premieres signatures trop larges). Deux etages rendent un
`bool` : le popup de weaponskill et la resolution de debuff **consomment** le paquet, ce que leur
`return` faisait deja sans que ca se voie. Resultat : `on_action` = **24 lignes** qui se lisent comme
la liste des consommateurs du paquet, plus grosse fonction du projet ramenee de 950 a **353**
(`act_ally_buffs`). Verifie par 2308 controles / 0 echec et les **2 rejeux de session conformes a leur
golden** — ce sont de vrais paquets 0x028 rejoues a travers cette fonction.

**Les `draw()` restent a laisser** tant qu'aucun test de rendu n'existe : les couper a l'aveugle, c'est
echanger de la lisibilite contre un risque visuel que rien ne rattrape.

### Duplication — 13 groupes d'au moins 9 lignes identiques
Le gros est entre panneaux de config : `ep_config` / `grim_config` / `hl_config` / `minimap_config` /
`pw_config` / `player_config` partagent deux a trois blocs de 12 lignes (en-tete de panneau, section
Text). C'est la suite du chantier « palier 2 » deja engage. Les repetitions de `draw.cpp` sont des
variantes de primitives AA, pas des copies.

## Non-regression

Rien n'a ete commite sans que tout ceci passe : build `/W4 /WX` sans avertissement, suite offline
**247 verifications / 0 echec** + les 2 rejeux de session conformes a leur golden, cross-check in-game
`igtest` **247 / 0 echec**, et `check_gen_drift` **21 SAME / 0 DRIFT**. Le hook de pre-commit refuse
le commit si l'un d'eux tombe.
