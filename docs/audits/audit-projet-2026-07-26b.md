---
title: Audit projet complet — 2026-07-26 (seconde passe)
summary: Passe transverse conduite après l'audit du matin (docs/notes/audit-full-2026-07-26.md) et après les correctifs 50117e7/358d9d8. Sept constats nouveaux, dont un défaut d'outillage qui explique pourquoi le précédent avait dû trouver mmTried_ à la main. Statut vérifié des items encore ouverts.
---

# Audit projet complet — 2026-07-26 (seconde passe)

Conduit selon `docs/audits/audit-protocol.md` : standard de preuve à quatre éléments, règle zéro (attaquer
le code récent en premier), et section « Vérifié et SAIN » obligatoire.

**Ce rapport ne rejoue pas l'audit du matin.** `docs/notes/audit-full-2026-07-26.md` a été lu intégralement
avant de commencer ; ses constats encore ouverts sont listés en §4 avec leur statut **vérifié**, pas
re-instruits. Tout ce qui est en §2 est nouveau.

---

## 1. Couverture

**Lus intégralement** : `plugin/aiohud.cpp` (1058), `io/json.h`, `ui/buff_atlas.cpp`, `assets/aioupdate.ps1`,
`build.bat`, `tests.bat`, `.github/workflows/build.yml`, `docs/audits/audit-protocol.md`,
`docs/notes/audit-full-2026-07-26.md`.

**Lus par section ciblée** : `model/game_mem.cpp` (ancres + trois balayages du tableau d'entités +
`poll_game_state`), `model/ui_config.cpp` (`save_config_to` / `load_config_from` / `persist_eq`),
`model/party_state.cpp` (`on_action`, chemin WS-popup, bornes des tables indexées par statut),
`ui/hud.cpp` (`render`, bloc device-lost, `doctor`), `ui/config_controls.cpp` (`ease`, `ctrl_uid`),
`ui/target.cpp` (primitives de barre), `ui/config_page.cpp` (cycle de vie des handles empruntés).

**Mesuré, pas lu** : build complet instrumenté (42 warnings, ventilés par code et par fichier) ;
diff mécanique clés écrites / clés relues de la config ; diff mécanique champs `UiConfig` / champs comparés
par `persist_eq` ; exécution locale des cinq garde-fous CI.

**NON couvert — à traiter comme angle mort de cette passe :**
- **Tout `src/gfx/`** sauf lecture des signatures `on_device_lost`. Les primitives AA, le feathering, le
  stencil, l'atlas de police : non audités. C'est la couche que personne ne peut tester hors device.
- **`hud_timers.cpp` (1200 l.) et `hud_zonetracker.cpp` (654 l.)** : parcourus par grep, pas lus. Ce sont
  les deux modules les plus récents et les plus gros après la config.
- **Les 6 générateurs Python** de `scripts/` et la fidélité des tables `*_gen.h` à leurs sources.
- **Le rendu** : aucun constat visuel n'est possible sans jeu. Le constat §2.2 est un raisonnement sur le
  type, pas une observation à l'écran.

---

## 2. Constats nouveaux

### [S1] Le garde-fou CI « règle 10 » est aveugle à la convention de nommage des membres du projet

- **Où** : `.github/workflows/build.yml:47-50`
- **Code** :

      if git grep -nE 'static bool +[A-Za-z_]*tried|[A-Za-z_]*[Tt]ried *= *true' -- 'src/*' ':!src/ui/tex_retry.h'; then
        echo "::error::one-shot 'tried' latch (rule 10) -- use the bounded retry in ui/tex_retry.h"; fail=1; fi

- **Scénario d'échec** : la convention du projet suffixe les membres de classe par `_`. Le motif exige
  `Tried` suivi d'espaces optionnels puis `=`. `mmTried_ = true` n'est donc **jamais** signalé — ni aucun
  autre latch porté par un membre. Exécuté sur l'arbre courant :

      regex actuelle          -> 6 hits, tous des locales (map_dat.cpp, paths.cpp)
      même regex avec `_?`    -> 9 hits : + config_page.cpp:1495, :1517, :1535

- **Pourquoi c'est réel** : les trois sites manquants sont `if (!mmTried_) { minimap_help_textures(...);
  mmTried_ = true; }`, c'est-à-dire la forme exacte que la règle 10 interdit. L'audit du matin les a trouvés
  **à la main** et les a qualifiés de « dernier vrai latch one-shot de texture du dépôt, oublié par la passe
  qui a corrigé Timers/ZoneTracker/Debuffs/Treasure ». Ils n'ont pas été oubliés par distraction : l'outil
  censé les rendre impossibles à oublier ne peut pas les voir. Le coût est structurel, pas ponctuel — tout
  futur latch écrit en membre passera aussi.
- **Réfutation tentée** : j'ai vérifié que ce n'était pas l'exclusion `:!src/ui/tex_retry.h` qui filtrait
  (les hits sont dans `src/ui/config_page.cpp`), ni `continue-on-error` (celui-ci masque un hit *signalé*,
  pas un hit *non détecté* — ici il n'y a aucune ligne dans le rapport CI). J'ai aussi vérifié que la seconde
  alternance `static bool +[A-Za-z_]*tried` ne les rattrapait pas : `mmTried_` est déclaré `bool mmTried_ = false;`
  en membre, sans `static`. Le trou tient.
- **Confiance** : CONFIRMÉ
- **Correction suggérée** : `[Tt]ried_? *= *true` dans la règle, et ajouter au même passage une alternance
  sur `if (![A-Za-z_]+[Tt]ried_?)` qui attrape la lecture du latch autant que son écriture.

### [S2] `bw` déclaré `u32` dans une liste, ce qui tronque une largeur en pixels — et l'annule sous 1,82 px

- **Où** : `src/ui/target.cpp:189-191`
- **Code** :

      if (fw < w - 1.0f) {   // hot bright edge at the fill tip
          const u32 e0 = mul_a(base, 0.0f), e1 = mul_a(scl(base, 1.7f), a), bw = h * 0.55f;
          rrect_clip_begin(dev, x, y, w, h, r);
          grad_quad(dev, x + fw - bw, y, bw, h, e0, e1, e0, e1);

- **Scénario d'échec** : `bw` hérite du `u32` de la liste de déclaration, donc `h * 0.55f` est **tronqué à
  l'entier**. `h` vient de `barH = snap(18.0f * S * ui_config().tgtBarH)` (`target.cpp:522`), avec
  `tgtBarH` borné à `[0.10, 4.0]` par la config. À `tgtBarH = 0.10` sur un écran 1080p (`S ≈ 0.77`),
  `barH ≈ 1.4` → `bw = 0.76f` → `0` → `grad_quad` de largeur nulle : **le liseré lumineux en bout de jauge
  disparaît entièrement**. Aux réglages courants (`barH ≈ 14`) il vaut `7` au lieu de `7.7` : le liseré est
  quantifié au pixel entier et « saute » d'un cran au lieu de glisser quand on bouge le slider.
- **Pourquoi c'est réel** : le compilateur le signale trois fois (`C4244` sur `target.cpp:189` ×2 et `:191`),
  et `build.bat:40` déclare explicitement garder C4244 actif *parce qu'il mord*. Le chemin est le rendu
  normal de la barre de cible (`target.cpp:702`), pas un cas limite.
- **Réfutation tentée** : j'ai cherché si `bw` était retypé ou re-calculé avant usage — non, il part
  directement dans `grad_quad` (d'où le troisième C4244, u32→float au retour). J'ai vérifié que le troisième
  usage `x + fw - bw` ne pouvait pas produire un dépassement : `fw` et `bw` sont positifs, la promotion se
  fait en float. Le défaut se réduit donc à la troncature, pas à un dépassement. Sévérité **cosmétique**, et
  je la déclare telle : ce n'est pas un S1.
- **Confiance** : CONFIRMÉ
- **Correction suggérée** : sortir `bw` de la liste et le déclarer `const float`.

### [S2] Le filtre WS-popup est passé de 5 messages acceptés à 1, mesuré sur un seul personnage

- **Où** : `src/model/party_state.cpp:876-888` (commit `2f173a2`, le plus récent changement fonctionnel)
- **Code** :

      const bool wsMsgIsReal = (wsMsg == 185);
      if (cat == 3 && actor == selfId_ && sc_is_finish_msg(wsMsg) && !wsMsgIsReal) { ... suppression log ... }
      if (cat == 3 && actor == selfId_ && wsMsgIsReal) { ... popup ... }

  avec `src/model/skillchain.h:114-116` :

      inline bool sc_is_finish_msg(unsigned m) {
          return m == 110 || m == 185 || m == 187 || m == 317 || m == 802;
      }

- **Scénario d'échec** : un joueur dont les weaponskills émettent 110, 187 ou 802 n'obtient **plus aucun
  popup**, sur une fonctionnalité annoncée. Le message de suppression part dans `aiohud_debug.log`, un
  fichier qu'il faut lui demander ; rien n'apparaît en jeu ni dans `//aio doctor`.
- **Pourquoi c'est plausible** : le commit dit lui-même « never been observed on a weaponskill **here** ».
  110/187/802 sont dans la table parce que la boîte Skillchains les accepte comme messages d'ouverture ;
  or `sc_is_finish_msg` est appelé en `party_state.cpp:815` pour **n'importe quel acteur**, donc ces trois
  valeurs pourraient tout aussi bien n'être émises que par des mobs ou d'autres joueurs. **Je ne peux pas
  trancher depuis les sources.** Le projet documente par ailleurs (CLAUDE.md, mémoire testeur NA) que la
  variance entre installs est une classe de bug récurrente ici.
- **Réfutation tentée** : j'ai cherché une capture ou une table qui associerait chaque message à un type
  d'acteur — `docs/game-data/actions/` n'en contient pas, et les seules valeurs *mesurées* citées dans le
  code sont 185 (Savage Blade, Ruthless Stroke) et 317 (Jump). J'ai aussi vérifié que le raisonnement écrit
  dans le commit était traité : il l'est, et il est bon — « échouer fermé » est le bon arbitrage. Ce n'est
  donc **pas** le choix que je conteste, c'est **la boucle de retour**, dont la dernière étape dépend d'un
  utilisateur qui remarque une absence et envoie un log.
- **Confiance** : PLAUSIBLE
- **Correction suggérée** : router la ligne `WSPOP suppressed` vers `//aio doctor` (déjà console, déjà
  user-facing) en plus du log — la boucle se referme sans capture à demander.

### [S2] Le plugin écrit dans `scripts\init.txt` de Windower et ne l'enlève jamais

- **Où** : `src/plugin/aiohud.cpp:102-147` (`ensure_addon_autoload`), appelé sans condition depuis
  `aio_plugin_init:173`
- **Scénario d'échec** : l'utilisateur supprime AioHud (ou l'essaie puis le retire). La ligne
  `lua load aioupdate` reste dans `init.txt`, un fichier qui **appartient à Windower**, et Windower tente de
  charger un addon absent à chaque lancement, indéfiniment. `aio_plugin_unload` (`:563`) ne retire que la
  sous-classe WndProc et appelle `dispose()` — rien ne défait l'écriture.
- **Pourquoi c'est réel** : l'écriture est inconditionnelle à l'init, et il n'existe aucun autre site qui
  touche `init.txt` (`git grep init.txt -- src/` ne renvoie que ce bloc). Le symptôme se manifeste chez
  quelqu'un qui n'a **plus** le plugin, donc ne peut pas le diagnostiquer.
- **Réfutation tentée** : j'ai vérifié que le désinstalleur ne vivait pas ailleurs — ni `package.bat`, ni
  `assets/aioupdate.ps1`, ni `updater/aioupdate` ne retirent la ligne. J'ai aussi vérifié que l'écriture
  elle-même était sûre : elle l'est (mutex nommé inter-processus, temp + `MoveFileExA`, garde `full` sur les
  fichiers plus gros que le tampon) — le défaut porte uniquement sur l'absence de chemin inverse.
- **Confiance** : CONFIRMÉ
- **Correction suggérée** : soit rendre l'addon auto-nettoyant (il détecte l'absence du plugin et se retire),
  soit documenter la ligne dans le README d'installation — la seconde option coûte cinq minutes.

### [S2] L'updater ne vérifie aucune intégrité avant d'extraire sur la racine Windower

- **Où** : `assets/aioupdate.ps1:52-64`
- **Code** :

      Invoke-WebRequest $a.browser_download_url -OutFile $zip -Headers $ua
      ...
      $root = Split-Path $Plugins -Parent
      Expand-Archive -LiteralPath $zip -DestinationPath $root -Force

- **Scénario d'échec** : le zip est extrait `-Force` sur la **racine de Windower** (parent de `plugins\`)
  sans somme de contrôle ni signature. Le seul ancrage de confiance est HTTPS + la sécurité du compte GitHub.
  Un asset de release remplacé, ou un compte compromis, livre une DLL native arbitraire chargée dans le
  processus du jeu — au privilège du joueur, sans invite.
- **Pourquoi c'est réel** : le chemin est celui du bouton « Mettre à jour » de l'onglet Update, et il est
  aussi déclenché automatiquement par l'addon compagnon. `Expand-Archive` sur un répertoire parent expose en
  outre au *zip-slip* si une entrée contient `..\`.
- **Réfutation tentée** : j'ai vérifié que le zip ne venait pas d'une source déjà vérifiée par ailleurs — il
  vient de `browser_download_url` d'un asset GitHub, sans hash publié ni comparé. J'ai vérifié TLS : la ligne
  38 force bien TLS 1.2 (dans un `try/catch` muet, mais l'échec laisserait le défaut par défaut de la
  machine, pas une dégradation active). Rien ne réfute le constat. **Je qualifie explicitement le risque :
  il est faible en pratique** (dépôt personnel, pas de cible de valeur), mais le coût de la parade est d'une
  dizaine de lignes.
- **Confiance** : CONFIRMÉ (sur l'absence de vérification ; le risque d'exploitation est un jugement, pas
  une mesure)
- **Correction suggérée** : publier le SHA-256 du zip dans le corps de la release (la CI l'a déjà sous la
  main) et le comparer avant `Expand-Archive`.

### [S2] `ease()` abandonne silencieusement au-delà de 1024 ressorts

- **Où** : `src/ui/config_controls.cpp:115-121`
- **Code** :

      float ease(int id, int sub, float target, float speed) {
          Anim* s = nullptr;
          for (int i = 0; i < g_animN; ++i) if (g_anim[i].id == id && g_anim[i].sub == sub) { s = &g_anim[i]; break; }
          if (!s) { if (g_animN >= ANIM_MAX) return target; s = &g_anim[g_animN++]; ... }

- **Scénario d'échec** : à saturation (`ANIM_MAX = 1024`), tout **nouveau** contrôle reçoit sa valeur cible
  sans interpolation — il apparaît en dur, sans transition — et **rien ne le dit** : pas de log, pas de
  compteur, pas de remontée dans `//aio doctor`. Le symptôme (« certains contrôles ne s'animent plus, les
  autres si ») est exactement celui qu'on ne rattache pas à une capacité pleine.
- **Pourquoi c'est réel** : c'est le corollaire explicite de la règle 10 dans CLAUDE.md — « quand un budget
  ou une fenêtre expire, DIS-LE ». Les autres budgets du dépôt le respectent : `buff_atlas.cpp:37-41`
  journalise son abandon une fois, `minimap.cpp:499` journalise son plateau. Celui-ci est le seul muet.
- **Réfutation tentée** : j'ai cherché à démontrer l'atteignabilité de 1024 ressorts — je n'y arrive pas.
  Les 26 sites `ease()` sont majoritairement dans des boucles bornées par le nombre de lignes visibles, et
  les entrées ne sont jamais recyclées, donc le compteur croît avec le nombre de contrôles **distincts**
  visités dans la session, pas avec le temps. C'est plausiblement hors d'atteinte aujourd'hui. Le constat
  porte donc sur le **silence**, pas sur la saturation : un budget muet est un défaut même quand il n'est
  pas atteint, puisque c'est précisément ce qui rend son atteinte indiagnosticable.
- **Confiance** : CONFIRMÉ (sur le silence) / la saturation est NON RÉFUTÉE dans les deux sens
- **Correction suggérée** : une ligne `debug::log` une seule fois à la saturation, et exposer `g_animN` dans
  `//aio selfcheck`.

### [S3] Trois tampons `_snprintf` cumulatifs ne sont pas forcés à NUL, contrairement à leurs voisins

- **Où** : `src/model/party_state.cpp:1146`, `src/model/party_state.cpp:1583`, `src/ui/hud_timers.cpp:887`
- **Code** (représentatif, `party_state.cpp:1146`) :

      char gl[220]; int go = 0; gl[0] = 0;
      for (int gi2 = 0; gi2 < 16 && go < 200; ++gi2) go += _snprintf(gl + go, sizeof(gl) - 1 - go, "%u ", eids[gi2]);
      // ... gl passé à debug::log("%s") sans gl[sizeof(gl)-1] = 0;

- **Le changement futur qui se plantera** : `_snprintf` de MSVC renvoie `-1` **et ne termine pas** en cas de
  troncature — le dépôt le documente lui-même en `aiohud.cpp:815-818`. Aujourd'hui ces trois sites sont
  hors d'atteinte : la garde laisse ≥ 10 octets pour un `"%u "` qui en écrit au plus 6. Le jour où le format
  passe de `%u` à quelque chose de plus large (un nom, un `%08X`, un champ ajouté), la garde ne suffit plus,
  `go` **recule** de 1, et le tampon part non terminé dans `debug::log("%s")`. Les sites voisins
  (`party_state.cpp:938`, `:1642`) forcent la terminaison ; le commentaire de `aiohud.cpp:819` affirme même
  que « every sibling call site here already force-terminates » — ce n'est pas vrai de ces trois-là.
- **Réfutation tentée** : j'ai calculé la marge de chacun des trois (10, 10 et 10 octets pour 6 écrits) et
  confirmé qu'aucun n'est atteignable en l'état. C'est bien pour cela que c'est un S3 et pas un S1.
- **Confiance** : CONFIRMÉ (sur l'incohérence) — le défaut est latent par construction, pas actif
- **Correction suggérée** : `gl[sizeof(gl) - 1] = 0;` après chaque boucle, comme les voisins.

---

## 3. Mesures

Deux chiffres seulement, parce que chacun **est** le problème plutôt que de le décrire.

**42 warnings à la compilation, `/WX` absent.** Build complet effectué pendant cet audit
(`build.bat`, sortie propre, DLL produite) :

| Code | Nb | Nature |
|---|---|---|
| C4189 / C4101 | 30 | locales inutilisées — 12 dans `hud_zonetracker.cpp`, 12 dans `ui_config.cpp` |
| C4457 | 6 | masque un paramètre — **toutes dans `aiohud_probes.cpp`**, non suivi, hors release |
| C4244 | 3 | conversion avec perte — **c'est le constat §2.2** |
| C4458 | 2 | masque un membre de classe (`party_state.cpp:421`, `party_state_zonetracker.cpp:482`) |
| C4505 | 1 | fonction interne non référencée (`layout.cpp:36`) |

`build.bat:38-41` affirme garder C4457 et C4244 actifs *parce qu'ils mordent*. Trois C4244 sont livrés, et
l'un d'eux est un vrai défaut. Le chiffre compte ici parce qu'il démontre qu'un signal exact, gratuit et
déjà activé n'est pas consommé.

**La suite offline lie 1 des 20 `.cpp` de `model/`.** `tests.bat` compile `skillchain.cpp` et rien
d'autre ; `t_json` et `t_durations` testent des en-têtes. Le test précis qui manque, et le bug qu'il aurait
attrapé : **un aller-retour `save_config_to` → `load_config_from` → `persist_eq`**, ~40 lignes, qui aurait
attrapé la collision `mm3=` documentée en `ui_config.cpp:606` (« renamed from mm3= : it collided with the
clock's mm3= and never loaded ») — une option de config silencieusement jamais rechargée. J'ai vérifié
mécaniquement que la couverture est **complète aujourd'hui** (109 clés écrites / 104 relues en ligne + 5 via
les helpers hors-ligne ; `persist_eq` couvre les 294 champs de `UiConfig` sauf 12 champs runtime
intentionnels). Rien ne la maintient : la prochaine clé ajoutée peut redevenir muette.

---

## 4. Statut vérifié des items encore ouverts de l'audit du matin

Vérifié dans le code, pas supposé.

| Item (audit-full-2026-07-26) | Statut |
|---|---|
| S0 `mmZoom` — `sprintf` court + sanitisation | **CORRIGÉ** (impression bornée + `CLF(c.mmZoom, 1, 24)` en `ui_config.cpp:686`) |
| `helpCursorTex_` — handle emprunté non oublié | **CORRIGÉ** (`config_page.h:35`) |
| Polices fournies — budget de 8 appels sans espacement | **CORRIGÉ** (latch sur succès, `font.cpp:207-215`) |
| `mmTried_` — dernier latch one-shot | **OUVERT** — et §2.1 explique pourquoi il a fallu le trouver à la main |
| `debug::clear()` à l'init efface la preuve | **OUVERT** (`aiohud.cpp:175`) |
| Garde-fous CI en `continue-on-error` | **OUVERT** — 14 hits dans l'arbre ; §2.1 ajoute qu'un des cinq ne voit pas ce qu'il devrait voir |
| `build.bat` relie un `.res` périmé (`if exist` au lieu du code retour) | **OUVERT** (`build.bat:32-35`) |
| Bloc de re-ancrage `hud.cpp:208-233` | **OUVERT** |
| `CLAUDE.md` faux sur les sondes livrées | **OUVERT** — quatrième signalement |

---

## 5. Vérifié et SAIN

À ne pas re-scanner à la prochaine passe sans élément nouveau.

- **Aller-retour de la config.** Diff mécanique clés écrites / clés relues : aucune orpheline. Les 14
  apparentes (`db`, `ep`, `eptrack`, `mm5`, `distcol`, `tgtRangeMin`, `zt*`, `*CastDemo`) sont toutes prises
  par les cinq helpers hors-ligne — vérifié une par une.
- **`persist_eq`.** Diff mécanique des 294 champs de `UiConfig` : les 12 non comparés sont tous des états
  runtime légitimes (`editLayout`, `wheel`, `mmWheel`, `mmVisible`, `mmPreview`, `mmHit*`, `favColors*`).
  Pas de champ persisté oublié.
- **Bornes des tables indexées par identifiant de statut.** `buffCaster_`, `selfBuffSpell_`, `rollVal_`,
  `rollLuck_`, `songMod_` sont tous `[1024]` et **tous** les sites d'écriture testent `< 1024` — y compris
  les chemins alimentés par paquet (`party_state.cpp:992`) et par fichier de cache (`:608-610`).
- **`buff_atlas.cpp`, hypothèse « propriétaire unique ».** Un seul `release_texture` dans tout le programme ;
  les consommateurs ne font que lire ; `forget` réarme le budget et le HUD l'appelle à chaque recréation de
  device. Le seul consommateur qui *stocke* le handle (`config_page.cpp:tgtBuffTex_`) le remet bien à zéro
  dans `on_device_lost` et le re-demande à chaque frame (`:1467`, `:1478`, `:1487`). Pas de handle mort
  trouvé.
- **`io/json.h`.** La borne de profondeur (`MAX_DEPTH 64`) et le clamp de `num_str` (±1e15 avant formatage)
  tiennent ; `strtod` s'appuie sur la terminaison de `c_str()`. Reste un écart de conformité sans
  conséquence ici : les paires de substituts `\uD800-\uDFFF` produisent de l'UTF-8 invalide.
- **Couverture SEH des balayages du tableau d'entités.** Les trois copies de bloc 0x900
  (`game_mem.cpp:127`, `:169`, `:198`) sont gardées et retournent 0 en cas de faute ; chaque pointeur est
  revalidé par `valid_ptr` avant usage.
- **Discipline `on_device_lost`.** Les 12 implémentations oublient (mise à 0) sans `Release` ; le bloc
  `hud.cpp:185-203` couvre aussi les copies paresseuses hors widgets (échantillons Help, skins de boîte,
  matériaux procéduraux).
- **Files inter-threads.** `g_cmdQ` (`aiohud.cpp:1028-1051`) et la file de texte sont des anneaux SPSC à
  capacité fixe, publication de l'index en dernier, sans allocation. Le choix de nourrir les paquets en
  ligne est argumenté par des mesures, avec la contre-mesure notée si on le revisite.
- **Invariants de taille de tableau.** `RELEASES_N` ≤ `relOpen_[128]` est désormais tenu par un
  `static_assert` (`config_page.cpp:1652`) — le piège documenté dans CLAUDE.md ne peut plus se reproduire
  en silence. Idem `hov_`, `dbgOpen_`, `trkCatOpen_`, `BuffSet::ids`.

---

## 6bis. Statut au 2026-07-27 — ce qui a été corrigé depuis la rédaction

Tout ce qui suit est compilé (`build.bat` OK, 39 warnings contre 42) et vert (`tests.bat` : 88 checks, 0 échec).

| Constat | Statut |
|---|---|
| §2.1 garde-fou aveugle au suffixe `_` | **CORRIGÉ**. `_?` + détection de la lecture du latch + filtrage des lignes de COMMENTAIRE + marqueur `rule10-ok: <raison>` pour les cas légitimes (5 sites annotés). Zéro hit sur l'arbre ; prouvé dans l'autre sens avec un faux latch témoin, détecté. La règle 10 est maintenant une étape CI **bloquante** distincte ; les quatre autres restent `continue-on-error` jusqu'à ce que chacune atteigne zéro. |
| §2.2 `bw` tronqué en `u32` | **CORRIGÉ** (`const float`). Les 3 C4244 du projet ont disparu. |
| §2.3 whitelist WS-popup mesurée sur un perso | **BOUCLE REFERMÉE** — pas la whitelist elle-même (l'arbitrage « échouer fermé » est bon), mais sa remontée : `PartyState::ws_suppressed()` expose le compte + le dernier `(id, message)`, et `//aio doctor` l'imprime en console avec la ligne exacte à renvoyer. |
| §2.4 `init.txt` sans chemin inverse | **CORRIGÉ** côté addon : `aioupdate.lua` se retire de `init.txt` (réécriture temp + rename, comme le writer du plugin) quand il constate que `plugins\AioHud.dll` a disparu. Ne fait rien si le plugin est là, rien si le fichier est illisible. |
| §2.5 updater sans vérification d'intégrité | **CORRIGÉ**. La CI publie un `AioHud-<ver>.zip.sha256` à côté du zip ; le script compare avant `Expand-Archive` et **échoue fermé**, y compris si le sidecar est absent — les deux messages nomment le remède. |
| §2.6 `ease()` muet à saturation | **CORRIGÉ** (une ligne de log, une fois). |
| §2.7 trois tampons non terminés | **CORRIGÉ** (les trois forcent le NUL). |
| §3 pas de test d'aller-retour de la config | **CORRIGÉ** — `tests/t_config.cpp`, 13 checks. Il a fallu exposer `persist_eq` (`ui_config_persist_eq`), parce que `profile_dirty()` ne peut PAS servir d'oracle : `profile_load` appelle `profile_mark_clean()` **après** lecture, donc la comparaison est vide de sens par construction. Le test est un **point fixe**, pas un « écris / relis / compare » naïf : les flottants sont écrits à 3-5 décimales et comparés à l'identique, si bien que la forme naïve échoue sur une config saine. Validé par sabotage : neutraliser la lecture d'une seule clé (`buffMax`) fait tomber 2 assertions. |

Et les quatre items encore ouverts de l'audit du matin (§4), traités dans la foulée :

| Item | Statut |
|---|---|
| `debug::clear()` à l'init efface la preuve | **CORRIGÉ**. Remplacé par `debug::begin_session(version)` : plus de troncature, une bannière **version + horodatage + pid** en tête de session, et une rotation vers `aiohud_debug.prev.log` au-delà de ~4 Mo. Règle au passage le constat jumeau « le log n'identifie ni la build ni l'heure », qui rendait une capture non attribuable alors que l'updater remplace silencieusement la build d'un testeur. |
| `build.bat` relie un `.res` périmé | **CORRIGÉ**. `del` du `.res` d'abord, puis le **code retour** de `rc` (plus `if exist`), avec un avertissement explicite. Prouvé par sabotage : `.rc` cassé après un build 1.0.71 → l'ancien comportement aurait estampillé 1.0.71 sur un build 1.0.99 ; désormais l'avertissement s'affiche et la DLL ne porte aucune version. |
| Bloc de re-ancrage `hud.cpp` (perf) | **CORRIGÉ**. La correction géométrique reste immédiate (la boîte suit le slider) ; ses deux conséquences lourdes sont **débouncées** — `save_ui_config()` (réécriture atomique complète) à 500 ms, `place_widgets()` (destruction/reconstruction de tous les widgets + rechargement des assets + re-décodage du DAT de carte) à 150 ms, l'échéance étant re-tamponnée à chaque changement. `Hud::dispose()` vide la sauvegarde en attente, sinon un redimensionnement suivi d'un `//unload` dans la fenêtre perdait le décalage — c'est-à-dire la boucle d'itération standard. |
| `CLAUDE.md` faux sur les sondes livrées | **CORRIGÉ** (4ᵉ signalement). L'affirmation « aucune sonde `//aio` n'est dans une release sauf geartrace » était fausse : `aiohud.cpp` est suivi et livre **dix** commandes de diagnostic — `doctor` `selfcheck` `dbflog` `tpool` `tmem` `ftrace` `oblog` `songdur` `geartrace` `keylog`. La phrase dit maintenant ce qui est vrai, et pointe `//aio doctor` en premier. |

Trouvé en passant : le repli `#ifndef AIOHUD_VERSION` de `aiohud.cpp` se trouvait ~400 lignes **sous** son premier usage, donc inopérant. Remonté en tête de fichier.

---

## 6. Ordre proposé

1. **§2.1 — corriger la regex du garde-fou** (`_?`). Une minute, et c'est ce qui empêche le prochain
   `mmTried_` de coûter un audit entier. Puis nettoyer les 3 sites qu'elle révèle.
2. **Rendre les garde-fous bloquants** une fois l'arbre propre. Un garde-fou qui ne bloque jamais entraîne
   à ignorer sa sortie — et il vient de manquer un vrai hit, ce qui est le pire des deux mondes.
3. **§2.2 — `const float bw`.** Une ligne, supprime le seul C4244 qui cache un défaut réel. Puis viser
   zéro warning et poser `/WX`.
4. **§2.3 — router `WSPOP suppressed` vers `//aio doctor`.** Referme la boucle de retour du changement le
   plus récent sans avoir à trancher la question des messages.
5. **§2.5 — SHA-256 dans l'updater.** Dix lignes, et c'est le seul chemin du projet qui exécute du code
   téléchargé.
6. **§2.6 et §2.7** — deux lignes chacun, à faire en passant.
7. **§3 — le test d'aller-retour de la config.** ~40 lignes dans la suite offline existante ; c'est le
   meilleur rapport couverture/coût qui reste sur la table.
