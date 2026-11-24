# Audit complet d'AioHUD — 2026-11-23

> Analyse en lecture seule (le rapport a été la seule écriture de l'audit). Phase 1 appliquée le même jour :
> commits `f5dfd77` .. `3279a50` (H1, /MP, A1+H3, R2/R3/R6, R7/R9/R11, S1, P5, R10+S9) ; R1 et R4 en attente.
> Base : `main` @ `16c8897` (2026-11-22), plus l'arbre de travail tel quel (19 fichiers modifiés, 23 non suivis).
> Méthode : 5 analyses parallèles (architecture, performance, robustesse, outillage, assets), puis vérification
> par moi-même des constats les plus lourds (marqués **[vérifié]**).

**Légende des preuves**
- **[code]** : constaté en lisant le code (fichier:ligne).
- **[mesuré]** : obtenu en exécutant quelque chose ici (taille, compte, compilation, `luac -p`). Les commandes sont indiquées.
- **[à vérifier en jeu]** : supposition plausible tirée du code, non observée. À mesurer avant d'agir (règle maison « mesurer avant de corriger »).

---

## 0. Mise au point : ce n'est plus un addon Lua

La demande parle d'un « addon Lua (Lua 5.1, prerender, texts/images) ». **Le projet actuel n'en est pas un.**
AioHUD est un **plugin Windower natif en C++ 32 bits** (`AioHud.dll`), qui dessine lui-même en **Direct3D 8 à pipeline fixe**.
Il n'utilise ni l'événement `prerender` ni les objets `texts`/`images` de Windower, et ne passe pas par le ramasse-miettes Lua.

- **[mesuré]** `src/` compte 97 `.cpp` et 126 `.h`, soit 136 599 lignes (`wc -l`), dont environ 52 000 lignes de tables générées (`*_gen.h`).
- **[mesuré]** Il reste seulement 4 fichiers Lua (1 469 lignes) :
  - `updater/aioupdate/aioupdate.lua` (390 lignes) : l'addon de mise à jour, **livré** ;
  - `dev/aiotest/aiotest.lua` (947), `dev/scripts/aioupdate_sortie_harness.lua` (132), `scripts/skills.lua` : outils de dev.
- **[code]** L'ère Lua existe bien dans l'historique : `.claude/settings.local.json` autorise encore des lectures de
  `D:\Windower Tetsouo\addons\AioHUD\` (`core/frames.lua`, `boxes/textpanel.lua`, `assets/fiole`...).
  `aiohud-backup.git` (59 commits, jusqu'à v1.0.27) et `_aiohud_attic/` (`Claude outputs`, `rolls_src`) sont les vestiges de cette époque et de la réécriture.

**Conséquence pour ce rapport.** Chaque question posée en termes Lua est traduite dans son équivalent C++/D3D8 :

| Question posée (Lua) | Équivalent réel dans AioHUD |
|---|---|
| prerender | le rappel de rendu `aio_plugin_render6` |
| objets texts/images recréés | appels de dessin et changements d'état D3D |
| pression GC / tables dans les boucles | allocations par image |
| erreurs Lua / accès nil | lectures mémoire non gardées, crochets, échecs silencieux |
| luacheck | `/W4 /WX`, ASan, clang-tidy |

luacheck n'a de sens que pour les 4 fichiers Lua (voir §5).

---

## 1. Vue d'ensemble

### 1.1 Couches et modules

| Couche | Rôle | Fichiers clés |
|---|---|---|
| `src/plugin/` | colle avec l'ABI Windower : chargement, commandes `//aio`, rappels paquets/rendu | `aiohud.cpp` (2 021 l.), `aiohud_probes.cpp` (3 181 l., dev, **ignoré par git**) |
| `src/model/` | données du jeu uniquement : lecture mémoire gardée, paquets, état de groupe, timers, config | `game_mem.cpp`, `party_state*.cpp`, `timers_build.cpp`, `native_ui.cpp` (5 700 l.), `ui_config.cpp`, `*_gen.h` |
| `src/gfx/` | backend D3D8 sans connaissance du jeu : quads AA, polices GDI→atlas, textures BGRA | `d3d.h`, `draw.cpp`, `font.cpp`, `texture.cpp`, `window.cpp` |
| `src/ui/` | widgets + compositeur HUD + config immédiate | `hud.cpp`, `party.cpp`, `target.cpp`, `minimap.cpp`, `hud_chat.cpp`, `hud_actmenu.cpp` (8 094 l.), `hud_scfx.cpp` (3 270 l.), `config_*.cpp` |
| `include/` | enveloppes de l'ABI plugin | |
| `updater/` | addon Lua `aioupdate` (mise à jour depuis les releases GitHub) | |

**Règle de dépendance** : `ui → model + gfx`, `model → rien`, `gfx → rien`.
- **[code]** Elle est respectée sauf une exception documentée : `gfx/font.cpp:5`, `gfx/texture.cpp:5` et `gfx/window.cpp:5` incluent `model/paths.h`, qui est un simple utilitaire de chemins Win32.
- **[code]** `docs/architecture/layers.md` affirme encore « gfx → rien ».

### 1.2 Flux de données

Tout se passe sur le thread du jeu, à chaque image.

```
                     ┌──────────────── Windower 4 (hôte) ────────────────┐
  paquets entrants ──┤ aio_plugin_incoming (aiohud.cpp)                  │
                     │   └─> model : decode 0x028/0x076/0x063/... ──┐   │
                     │                                              ▼   │
                     │            party() singleton  (roster, buffs, hate, timers, SC)
                     │                                              ▲   │
  image (rendu) ─────┤ aio_plugin_render6 (aiohud.cpp:425)          │   │
                     │   1. native_ui_tick   (native_ui.cpp:5315) ──┼── écrit DANS le client (fenêtres natives,
                     │                                              │   crochets JMP, capture de texte)
                     │   2. Hud::render      (hud.cpp:215)          │
                     │      a. poll_game_state (game_mem.cpp:1649) ─┘  lecture mémoire SEH -> GameState f.game
                     │      b. model_frame_upkeep (party_state.cpp:2783)
                     │      c. dCreateSB(D3DSBT_ALL)        (hud.cpp:396)   <- sauvegarde de TOUT l'état D3D
                     │      d. widgets_[i]->draw(f)         (hud.cpp:433-446)
                     │         + ~55 *_draw (actmenu, chat, scfx, ...)  (hud.cpp:450-516)
                     │         + config_.draw
                     │      e. dApplySB + dDelSB            (hud.cpp:545)   <- restauration
  //aio commande ────┤ aio_plugin_command -> anneau -> aio_command_dispatch (aiohud.cpp:1098-1978)
  //unload ──────────┤ aio_plugin_unload (aiohud.cpp:939) — AUTRE THREAD que le rendu (aiohud.cpp:313-316)
                     └───────────────────────────────────────────────────┘
  disque : data\config.txt + data\profiles\*.txt (lignes positionnelles), data\native_ui_undo.txt, aiohud_debug.log
```

### 1.3 Points d'entrée

- `aio_plugin_load` / `aio_plugin_unload` (`aiohud.cpp`), `aio_plugin_render6` (`:425`), `aio_plugin_incoming`, `aio_plugin_command` (`:2004`).
- Côté hors jeu :
  - `build.bat` produit la DLL ;
  - `tests.bat` produit `build\tests.exe`, une suite hors ligne avec un faux jeu et un faux device ;
  - `package.bat` assemble `dist\` ;
  - la CI (`.github/workflows/build.yml`) fait build + tests + package + release depuis un tag.

### 1.4 Dépendances lourdes entre fichiers

- **[code]** `ui_config.h` (833 l.) est inclus par 63 fichiers, `gfx/draw.h` par 54, `party_state.h` (1 165 l.) par 38.
  `ui_config()` est appelé depuis 55 `.cpp`, `party()` depuis 24. Ce sont des singletons globaux, donc un couplage implicite fort.
- **[code]** Les globaux partagés entre fichiers ne sont pas déclarés dans des en-têtes :
  - `g_eqRule`, `g_rg`, `g_trade`, `g_key`, `g_jrn`, `g_miss` dans `native_ui.cpp:84-91, 1427, 2353, 3760` ;
  - `extern float g_trBox[4]` déclaré **à l'intérieur de corps de fonction** (`hud_actmenu.cpp:1285, 4633`).
- **[code]** Nombre de globaux mutables par fichier : `native_ui.cpp` 128, `hud_scfx.cpp` 48, `hud_actmenu.cpp` 47, `aiohud.cpp` 21.

---

## 2. Performance en jeu

**Ce qui va bien [code]**
- Pas d'allocation de tas trouvée dans les chemins chauds. `new`/`HeapAlloc` n'apparaissent qu'au chargement, au bake ou au clic.
- `item_name` fait une recherche dichotomique (`itemnames_gen.h:23561`).
- Les pertes de device oublient bien les handles (`hud.cpp:253-280`).
- La texture de distorsion est libérée 5 s après sa dernière utilisation.

**Ce qui manque : aucune mesure de temps par image.**
- Il n'existe pas de `//aio perf`.
- `sx_perf` (`hud_scfx.cpp:3097-3122`) ne mesure que la démo `//aio scfx`, pas le rendu réel (`sc_circle_draw` `:3176`).
- **Tous les coûts ci-dessous sont donc des estimations [à vérifier en jeu].**

| # | Constat | Preuve | Gain estimé | Correctif | Effort |
|---|---|---|---|---|---|
| P1 | **Un state block complet est créé, appliqué et détruit à chaque image** **[vérifié]** | `hud.cpp:396` `dCreateSB(dev, D3DSBT_ALL)` ; `hud.cpp:545` `dApplySB(dev,tok); dDelSB(dev,tok);` | 0,1-0,5 ms/image, plus sous dgVoodoo | Créer le bloc une fois par device et le rafraîchir par `CaptureStateBlock`. Mieux : un bloc enregistré (`Begin/EndStateBlock`) limité aux ~25 états qu'AioHUD touche. L'oublier à la perte de device (règle 4). | S |
| P2 | **Points de minimap : environ 4 draws et 200 sin/cos par entité** | `minimap.cpp:813-814` appelle 2 `disc()` par PNJ/PJ ; `disc()` (`draw.cpp:203-225`) fait 2 `DrawPrimitiveUP` de 24+ segments | En ville avec 300 entités : 1 000+ draws, environ 60 000 appels trigo | Sprite de point pré-calculé, toutes les entités dans une liste de triangles, 2-3 draws au total | S-M |
| P3 | **Texte : contour 8 directions = 9 fois la géométrie ; 21 états par `Font::begin`** | `font.cpp:484-523`, `:489-491`, `:416-442` ; environ 467 appels dans `src/ui` | Probablement le premier poste de draws | Lot de glyphes sur toute l'image, par atlas ; glyphe « contouré » pré-calculé dans l'atlas (1 quad au lieu de 9) | M |
| P4 | **Aucun filtre des changements d'état redondants** | `d3d.h:76-80` appelle la vtable directement ; `dColorQuadState` fait ~20 appels (`d3d.h:121-131`, ~100 sites) ; `rrect_masked` appelle les deux (`draw.cpp:281,301`) | Large, touche tous les modules | Tableaux d'ombre `RS[256]`, `TSS[2][32]`, `tex[2]` dans `dSetRS`/`dSetTSS`/`dSetTex`, invalidés autour du state block. Le motif existe déjà localement (`hud_scfx.cpp:134-139`, `fx_state`). | S |
| P5 | **Fiche de carte cherchée par balayage linéaire à chaque image** | `game_mem.cpp:500-521` (jusqu'à 2 048 fiches × 2 lectures gardées), appelée depuis `:1676` | Jusqu'à ~4 000 lectures SEH par image | Cache par (zone, sous-carte), re-scan seulement quand la clé change, avec retry borné (règle 10) | XS |
| P6 | **Game UI : la liste des menus ouverts du client est parcourue ~20 fois par image** | `native_ui_tick` (`native_ui.cpp:5336-5429`) : ~28 sites de parcours (`1672, 1786, 1835, 2431...`), jusqu'à 400 nœuds ; `apply()` en O(defs × g_eN) chaque seconde (`1707-1720`) | Moyen, seulement si Game UI est activé | Un snapshot `{obj, def, nom, rect}[64]` par image, que tous les pollers interrogent (c'est la règle 6 appliquée au client) | M |
| P7 | **Le tableau des entités est copié et balayé 3 fois** | `game_mem.cpp:173-209`, `220-245`, `250-277` (O(2 304 × n)), chacun avec sa copie de 9 Ko | Moyen en zone peuplée | Un snapshot d'entités par image et un index id→slot | M |
| P8 | **Rectangle arrondi : jusqu'à 15 draws** | `rrect` `draw.cpp:305-379` ; masqué `260-303` ; mêmes formes `519-578`, `646-716` | Toutes les boîtes et barres | Tables cos/sin par quart de cercle, une seule liste de triangles | S-M |
| P9 | **Moteur des sphères de skillchain** | `bloom()` = 1 draw chacun (`hud_scfx.cpp:356,365`), souvent suivi de `fx_end` qui remet ~25 états ; `distort()` copie 512×512 du back-buffer et calcule 715 sin/cos par image (`1856-1897`) | Moyen tant qu'une chaîne est affichée | Faire passer les blooms par le batcher existant (`bat_add` `:360`), un `fx_end` par groupe, une table trigo dans `distort` | S-M |
| P10 | **Le chat re-découpe ses lignes à chaque image ; le coût croît avec le défilement** | `hud_chat.cpp:1103-1112` (boucle `skipLeft`), 2e passe `772-798` | Faible à moyen, élevé quand on remonte loin | Cache des découpages par ligne, clé (largeur, police) | S |

**Autres points [code]**
- Les atlas de police n'ont pas de budget global : environ 15 Mo de métriques résidentes (`font.h:59-101`), plus des textures sans plafond (`font.cpp:307-341`), dans un processus 32 bits.
- `eased_angle` fait une recherche linéaire sur 256 entrées par mob (`minimap.cpp:107-113`).
- `FontManager::get` utilise `lstrcmpA`, qui dépend de la locale (`font.cpp:403`).

**Recommandation n°1 côté performance : construire `//aio perf` avant de toucher à quoi que ce soit.**
- Une table fixe `Perf[64]{nom, sumUs, maxUs, draws, états}` et une macro `PERF_SCOPE(id)` sur QueryPerformanceCounter.
- Compteurs dans `dDrawUP`/`dSetRS`/`dSetTSS`/`dSetTex` : `d3d.h:76-80` est le point de passage unique.
- Affichage en surimpression, moyenne glissante sur 60 images. Coût nul quand c'est éteint.
- Mesurer dans une ville chargée, minimap active, alliance pleine, chat remonté. **Sans ces chiffres, P1-P10 restent des hypothèses.**

### 2.1 Mesures en jeu (`//aio perf`, 2026-11-24) [mesuré]

`//aio perf` est livré (`src/perf.h`, `src/ui/hud_perf.cpp`, test `tests/t_perf.cpp` contre le faux device). Campagne
`//aio perf auto` ×3, DLL en forme release, Tetsouo en ville (minimap active), 600 images par phase, surimpression
masquée. Temps = temps PROPRE par image (les scopes imbriqués sont soustraits), en µs, moyenne sur 600 images.
Troisième passage, avec les scopes de primitives `gfx.text` / `gfx.rrect` / `gfx.disc` :

| Phase | Total/image | Draws | RS+TSS | gfx.rrect | gfx.text | gfx.disc | Autres postes notables |
|---|---|---|---|---|---|---|---|
| base (ville) | 1 231 | 903 | 2 881 | 245 (444 draws) | 265 | 159 | PlayerHub 98, native_ui_tick 52, state block 26+24 |
| alliance 18 (démo) | 2 590 | 2 947 | 7 421 | **1 049** (1 965 draws) | 514 | 156 | party 171, alliance1/2 79+77 |
| alliance + chat remonté | 2 800 | 2 795 | 7 200 | 1 062 | 389 | 156 | **chat.0 49 → 256, chat.1 34 → 104** |
| sphères (démo scfx all) | 1 741 | 1 189 | 4 824 | 332 | 346 | 168 | scfx.demo 288 (pic 22 ms au 1er passage) |
| config ouverte | 2 910 | 3 365 | 10 475 | **1 438** (2 532 draws) | 479 | 8 | config.preview 313, config.draw 229 |

- **Deux clients** (Kaories chargé dans la même ville) : base 1 252 → 1 323 µs, chat 2 640 → 2 864 µs (+5 à +8 %).
- **Pic hors liste : `poll.fm_tick` = 8,6 à 14,9 ms sur UNE image, dans chaque fenêtre de 10 s** des trois passages
  (moyenne 30 à 70 µs). C'est le réparateur d'adresses FFXiMain (`ffximain_rva.cpp:781`). Absent d'une mesure manuelle
  faite plus tard dans une session (max 63 µs sur 1 160 images) : hypothèse à vérifier, les balayages de
  `heal_menu_ptr` (toutes les ~3 s tant qu'aucun vrai nom de menu n'a été vu après le chargement).
- **Coût de l'instrumentation** (`//aio perf ab`, médianes) : +26 µs, +26 µs, puis +49 µs/image avec les scopes de
  primitives (1 112 → 1 161 µs). Éteint, il ne reste que des tests d'un octet (aucune horloge, aucune écriture).

**Classement d'après les mesures** (gain possible, cas le plus chargé réaliste) :

| Rang | # | Mesuré | Verdict |
|---|---|---|---|
| 1 | P8 rrect | 0,25 ms en base, **1,05 ms** en alliance, 1,44 ms config ; 444 → 2 532 draws | Premier poste. L'audit le plaçait 8e. |
| 2 | P3 texte | 0,27 ms en base, 0,51 ms en alliance ; 171-350 draws, jusqu'à 16 000 primitives | Deuxième, comme prévu. |
| 3 | P4 états | 2 900 (base) à 10 500 (config) appels RS+TSS/image, ~3 par draw | Coût unitaire non isolé : majoré à ~0,1 µs/appel par les scopes qui ne font presque que des états (chat.0 : 49 µs pour 477 appels) → ≤ 0,3 à 0,75 ms. |
| 4 | P10 chat | **+0,28 ms** dès que le chat est remonté (chat.0 ×5) ; 0 sinon | Confirmé, plus cher que « faible à moyen ». |
| 5 | P2 minimap | gfx.disc 0,16 ms, **130 draws** (pas 1 000+) dans cette ville | Réel mais 8 fois sous l'estimation en draws. |
| 6 | P9 sphères | scfx.demo 0,29 ms propre ; pic 22 ms une fois (création de la texture de distorsion) | Seulement pendant une chaîne. |
| 7 | P6 native_ui_tick | 52 µs constant, pics 250-780 µs | Faible. |
| 8 | P1 state block | 26 + 24 = **50 µs** | Sous l'estimation (0,1-0,5 ms). Facile mais petit. |
| 9 | P5 fiche de carte | poll.map_record 18 µs | Déjà en cache (`map_record_cached`) : le constat ne tient plus. |
| 10 | P7 entités | poll.map_entities 9-12 µs, poll.targets 5 µs | Négligeable. |

**Avant P8, traiter le pic `fm_tick`** : c'est la seule image ratée perceptible (15 ms d'un coup = une image perdue à
60 fps), et il n'est dans aucune ligne P1-P10.

---

## 3. Robustesse

Rappel : il n'y a pas d'« erreur Lua » possible. Les équivalents sont **le plantage du client** (lecture ou écriture
mémoire, crochet) et **l'état silencieusement faux** (violation de la règle 10).

**Vérifié sain [code]**
- Le dispatch des paquets : SEH, planchers de longueur `pkt_short`, `idx < 10` pour les trésors, contrôles de longueur des paquets de région.
- `read_member` : une copie gardée de 0x7C, nom borné à 18 caractères.
- Les scanners et le cache de `ffximain_rva` (empreinte + `g_poisoned`).
- La sauvegarde de config : fichier temporaire + `MoveFileEx`, valeurs bornées.
- Les drapeaux `*Fail` de native_ui, qui se relâchent après 5 s de lectures bonnes.

| # | Gravité | Constat | Preuve | Scénario | Correctif | Effort |
|---|---|---|---|---|---|---|
| R1 | **Plantage (plausible)** | **Les crochets JMP sont retirés depuis le thread de `//unload` pendant que le thread du jeu les exécute** **[vérifié : threads distincts ; course non observée]** | `aiohud.cpp:313-316` : « the unload does not run on the window's thread » ; `native_ui_shutdown` (`native_ui.cpp:5439-5444`) → `hook_off` = `memcpy` non atomique de 5 à 8 octets (`:1350-1360`) ; `hk_windraw` (`1539-1548`) enveloppe le dessin des fenêtres du client | `//unload` en pleine image : le jeu exécute des octets à moitié restaurés, ou revient dans du code de DLL déjà libéré | Reprendre le motif du relais WndProc (`aiohud.cpp:336-357`) : chaque JMP vise un stub `VirtualAlloc` **permanent** qui saute via un slot de 4 octets. À l'unload, `InterlockedExchange` du slot vers le trampoline, sans jamais restaurer d'octets. Ajouter un compteur d'appels en vol et une attente bornée (~200 ms). | M (~80 l., 8 crochets) |
| R2 | **Plantage** | **`hook_off` écrase sans vérifier que c'est encore notre JMP, et annonce « unhooked » même en cas d'échec** **[vérifié]** | `native_ui.cpp:1350-1360` : pas de comparaison des octets ; `h.on = false` et le log sont faits même si `VirtualProtect` échoue | Un autre plugin crochète la même fonction après nous : on détruit son patch, et son trampoline saute encore dans notre DLL déchargée. C'est la même famille que le plantage WndProc du 2026-11-09. | Comparer avec notre JMP avant de restaurer, sinon s'appuyer sur le relais de R1 ; en cas d'échec de protect, logger FAILED et garder `on = true` | S |
| R3 | **Plantage (faible probabilité)** | **`apply(true)` passe avant `hook_remove()`, sous un `__except{}` muet** **[vérifié]** | `native_ui.cpp:5443-5444` ; `aiohud.cpp:944` `__try { native_ui_shutdown(); } __except(...) {}` ; même chose chaque image à `aiohud.cpp:434` pour `native_ui_tick` | Une faute dans `apply` saute `hook_remove` : le crochet de déplacement reste et pointe vers une DLL déchargée, et le prochain déplacement de fenêtre plante. Une faute qui se répète chaque image tue Game UI **sans aucune ligne de log**. | `apply(true)` dans son propre `__try`, puis `hook_remove` **toujours** ; log à débit limité dans les deux `__except` (modèle : `hud.cpp:533`) | S |
| R4 | **Faux silencieux** | **Un seul fichier d'annulation partagé par tous les clients en multibox, effacé même après une récupération partielle** **[vérifié]** | `undo_path()` = `data\native_ui_undo.txt` fixe (`native_ui.cpp:1236`) ; `recover_from_file` le supprime même si `miss > 0` (`1250-1261`) ; `save_undo` (`1265`) | Le client A échoue sa restauration, le client B charge et consomme ou efface le fichier : les fenêtres de A restent hors écran. **C'est un candidat sérieux pour la famille de bugs « l'AH ne poppait plus ».** | Un fichier par processus (`native_ui_undo_<pid>.txt`), nettoyage des PID morts, ne réécrire que les lignes manquées | S |
| R5 | Plantage ou écran noir (plausible) | **La texture de distorsion en `D3DPOOL_DEFAULT` reste vivante tant que la fenêtre Skillchains est visible** | `dist_ensure` (`hud_scfx.cpp:1807`) ; créée dès que la fenêtre s'affiche (`3183`) ; `sx_cUse` rafraîchi chaque image (`3182`), donc `sc_circle_idle` ne libère jamais ; la perte de device n'est détectée que par changement de pointeur (`hud.cpp:253`) | Un `Reset` sur le même pointeur (alt-tab plein écran, changement de résolution) échoue tant qu'une ressource DEFAULT est vivante **[à vérifier en jeu]** | Ne créer la texture que pendant une distorsion active ; `TestCooperativeLevel` → `dist_release()` sur DEVICENOTRESET | S |
| R6 | Faux silencieux | **Installation de crochet : adresse calculée sans contrôle de bornes, `memcmp` non gardé, échecs non logués** | `native_ui.cpp:1332` (memcmp), `1583` (`asc = aw + 0x25 + rel` sans `image_range`), `1335/1342` (retours false muets) | Un rel32 faux fait une faute avalée par R3 ; 20 tentatives toutes muettes | Contrôle `lo <= at && at+len < hi`, memcmp gardé, un log par branche d'échec | S |
| R7 | Règle 10 | **Les textures générées du moteur scfx abandonnent après 2,4 s, sans aucun log** **[vérifié]** | `hud_scfx.cpp:232-235` : `sx_genTries >= 8`, 300 ms d'écart ; aucun log de succès, d'échec ou d'abandon | Un échec juste après un zonage laisse les sphères sans texture, sans trace | Log « ready » / « gave up (missing: ...) », recul exponentiel | XS |
| R8 | Faux silencieux | X écrit mais pas Y : l'origine apprise est empoisonnée | `native_ui.cpp:1734` puis la branche de ré-apprentissage `1727-1729`, persistée dans le fichier d'annulation | Fenêtre décalée de façon permanente | Remettre X si Y échoue, et logger | XS |
| R9 | Faux silencieux | Le suivi des fenêtres s'arrête en silence à 768 entrées | `g_e[768]`, `D[768]` (`native_ui.cpp:269, 1713`) pour un parcours jusqu'à 2 000 (`1715`) | Au-delà, une fenêtre n'est ni suivie ni cachée | Log unique de saturation (`capwatch` existe déjà) | XS |
| R10 | Cosmétique | Modes d'adressage de texture laissés modifiés pour le widget suivant | `distort()` laisse CLAMP (`hud_scfx.cpp:1896`) ; `fx_end` (`227-231`) ne le remet pas | Bavures de bord sur une texture dessinée ensuite | WRAP sur l'étage 0 dans `fx_end` | XS |
| R11 | Diagnostic | Le message du state block ment | `hud.cpp:541-544` dit « drawing WITHOUT save/restore this session » alors qu'il retente à chaque image, et ne logue jamais la récupération | Diagnostic trompeur | Compteur + ligne « recovered » | XS |
| R12 | Faible (plausible) | Commande tronquée à 255 caractères sans le dire | `aiohud.cpp:2004` | Argument long silencieusement coupé | Compteur, comme `g_cmdDropped` | XS |

**Équivalents « zonage, changement de job, déconnexion, groupe qui change »**
- Ces transitions sont traitées par le modèle et largement testées hors ligne : 37 fichiers de tests, environ 1 900 `CHECK`, des bandes de rejeu.
- Le risque réel se concentre sur ce qui **écrit dans le client** (native_ui, crochets) et sur le **déchargement**, pas sur la lecture des paquets.
- Le dispatch des commandes `//aio` repose sur des `strstr` n'importe où dans la ligne (`aiohud.cpp:1098-1978`, 76 tests, dont `"on"`, `" off"`, `"hp"`, `"why"`). Un argument contenant « on » peut prendre la mauvaise branche ; le résultat dépend de l'ordre des tests **[code]**.

---

## 4. Santé du code

| # | Constat | Preuve | Recommandation | Effort |
|---|---|---|---|---|
| S1 | **Table de 1,3 Mo des noms d'objets compilée dans 7 unités de compilation**, malgré un commentaire qui dit le contraire **[vérifié + mesuré]** | `party_state.h:1151` : « One definition... every translation unit including it would carry its own copy » ; pourtant `itemnames_gen.h` est inclus par `player.cpp`, `hud_actmenu.cpp`, `hud_treasure.cpp`, `hud_empypop.cpp`, `doctor_html.cpp`, `chat_log.cpp`, `aiohud_probes.cpp`. **[mesuré]** `player.obj` 7,7 Mo, `hud_treasure.obj` 7,6 Mo, `hud_empypop.obj` 7,6 Mo, contre environ 1 Mo pour `native_ui.obj`. | Le générateur produit un `.cpp` plus un en-tête de déclarations ; les fichiers UI passent par `model_item_name`. Même motif `static` dans les autres `*_gen.h` (`spells_gen.h`, `abilities_gen.h` : ~8 inclusions chacun). | 1 h |
| S2 | **Fonctions géantes** | `timers_build_rows` 1 140 l. (`timers_build.cpp:401-1541`) ; `aio_command_dispatch` 880 (`aiohud.cpp:1098-1978`) ; `chat_box_draw` 865 (`hud_chat.cpp:663-1528`) ; `Party::draw` 720 (`party.cpp:537-1260`) ; `ConfigPage::draw` 710 (`config_page.cpp:447-1156`) ; `fx_property` 460 ; `draw_edit_layout` 420 ; `act_ally_buffs` 354 ; `Hud::render` 340 | Découper par étape : une source par fonction pour les timers ; mise en page / onglets / défilement / saisie pour le chat ; une fonction par onglet pour la config | 1-3 j chacune, progressif |
| S3 | **`hud_actmenu.cpp` : 8 094 lignes, 45 panneaux qui répètent le même préambule d'environ 20 lignes** | Occurrences : `float dt = g_xMs ? …` ×43, `block_ease(` ×71, mise à l'échelle `f.screenW / last.uiW` ×50, bornage écran 4 px ×37, `menu_place(f, __LINE__…)` ×49 ; exemple complet `jobs_draw` (`2000-2047`) | Un `begin_native_panel(...)` qui renvoie `{bx, by, bw, bh, A, S, k}` ; découpage par famille (`actmenu_status`, `_unity`, `_equipment`, `_npc`, `_generic`, `_bars`), à faire en miroir pour `native_ui.cpp` (21 `*_poll`) | 2 j |
| S4 | **Le paquet 0x028 est décodé environ 12 fois** | `party_state.cpp:83-108` promet « One implementation now », mais `act_target_debuffs` garde sa propre boucle (`1890-1905`) ; `getbits(p,72,6,size)` répété aux lignes 116, 128, 979, 1024, 1060, 1232, 1276, 1543, 1800, 1839, 1883 ; copie `sl_getbits` (`aiohud_probes.cpp:456`) | Un seul décodage complet en `ActionDecode`, passé à chaque `act_*`. Cela supprime toute la famille de bugs « stride desync ». | 1-2 j |
| S5 | **Config positionnelle : chaque champ s'écrit à 5 endroits à la main** | `ui_config.cpp` : 150 `fprintf`, 132 `sscanf`. Lignes `aw=` (`:255`, 18 champs), `schit=` (`:331`, 14), `sc3=` (`:332`, 17). Lecteurs en échelle `if (n >= k)` (`401-444`). Il faut toucher le struct, les défauts, `save_config_to` (`171-384`), `load_config_from` (`838-1079`) et `persist_eq` (`1326-1506`). La chaîne de chargement est déjà à la limite d'imbrication C1061. | Une table de descripteurs (X-macro) `{clé, type, offset, défaut, min, max}` qui génère save, load, égalité et reset, et écrit en `clé=valeur` nommé. L'ancien parseur reste pour lire une fois les vieux fichiers. | 2-3 j |
| S6 | **Code mort de variantes dans `hud_scfx.cpp`** | Défauts figés : `sx_rv = REV_A2` (`:81`), `sx_lq = 1` (`:90`), `sx_im = 2` (`:92`). Le seul commutateur, `scfx_toggle` (`:2946`), n'est appelé que depuis `aiohud_probes.cpp:1981` (dev, ignoré par git). Inatteignables en release : `reverb_water`, `rev_e`/`fx_rev_e`, `fx_reverberation` (D) — environ 250 l. —, toutes les branches `sx_rv == 0/1/2`, `sx_lq == 0`, `sx_im == 0/1/3`, et environ 150 l. de capture. | Une fois chaque SC validée, supprimer les variantes rejetées et mettre la capture sous `#ifdef AIOHUD_PROBES`. Environ 3 270 → 2 600 lignes. | ½ j |
| S7 | Globaux partagés non déclarés (voir §1.4) | | Un en-tête interne par module, ou une struct d'état par fenêtre | 1 j |
| S8 | **Constantes magiques de couleur** | **[mesuré par l'agent]** 1 945 littéraux `0xFFxxxxxx`, 661 valeurs distinctes. Fichiers les plus touchés : `hud_actmenu` 401, `target` 63, `hud_chat` 53, `config_page` 51. `C_HP` est défini 2 fois avec des valeurs différentes : `config_controls.h:46` 0xFF5ADC5A contre `ui_colors.h:23` 0xFF6FDC74. | Voir §7 (`ui/tokens.h`) | M-L |
| S9 | En-tête obsolète | `ui_config.h:3` dit « Not yet persisted to disk » | Corriger | XS |

**Ce qui est sain [code]**
- Les macros de lignes de config (`config_rows.h`, utilisées par 22 fichiers).
- Les lectures mémoire centralisées (`safe_read` + `valid_ptr`, `game_mem.cpp:70-76`).
- Aucun bloc `#if 0`.
- Les mêmes options de compilation que le produit pour les tests.

Petit point à noter : `native_ui.cpp` contient 32 blocs `__try` écrits à la main, qui pourraient partager un helper de copie gardée.

---

## 5. Vérification et tests sans lancer le jeu

### 5.1 Ce qui existe déjà (et c'est solide) [code]

- `tests.bat` compile `build\tests.exe` avec `/W4 /WX /fsanitize=address /MP`.
- 37 fichiers `tests/t_*.cpp`, environ 1 900 `CHECK`, contre un **faux jeu** (`tests/fake_game.cpp`), des **paquets exacts au bit près** (`tests/fake_packets.h`) et un **faux device D3D** (`tests/fake_device.cpp`).
- Bandes de rejeu et golden files (`dev/replay`).
- La CI lance tests, build, package et une vérification de release. Un garde-fou grep pour la règle 10 bloque ; quatre autres garde-fous ne sont qu'indicatifs depuis juillet.

### 5.2 Ce que j'ai mesuré ici

- **[mesuré]** `luac5.1 -p` passe sans erreur sur les 4 fichiers Lua (Lua 5.1 est installé dans `C:\ProgramData\chocolatey\bin`, ainsi que `luarocks` et `stylua`).
- **[mesuré]** Le build complet prend environ 49 s. Plusieurs builds ont été faits dans cette session : la DLL de 7 964 672 octets est sortie à 09:16 et 09:20. La machine a 12 cœurs (`nproc`).
- Je n'ai pas lancé `tests.bat` : cela écrit dans `build\`, et la consigne était de ne rien modifier.

### 5.3 Ce qui est faisable, outil par outil

| Outil | Pour quoi | Faisable ici ? |
|---|---|---|
| `luacheck` (`luarocks install luacheck`) | Les 4 fichiers Lua : globaux involontaires, variables non utilisées. Config `.luacheckrc` avec `std = "lua51"` et les globaux Windower déclarés (`windower`, `_addon`, `register_event`...). | Oui, en 5 min. Utilité limitée à `aioupdate.lua`. |
| Faux environnement Windower pour Lua | `aioupdate.lua` hors jeu. **Existe déjà** (`dev/scripts/aioupdate_sortie_harness.lua`, avec Windower simulé ; voir la mémoire « Lua 5.1 + updater harness »). | Oui, existe |
| `tests.exe` (C++) | Toute la logique du modèle | Oui, existe. Les manques sont ci-dessous. |
| `clang-tidy` | Un `.clang-tidy` existe. Constat passé : 396 avertissements pour 1 vrai défaut, donc il faut un profil étroit (`bugprone-*`, `clang-analyzer-*`) et ne regarder que le diff. | Oui, mais bruyant |
| Test de rendu sur faux device | Vérifier que chaque widget **rend l'état D3D tel qu'il l'a trouvé** (règles 3 et 8) et que le nombre de draws reste borné. `fake_device.cpp` existe déjà. | Oui, 2-3 h. Fort rendement. |
| Comparaison visuelle sur captures | `//aio scfx capture` existe déjà. Un script PIL qui compare avec des captures de référence (PSNR, % de pixels différents) et produit une planche HTML. | Oui, 3 h |

### 5.4 Les tests manquants qui rapportent le plus

1. **La config sur `sc3=`, `schit=`, `aw=` [code]**
   - `scribble()` dans `tests/t_config.cpp` est écrit à la main et ne touche aucun des nouveaux champs. Une recherche de `sc3|scStyle` dans `tests/` ne trouve rien.
   - Un champ perdu par le parseur passerait donc inaperçu.
   - Correctif : remplir `scribble()` par motif d'octets, comme `dirty_every_field`.
2. **La table des skillchains** : chaque paire ouverture/fermeture vers la propriété, le niveau et les éléments de burst, contre la table de la doc. `t_skillchain.cpp` n'a que 34 CHECK.
3. **`spell_elem`** : quelques contrôles ponctuels contre l'entrée du générateur.
4. **Les décisions pures de `native_ui`** (cacher/montrer selon le masque), extraites pour tourner sans device.
5. **Le test d'hygiène d'état D3D** décrit plus haut, sur le chemin du cercle de skillchain.

---

## 6. Outillage pour aller plus vite avec Claude

### 6.1 Le constat qui domine : la boucle d'itération

**Édition → build 49 s → `//unload` → deploy → `//load` → capture → lecture.**

| Mesure | Preuve | Gain |
|---|---|---|
| **`/MP` absent de `build.bat`** **[vérifié]** | `build.bat:102` : un seul `cl /nologo /LD /O2 ...` sur environ 97 fichiers, sans `/MP` ; `tests.bat` l'a déjà | Probablement 49 s → 10-15 s sur 12 cœurs **[à mesurer]**. Un seul mot à ajouter. |
| Pas de compilation incrémentale | Tout est recompilé à chaque fois | Petit script Python : `.obj` par fichier, dépendances par `/showIncludes`, puis link. Une modification d'un seul widget passerait à quelques secondes. 2-4 h. |
| Table d'objets dans 7 unités de compilation | S1 | Moins de temps de compilation et de link |
| Liste des sources tapée à la main sur une ligne d'environ 8 000 caractères | Le commentaire de `build.bat` rappelle qu'on a déjà heurté la limite de 8 191 caractères de cmd (2026-09-14) | Fichier de réponse généré depuis `git ls-files 'src/*.cpp'`, ce qui garantit aussi que « non suivi = non compilé » |
| Réglages de VFX compilés en dur | Des dizaines de flottants dans `hud_scfx.cpp` ; le rechargement à chaud de la config existe déjà (`ui_config.cpp:1242-1272`) | Un `data\tune.txt` de dev, relu quand son horodatage change, via une macro `TUNE("rev.crest_w", 1.2f)` compilée hors release. Une retouche visuelle passerait d'environ 90 s à environ 1 s. 2-3 h. **Le plus gros gain pour le travail actuel sur les sphères.** |

### 6.2 Brouillon de CLAUDE.md

Le CLAUDE.md actuel est déjà bon : 10 règles non négociables, et des règles détaillées par chemin dans `.claude/rules/`.
Ce brouillon le **resserre** et le **corrige** sur les points que l'audit fait apparaître.

```markdown
# CLAUDE.md — AioHUD

Plugin Windower 4 natif (C++17, 32 bits, Direct3D 8 pipeline fixe) qui redessine l'UI de FFXI. PAS un addon Lua
(seul updater/aioupdate est en Lua 5.1). Règles détaillées : `.claude/rules/` (chargées par chemin).

## Boucle
build.bat (vérifier l'horodatage de build\AioHud.dll) -> //unload AioHud -> deploy.bat -> //load AioHud
tests.bat (hors jeu, ASan) avant tout commit qui touche src/model.
Tuning visuel : data\tune.txt (dev, rechargé à chaud) -- pas de rebuild pour un flottant.
Perf : //aio perf -- mesurer AVANT et APRÈS toute optimisation.

## Couches
gfx (D3D, zéro jeu) <- model (jeu, zéro rendu) <- ui (widgets) <- plugin (ABI). paths.h vit dans src/io/.

## Règles qui mordent
1. snap() + demi-pixel. 2. Feather tout ou rien. 3. Remettre le blend après un passage additif.
4. on_device_lost OUBLIE (0) ; dispose Release. Pas de ressource D3DPOOL_DEFAULT gardée au repos.
5. Toute lecture mémoire du jeu sous SEH (safe_read/valid_ptr). Toute ÉCRITURE dans le client : bornée, vérifiée, loguée.
6. Snapshot : seuls les pollers lisent la mémoire, une fois par image.
7. Un offset = une source (poller), documenté dans docs/game-data/.
8. Pas d'état D3D sale laissé au widget suivant.
9. Contrôles config : CTRL_ID comme uid ease().
10. Échec transitoire : retry borné, drapeau ok explicite, succès ET abandon logués. Pas de __except{} muet.
11. Crochets : jamais restaurer des octets depuis le thread d'unload ; relais permanent + slot atomique.
12. Toute nouvelle source .cpp/.h est ajoutée à git DANS LE MÊME commit que son usage.

## Conventions
Pas d'allocation par image. Tables générées (`*_gen.h`) : ne pas éditer, une seule unité de compilation.
Config : un champ = une ligne de la table de descripteurs. Couleurs : ui/tokens.h, jamais un littéral.
Commit seulement sur demande ; jamais de trailer Co-Authored-By ni d'attribution IA. Branche unique : main.
Réponses à l'utilisateur en français ; code et commits en anglais.
```

### 6.3 Hooks Claude Code utiles

Configuration dans `.claude/settings.json`, via la commande `/update-config`.

| Hook | Déclencheur | Action |
|---|---|---|
| **Garde des fichiers non suivis** | `PreToolUse` sur `Bash(git commit*)` | Refuser si un fichier cité dans `build.bat`/`tests.bat` ou inclus par un fichier suivi n'est pas dans `git ls-files`. **Il aurait bloqué l'état actuel.** |
| Compilation rapide d'un fichier | `PostToolUse` sur `Edit\|Write` de `src/**/*.cpp` | `cl /c /Zs` (syntaxe seule) sur le fichier modifié, quelques secondes, pour attraper C4189/C4100 avant le build de 49 s. |
| `luac -p` / luacheck | `PostToolUse` sur `*.lua` | Vérification de syntaxe Lua 5.1 |
| Interdit d'attribution | `PreToolUse` sur `git commit` | Refuser si le message contient `Co-Authored-By` (règle dure du projet). |
| Rappel d'horodatage | `PostToolUse` sur `build.bat` | Afficher l'horodatage et la taille de la DLL. |

### 6.4 Sous-agents et commandes

- **Les agents existent déjà** dans `.claude/agents/` : `asset-pipeline`, `code-reviewer`, `docs-writer`, `gfx-render`, `re-probe`, `ui-widget`.
- **À ajouter :**
  - **`perf-auditor`** : lit un journal `//aio perf` et propose le prochain point chaud à traiter.
  - **`scfx-capture-review`** : assemble les planches PIL d'une série `scfxshot\`. C'est ce que je fais à la main à chaque « check ».
- **Commandes personnalisées** (`.claude/commands/`) :
  - `/ship-check` : build + tests + horodatage + `git status` des fichiers non suivis compilés ;
  - `/capture-sheet <série>` ;
  - `/release <ver>` : la règle changelog-release en liste de contrôle.

---

## 7. Assets et UI

### 7.1 Inventaire [mesuré par l'agent]

- `assets/` pèse 91 Mo, dont 48 Mo dans `rolls_src/` (ignoré, non utilisé par le code).
- Types de fichiers : 1 323 `.bmp`, 1 130 `.png`, 198 `.raw`, 52 `.dds`.
- Postes principaux :

| Dossier ou fichier | Taille | Contenu |
|---|---|---|
| `gearicons/` | 11 Mo | 1 323 BMP |
| `bstpets/` | 5,5 Mo | |
| `window/` | 4,7 Mo | 12 thèmes × coin/cadre/fond |
| `buff_atlas.raw` | 2,5 Mo | |
| `pointers.raw` | 2 Mo | |
| `chat_icons_src` | 8,4 Mo | 7 PNG de 1 Mo chacun : surdimensionnés |

- **Chargement [code]** : tout passe par `gfx/texture.cpp`, en pool MANAGED, avec un chargement paresseux et un retry borné (`ui/tex_retry.h`, 12 essais × 300 ms).
  - Les mipmaps sont optionnels.
  - Des atlas existent là où c'est utile (buffs, jobs, chat, pointeurs).
  - Les `.raw` n'ont pas d'en-tête : leurs dimensions sont codées en dur à l'appel (`gfx/window.cpp:104-107`). Une source redimensionnée serait mal échantillonnée sans le moindre avertissement.

### 7.2 Constats

| # | Constat | Preuve | Recommandation |
|---|---|---|---|
| A1 | **Le prochain `package.bat` livrerait les kits de thème d'essai** | `package.bat:43-44` robocopie tout `assets/` sauf `*_src`, donc aussi `assets/themes/` (227 fichiers, non suivi) ; `verify_payload.ps1` ne le verrait pas | `/XD themes`, ou copier depuis `git ls-files assets` |
| A2 | **Sources d'assets hors du dépôt : impossible de régénérer depuis git** | `gen_grimoire.py:4,26` (dossier d'addon Windower), `gen_smn_art.py` (Téléchargements), `gen_geo_icons.py` (`F:\Telechargement Divers\...`), `gen_buff_atlas.ps1:3,21` (`assets/buffIcons/` absent), `gen_weapon_icons.py:4` (`assets/bst/` absent) | Ajouter les sources au dépôt (ou à un stockage externe) et corriger les chemins morts |
| A3 | **Charte « or sur bleu nuit » ≠ palette livrée** | `config_controls.h:19-46` : palette « graphite » (C_CONTENT 0xD20F161D, C_CARD 0xF4232C35). `C_GOLD` est un alias de l'accent **choisi par l'utilisateur** (`:50-52`) ; l'or fixe n'existe que dans `C_METAL*` à côté de `C_STEEL*`. La doc décrit un système « deux métaux or/acier » (`docs/architecture/config-panels.md:56-75`). | **Décider** quelle référence est canonique (question Q3) |
| A4 | **Pas de jeu de tokens pour le HUD** | `config_controls.h` couvre seulement la config ; `ui/ui_colors.h` et `ui/palette.h` sont partiels ; `C_HP` est en double (S8) ; 1 945 littéraux | Un `ui/tokens.h` (surfaces, texte, métaux, sémantique PV/PM/TP) ; migrer d'abord les doublons sémantiques, puis `hud_actmenu` |
| A5 | **La grille de 8 px n'est pas suivie** | Hauteurs de lignes de config : `ROW_BAND(52)` ×91, `ROW_NEXT(42)` ×87, `ROW_BAND(46)` ×85, seuls 48 et 40 sont des multiples de 8 (`config_rows.h:44-66`). Dans `src/ui`, 1 495 littéraux passés à `snap`/`ROW_*` ne sont pas des multiples de 8, contre 2 321 qui le sont. | Tokens `SP=8`, demi-pas 4, lignes 40/48/56 ; revoir la mise en page visuellement |
| A6 | **Trois systèmes de cadres** | FFXI raw (`gfx/window.cpp`, tuiles 32×32), procédural (`box_style`, `texture.h:22-25`), kit PNG (`theme_kit.cpp`, GDI+, non suivi), plus `frame_bake.cpp` (non suivi) | Garder `draw_themed_box` comme point d'entrée unique ; décider du sort des kits (pause actuelle) |
| A7 | **Icônes d'équipement : la moitié du paquet** | 1 323 BMP = 11 Mo sur un `dist` de 26 Mo, alors que `texture.h:43-44` dit que le décodage ROM est « le chemin normal » | Supprimer cette amorce ou la regrouper dans un seul fichier : environ 10 Mo et 1 300 écritures de moins par mise à jour |
| A8 | Binaires générés suivis avec leurs sources | `.raw` et `*_src` versionnés tous les deux : chaque régénération ajoute un blob à l'historique (pack 27,7 Mo **[mesuré]**) | Git LFS pour `*.raw`, `*.bmp`, `*.dds`, ou n'en garder qu'un des deux |

---

## 8. Hygiène du dépôt

| # | Gravité | Constat | Preuve | Recommandation |
|---|---|---|---|---|
| H1 | **CRITIQUE** | **Un clone propre ne compile plus, et une release taguée échouerait** **[vérifié]** | `build.bat` compile `src\ui\hud_scfx.cpp` (1 occurrence) ; `git ls-files hud_scfx.cpp sc_circle.h spell_elem_gen.h` renvoie **0** ; `party_state.cpp:14` inclut `spell_elem_gen.h` ; `hud_skillchains.cpp:7` et `hud_absorb.cpp:11` incluent `sc_circle.h` ; `tests.bat` compile `party_state.cpp`. Environ 4 300 lignes n'existent que sur un disque. | Commit unique : `hud_scfx.cpp`, `sc_circle.h`, `spell_elem_gen.h`, `gen_spell_elem.py` + les 19 fichiers modifiés. **5 minutes, le jour même.** |
| H2 | Élevée | Des sources de dev n'ont aucun historique | `aiohud_probes.cpp` (ignoré), `theme_kit.cpp` et `frame_bake.*` (non suivis) ; `dev/` a son propre dépôt, dernier commit le 2026-10-29, 46 changements en attente ; `docs/*` est ignoré sauf `game-data/`, `audits/`, `songs/`, donc `docs/architecture/` n'a pas d'historique. (Ce rapport, d'abord écrit dans `docs/`, a été déplacé dans `docs/audits/` pour être versionné.) | Un dépôt privé distant pour `dev/` + docs + CLAUDE.md, commit quotidien |
| H3 | Moyenne | Racine encombrée : environ 110 Mo de travail de design à un `git add -A` d'un commit | `AioHUD_Planches_et_Maquettes/` 50 Mo, `Items San d'oria/` 23 Mo, `AioHUD_Planches_Skillchains/` 11 Mo, `AioHUD_Cinq_planches.pdf` 4,7 Mo, des PNG à la racine, `ReponseGpt.md`, `assets/themes/` ; `.gitignore` rappelle que l'erreur est **déjà arrivée** (34 PNG commités) | Un dossier `_local/` ignoré qui regroupe tout ; `/*.pdf`, `/*.png` à la racine dans `.gitignore` |
| H4 | Moyenne | La CI ne fait pas tourner `scripts/check_repo.py` (seul le hook pre-commit local le fait), ni de contrôle « tout fichier cité dans le build est suivi » ; 4 garde-fous ne sont qu'indicatifs | `.github/workflows/build.yml` | Ajouter les deux étapes ; rendre les garde-fous bloquants ou les retirer |
| H5 | Faible | `build/` contient environ 297 Mo, avec des `.obj` de fichiers supprimés (`timers_legacy.obj`, `timers_shadow.obj`) et une soixantaine de vieux logs **[mesuré : `du`]** | | Faire un clean de temps en temps |
| H6 | Faible | Fins de ligne : git signale que `build.bat` et 4 autres fichiers modifiés sont en LF et passeront en CRLF | `git diff` | `.gitattributes` (`*.bat text eol=crlf`) |
| H7 | Info | Release : construite depuis le tag poussé, avec une vérification du changelog (`RELEASES[]`). C'est bien, **mais H1 casse n'importe quel tag aujourd'hui**, et un `package.bat` local livrerait `assets/themes` (A1). | | |
| H8 | Info | Le `README.md` racine date du 2026-09-07 (dernier commit), donc il est probablement en retard sur Game UI, Chat et les sphères | `git log -1 -- README.md` | Une page utilisateur par grande fonction, avec la liste des commandes livrées (règle `shipped-commands`) |

---

## 9. Feuille de route

### Phase 1 : gains rapides (moins d'une journée)

1. **Commiter les fichiers non suivis dont le build dépend** (H1). 5 min. C'est le seul risque de perte réelle.
2. **Ajouter `/MP` à `build.bat`** (§6.1). 1 min, puis mesurer le nouveau temps de build.
3. **Empêcher `assets/themes` de partir dans le paquet**, et ranger la racine dans `_local/` (A1, H3). 20 min.
4. **Sécuriser l'arrêt de native_ui** :
   - `apply(true)` dans son propre `__try` et `hook_remove` toujours ;
   - logs à débit limité dans les deux `__except{}` muets (R3) ;
   - `hook_off` qui vérifie nos octets et ne ment pas (R2). 1 h.
5. **Un fichier d'annulation par PID**, sans suppression après une récupération partielle (R4). 1 h.
6. **Logs règle 10** : textures scfx (R7), échecs de `hook_on` (R6), table de 768 pleine (R9), message du state block (R11). 30 min.
7. **Une seule définition de la table des noms d'objets** (S1). 1 h.
8. **Cache de la fiche de carte** (P5). 30 min.
9. **WRAP remis dans `fx_end`** (R10), en-tête `ui_config.h` corrigé (S9). 5 min.

### Phase 2 : chantiers de 1 à 2 semaines

1. **`//aio perf`** : portées QPC + compteurs de draws et d'états. **C'est un préalable à toute optimisation.**
2. **State block persistant et minimal** (P1), puis **filtre des états redondants** (P4). Mesurer avant et après.
3. **Build incrémental** (script Python + `/showIncludes`), avec une liste de sources générée depuis git (§6.1).
4. **`data\tune.txt` rechargé à chaud** pour les VFX (§6.1).
5. **Crochets sur relais permanent** (R1), pour les 8 crochets native_ui.
6. **Décodage unique du 0x028** (S4).
7. **Dispatch des commandes par table** de verbes (`{verbe, handler, livré?}`), qui génère aussi la liste des commandes livrées (§3).
8. **Tests** : champs `sc3`/`schit`/`aw` dans `scribble()`, table des skillchains, hygiène d'état D3D sur faux device (§5.4).
9. **Hooks Claude** : garde des fichiers non suivis et interdiction d'attribution (§6.3).
10. **Nettoyage de `hud_scfx.cpp`** une fois les 15 SC validées (S6).

### Phase 3 : chantiers de fond

1. **Config par table de descripteurs** et format `clé=valeur` nommé, avec lecture unique de l'ancien format (S5).
2. **Découpe de `hud_actmenu.cpp` et `native_ui.cpp`** par famille de menus, avec `begin_native_panel` (S3) et un snapshot unique de la liste des menus ouverts (P6).
3. **Découpe des fonctions géantes** : timers, chat, Party, ConfigPage (S2).
4. **Rendu par lots** : texte (P3), rectangles arrondis (P8), minimap (P2), blooms (P9).
5. **`ui/tokens.h` et grille de 8 px**, une fois la charte tranchée (A3-A5).
6. **Assets** : sources dans le dépôt, LFS, amorce d'icônes d'équipement supprimée ou regroupée (A2, A7, A8).
7. **Dépôt privé distant** pour `dev/`, les docs et CLAUDE.md (H2).
8. **Snapshot d'entités unique** (P7), budget global des atlas de police.

---

## 10. Questions pour toi (le code ne permet pas de trancher)

1. **Lua ou C++ ?** Ta demande décrit un addon Lua. Est-ce que tu envisages de revenir à Lua, de garder un module Lua à côté, ou est-ce que le cadre « Lua » venait d'un ancien modèle de prompt ? La réponse change complètement les §5 et §6.
2. **`dev/`, `aiohud_probes.cpp`, docs et CLAUDE.md** : ils sont exclus du dépôt public **exprès**. Veux-tu un **dépôt privé distant** (GitHub privé) pour qu'ils aient un historique hors de ton disque ?
3. **La charte** : « or sur bleu nuit, grille de 8 px » est-elle la référence ? Le code livré suit une palette graphite avec un accent au choix et un système or/acier documenté. Laquelle fait foi ?
4. **Multibox** : joues-tu plusieurs clients en même temps avec AioHUD et Game UI actifs ? Si oui, R4 passe en priorité 1, et c'est peut-être la vraie cause de « l'AH ne poppait plus ».
5. **`//unload` en jeu** : le fais-tu souvent pendant le jeu (hors dev) ? Cela fixe l'urgence de R1.
6. **Fullscreen / alt-tab** : utilises-tu le plein écran exclusif, ou dgVoodoo en fenêtré ? (R5 n'existe qu'avec un `Reset` du device.)
7. **Kits de thème (`assets/themes`, `theme_kit`, `frame_bake`)** : sont-ils abandonnés, en pause ou destinés à une release ? Cela décide s'il faut les commiter ou les exclure.
8. **Icônes d'équipement (11 Mo)** : leur amorce est-elle encore utile maintenant que le décodage ROM est le chemin normal ?
9. **Variantes VFX** : une fois les 15 skillchains validées, peut-on supprimer définitivement les variantes rejetées, ou veux-tu les garder comme options dans la config ?
10. **Ancien format de config** : combien de versions en arrière faut-il savoir relire (pour la migration vers `clé=valeur`) ?
