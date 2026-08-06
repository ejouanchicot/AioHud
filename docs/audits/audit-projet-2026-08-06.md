---
title: Audit projet complet — 2026-08-06 (8 axes)
summary: Audit multi-agents conduit selon docs/audits/audit-protocol.md, onze jours et 31 commits après la passe du 2026-07-26. Couvre pour la première fois les deux angles morts déclarés (toute la couche gfx/, hud_timers.cpp) et mesure le build, les tests et les 24 générateurs en les exécutant. Deux S1, dix-neuf S2, vingt-cinq S3, plus le statut vérifié de l'historique des audits (56 corrigés / 102 ouverts / 11 classés).
---

# Audit projet complet — 2026-08-06

Conduit selon `docs/audits/audit-protocol.md` : standard de preuve à quatre éléments, règle zéro
(attaquer le code récent en premier), section « Vérifié et SAIN » obligatoire.

**Base de départ** : `9c95343` (v1.0.74), 31 commits après `7cfa99d`, le commit du dernier audit.
Les rapports antérieurs ont été lus avant de commencer ; leurs constats ne sont pas re-instruits, leur
**statut vérifié** est en §5.

---

## 1. Couverture

Huit axes, conduits en parallèle. Les deux angles morts que la passe du 26/07 déclarait explicitement
sont couverts ici pour la première fois.

| Axe | Périmètre | Méthode |
|---|---|---|
| A — Code récent | Les 31 commits depuis `7cfa99d` : zonetracker, Limbus, Omen, debuffs, skillchains | Lecture intégrale des fichiers neufs + diff |
| B — Updater / release | `aioupdate.ps1`, `aioupdate.lua`, `verify_release.ps1`, `build.yml`, les `.bat` | Lecture + parsing exécuté + harnais de comparaison de versions |
| C — `src/gfx/` **(angle mort n°1)** | **100 % de la couche** — 12 fichiers, 2641 lignes | Lecture intégrale + build + recalcul analytique des bornes |
| D — Timers / buffs alliés **(angle mort n°2)** | `hud_timers.cpp` intégral (1211 l.) + le pipeline d'estimation | Lecture intégrale + traçage des chemins d'appel |
| E — Mémoire / SEH / paquets | `game_mem.cpp` intégral, `aiohud.cpp` intégral, tous les handlers de paquets | Lecture intégrale + inventaire exhaustif des déréférencements |
| F — Config / persistance | `ui_config.cpp`, `config_page.cpp`, `config_controls.cpp`, les 15 `*_config.cpp` | Diff **mécanique** outillé des 302 champs persistables |
| G — Historique | Les 9 rapports d'audit précédents | Chaque item rouvert et **vérifié dans le code actuel** |
| H — Build / tests / outillage | Build, tests, 24 générateurs, règle de couches, CI | **Exécuté**, pas lu |

**Non couvert — angles morts de cette passe, à traiter comme tels :**

- **Aucune observation en jeu.** Aucun constat visuel n'a été vérifié à l'écran ; les constats de rendu
  (feathering, biais de mip, netteté de police) sont des raisonnements sur le code et la spécification D3D8.
- `resistances_gen.h` reste **invérifiable** : l'addon source `sheolhelper` n'est pas installé sur la machine.
- `config_help_data.h` et le rendu de l'onglet Help : survolés seulement.
- Reproductibilité binaire du build : non testée.
- Aucune exécution réelle de l'updater contre GitHub (aucune release de test n'a été créée), et aucun essai
  sur une install NA sous Program Files — les conclusions de ce profil reposent sur la lecture.

---

## 2. Constats nouveaux — S1

### [S1-1] Le cache de buffs alliés `buffs_[18]` n'est jamais libéré : slots périmés, puis saturés

- **Où** : `src/model/party_state.cpp:1639-1645` (unique site d'écriture), lu en `:1673` ; déclaration `party_state.h:674`
- **Code** :

      int slot = -1, free = -1;
      for (int s = 0; s < 18; ++s) {
          if (buffs_[s].id == mid) { slot = s; break; }
          if (!buffs_[s].id && free < 0) free = s;
      }
      if (slot < 0) slot = (free >= 0) ? free : 0;

- **Vérifié à la main** : `grep "buffs_\["` sur tout `src/model/` ne rend que `on_076` (écriture) et
  `buffs_for` (lecture). **Aucun site ne remet un `id` à 0** — ni la sortie de groupe, ni
  `on_character_changed` (qui le déclare explicitement, `party_state.h:728`).
- **Scénario d'échec, obsolescence** : un membre de ton groupe passe en party 2 de l'alliance. Le 0x076 ne
  couvre que les 5 slots de ton groupe : son entrée **gèle définitivement** avec le contenu d'il y a dix
  minutes. Tu lui lances Haste → `hud_timers.cpp:684` fait `if (known && !has) continue;` — le slot gelé
  existe donc `known` est vrai, le statut n'y est plus, **la ligne n'est jamais dessinée**, sans le délai
  de grâce de 3 s dont dispose le prune. Symétriquement, si le set gelé contient encore le statut,
  l'alerte OUT est structurellement supprimée pour ce membre.
- **Scénario d'échec, saturation** : les ids de trust changent à chaque ré-invocation — c'est écrit dans ce
  dépôt (`party_state.h:606-609`), où le bug **jumeau** a été corrigé pour `jobShadow_[24]` par une éviction
  du plus ancien. 5 trusts × 4 ré-invocations = 20 ids distincts > 18. Ensuite `slot = 0` pour tous : un
  même 0x076 porte 5 membres qui s'écrasent l'un l'autre → un seul membre caché à la fois → plus aucune
  alerte OUT alliée de la session.
- **Pourquoi c'est réel** : cinq consommateurs traitent `bs != 0` comme une autorité (`hud_timers.cpp:674`,
  `:718`, `party_state.cpp:1049`, `:1075`, `:1762`). C'est la règle 10 forme 2 appliquée à un troisième état
  jamais modélisé : **périmé**, distinct de « vide » et de « inconnu ».
- **Réfutation tentée** : « 18 ≥ 17 alliés, donc ça suffit » — 18 suffit pour les membres *simultanés*, pas
  pour les ids *distincts* d'une session, et l'invariant n'est écrit nulle part. « `on_character_changed`
  doit le vider » — le code déclare le contraire, avec justification ; cet argument tient pour le contenu
  d'un membre présent, pas pour un slot occupé par un membre parti.
- **Confiance** : CONFIRMÉ (mécanisme et chemins d'appel relus ligne à ligne ; aucune mesure en jeu)
- **Correction** : recopier le correctif `jobShadow_` — un horodatage par slot, éviction du plus ancien
  quand plein — **et** invalider le slot d'un membre que `party_order()` ne résout plus.

### [S1-2] Dual-box : « Update now » lance deux updaters concurrents qui partagent `done.txt` et `AioHud.dll.bak`

- **Où** : `src/plugin/aiohud.cpp:734-737` + `updater/aioupdate/aioupdate.lua:179-181` + `assets/aioupdate.ps1:141-143`
- **Le mécanisme** : le plugin écrit `request.txt` dans `plugins\AioHud\data\update\` — un dossier **partagé
  par les deux clients** — puis spawn son PowerShell. L'addon du second client poll ce fichier chaque seconde,
  le voit, et envoie `aio update` : le second plugin n'ayant jamais spawné, son garde `s_lastFullSpawn` est à
  zéro et il **spawn un second updater**. Le garde de dé-duplication (`aiohud.cpp:676`) est un `static` de
  processus : il ne protège que le client d'origine.
- **Ce qui est partagé** : `update_$PID.zip` et `stage_$PID` sont bien par PID — mais `done.txt`,
  `AioHud.dll.bak` et l'arbre de destination ne le sont pas. Les deux updaters attendent le même
  déverrouillage de DLL, repartent donc **en même temps**, et robocopient les mêmes 1390 fichiers vers la
  même racine. Le perdant obtient des violations de partage → `$LASTEXITCODE >= 8` → il écrit
  `ERROR asset install failed (robocopy 8)` **par-dessus** le `OK <ver>` que l'autre venait d'écrire.
  L'utilisateur lit en chat que sa mise à jour a échoué alors qu'elle a réussi.
- **Conséquence plus grave, PLAUSIBLE et non prouvée** : `$bak` étant partagé, le `Remove-Item $bak` de A
  peut effacer la sauvegarde de B entre sa copie et sa restauration ; si B a pris son `$bak` avant le swap
  de A, un échec côté B restaure l'**ancienne** DLL par-dessus la neuve — assets neufs, DLL ancienne.
- **Réfutation tentée** : recherche d'un mutex nommé dans le `.ps1` (comme celui d'`ensure_addon_autoload`) —
  il n'y en a aucun. « Un seul addon gagnera la course sur `os.remove` » — les deux pollers sont sur des
  `coroutine.schedule` indépendants ; c'est un tirage, pas une garantie.
- **Confiance** : CONFIRMÉ (double spawn + `ERROR` écrasant `OK`) / PLAUSIBLE (restauration de l'ancienne DLL)
- **Correction** : un mutex nommé `Global\AioHudUpdater` en tête du `.ps1`, un `$bak` par PID, et suppression
  du relais `request.txt` côté addon maintenant que le plugin spawn directement.

---

## 3. Constats nouveaux — S2

### Zonetracker / Limbus / Omen (le code le plus récent)

| # | Constat | Où | Conf. |
|---|---|---|---|
| S2-1 | **`lc_load()` n'est jamais appelé après un rechargement du plugin.** Son unique appelant est la branche `prevMode != 6` ; or au premier poll après `//load`, `zt_load(zone)` réussit et **`return`** avant de l'atteindre. Résultat en Limbus : « ? runs left » et pastilles vides pour toute la visite, alors que le fichier contient les bonnes valeurs. C'est une **régression** : le HUD lisait `zt.limbusWeekLeft`, qui survivait au rechargement | `party_state_zonetracker.cpp:385-389` et `:432` | CONFIRMÉ |
| S2-2 | **Le bump de `LC_VER` invalide tous les `limbus_*.bin` existants.** `LC_VER` encode `sizeof(LimbusCoffers)*2 + sizeof(LimbusWeek)` ; `LimbusWeek` étant appendé **en queue**, la lecture tolérante écrite juste en dessous (« *a file written by the previous layout stops here* ») gérerait le cas — mais le contrôle de version s'exécute avant et la rend inatteignable. Chaque joueur perd son historique de coffres une fois, en silence | `party_state_zonetracker.cpp:52` et `:108-115` | CONFIRMÉ |
| S2-3 | **`omen_floor_has_bonus()` traite « je ne sais pas encore » comme « pas de bonus ».** À l'entrée, `floorObj` vaut le placeholder `"Waiting for objectives..."`, qui contient `"Waiting"` → `bonusFloor = false`. Les objectifs arrivent, sont **correctement parsés**, et ne sont **pas dessinés** : exactement le symptôme que la fonctionnalité voulait corriger. `nrows > 0` est une preuve positive disponible sur place, et ignorée | `omen_objectives.h:155` + `hud_zonetracker.cpp:91-95` | PLAUSIBLE (masquage confirmé, durée non prouvée) |
| S2-4 | **Deux orthographes incompatibles du même bandeau dans le même fichier** : `"treasure portent"` (minuscule) dans le prédicat de bandeau, `"Treasure"` (majuscule) dans la liste NOBONUS. `strstr` étant sensible à la casse, une seule des deux peut correspondre — et selon laquelle, soit la boîte annonce une fenêtre bonus sur un sol coffre, soit `floorObj` garde le bandeau du sol précédent | `omen_objectives.h:139-141` vs `:157-158` | CONFIRMÉ (incohérence interne) |
| S2-5 | **`omen_is_floor_banner_line()` avale la ligne d'objectif `"3: Vanquish 2 foes."`** — elle contient `"Vanquish"`, donc quand le latch `floorCleared` est armé, les 10 slots sont remis à zéro et la ligne n'est jamais appliquée. Le garde qui existe (`s[0] >= '1'`) arrive **après** l'appel | `omen_objectives.h:180-186` | PLAUSIBLE |

### Timers / buffs alliés

| # | Constat | Où | Conf. |
|---|---|---|---|
| S2-6 | **Les entrées FOCUS visant un membre d'alliance sont immortelles.** Le 0x076 ne couvre pas l'alliance → `listReady` est faux en permanence → la ligne 904 remet `lostMs = 0` chaque frame → l'unique bloc de purge, qui exige `lostMs != 0`, ne s'exécute **jamais**. Un RDM qui Haste+Refresh 12 alliés remplit les 24 slots ; ensuite la ligne 757 refuse toute nouvelle entrée **en silence** — et **tes propres** alertes OUT s'arrêtent | `hud_timers.cpp:757`, `:834-869`, `:904` | CONFIRMÉ (immortalité) / PLAUSIBLE (atteinte du plafond) |
| S2-7 | **Un échec de `read_equipment_ext` au moment du cast fige l'estimation « base seule » pour toute la vie du buff**, sans `else` et sans log. Un Protect III allié lancé dans la fenêtre semi-prête d'après-zone est estimé à 1800 s au lieu des ~2952 s mesurés et cités douze lignes plus haut : la ligne disparaît **19 minutes trop tôt**. `durMs` est écrit une fois, jamais recalculé. Le lecteur voisin, lui, rapporte son échec (`SONGMOD UNREADABLE`) | `party_state.cpp:1116-1118` | PLAUSIBLE |
| S2-8 | **Les alertes OUT alliées s'affichent encore quand « Mes buffs sur alliés » est décoché.** La création d'entrées est gardée par `C.tmMine`, ni la purge ni l'émission ne le sont — une ligne rouge clignotante reste en tête de colonne jusqu'au re-cast, et les trois suppresseurs de swap délibéré bouclent sur zéro élément | `hud_timers.cpp:300` vs `:700` | CONFIRMÉ (mécanique) |

### Couche gfx — premier passage de son histoire

| # | Constat | Où | Conf. |
|---|---|---|---|
| S2-9 | **Le pré-bake des atlas de police WS dessine HORS du state block.** `pf->begin(dev)` pose 11 render-states + 13 texture-stage-states et `pf->draw()` émet de vrais `DrawPrimitiveUP` — **avant** `dCreateSB` ligne 316. Double conséquence : le jeu perd son état en fin d'EndScene, et le state block capture ensuite l'état **déjà corrompu**, donc le `dApplySB` final restaure l'état de la police au lieu de celui du jeu. `dCreateSB` n'apparaît qu'une fois dans tout le projet : ce bloc est le **seul** dessin non protégé. Vérifié à la main : l'ordre des lignes est bien celui-là | `src/ui/hud.cpp:304-316` | CONFIRMÉ (violation du contrat ; ampleur visuelle non mesurée) |
| S2-10 | **`Font` plafonne à 16 tailles par (face, graisse) sans aucune éviction.** Au-delà, `pick()` réutilise **silencieusement l'atlas le plus proche** — exactement ce que la conception promet d'éviter. Un décompte statique donne ≥ 13 tailles distinctes rien que dans la page de config, qui partage l'objet `Font` du HUD : le pool est dépassé dès qu'on ouvre `//aio` avec le HUD affiché. Reproductible à la main : balayer un slider « Size » de bout en bout occupe le pool avec les valeurs **transitoires**, et la taille finale reste floue. Le jeu de tailles gagnantes change à chaque zone-in (device lost) | `gfx/font.cpp:155-167`, `font.h:43` | CONFIRMÉ |
| S2-11 | **`Font::begin` pose `MIPMAPLODBIAS = 0.20` et personne ne le remet à 0.** C'est un texture-stage state persistant, et `grep` ne rend que cette ligne. `dTexQuadState` pose cinq états de sampler mais pas celui-là : dès qu'un texte a été dessiné plus tôt dans la frame, la minimap et les icônes de party sont échantillonnées avec un LOD biaisé. Deux widgets qui appellent la **même** fonction d'état rendent donc différemment selon l'ordre de dessin | `gfx/font.cpp:269` | CONFIRMÉ |

### Config / persistance

| # | Constat | Où | Conf. |
|---|---|---|---|
| S2-12 | **Supprimer le profil ACTIF ne le désactive pas.** `profile_delete` appelle `profile_refresh()`, qui **réécrit `g_profNames[]` en place** — or `name` pointe dedans (l'appelant passe `profile_name(i)`). Le `strcmp` compare donc l'ancien nom au **suivant de la liste** et échoue. `g_active` désigne toujours un profil supprimé, la barre affiche « Actif : Alpha », et un clic sur **Save changes** le **recrée**. Bug intermittent selon la position alphabétique — le dernier de la liste marche par accident. Vérifié à la main : le code et l'appelant sont exactement ceux-là | `ui_config.cpp:1098-1105`, `config_page.cpp:1283` | CONFIRMÉ |
| S2-13 | **L'échec d'écriture d'un profil est signalé par le modèle et jeté par l'UI aux 4 points d'appel.** `profile_save` a été retravaillé pour renvoyer `false` et logger ; les quatre appelants ignorent le retour, vident le champ de saisie et mettent `activeProf_` comme si ça avait marché. Profil de risque nommé : install NA sous Program Files, dossier non inscriptible | `config_page.cpp:747`, `:1214`, `:1218`, `:1264` | CONFIRMÉ (chemin) |

### Chaîne de publication

| # | Constat | Où | Conf. |
|---|---|---|---|
| S2-14 | **Rien dans la chaîne ne PARSE le `.ps1` ni le `.lua` qu'elle publie.** `tests.bat` est C++ seul, les garde-fous ne regardent que `src/`, `package.bat` est une robocopy, et le seul contrôle de `verify_release.ps1` sur le script livré est un `Contains` de chaîne. Une parenthèse déséquilibrée est publiée, et chez **100 %** des joueurs PowerShell échoue au parsing avant d'exécuter quoi que ce soit : aucun `done.txt`, et l'onglet Update affiche un remède qui n'a rien à voir. *(Les deux fichiers parsent correctement aujourd'hui — vérifié. Le constat porte sur le garde-fou absent.)* | `.github/workflows/build.yml:59-98` | CONFIRMÉ |
| S2-15 | **`verify_release.ps1` tourne APRÈS `gh release create --latest`.** Une release jugée non installable **reste en ligne et reste « latest »** ; le job passe rouge, mais tout client voit `AVAILABLE` entre-temps. Or 4 des 6 contrôles portent sur un artefact que la CI possède **localement** avant publication | `build.yml:150` puis `:161-166` | CONFIRMÉ |
| S2-16 | **`deploy.bat` ne déploie pas `aioupdate.lua`.** La boucle d'itération documentée laisse en place la version de la dernière *release* : le code de récupération ajouté par `119948a` n'a jamais tourné sur la machine du dev. Premier exécutant = l'utilisateur final, sur un chemin qui ne s'emprunte que quand une mise à jour a déjà échoué | `deploy.bat:22-30` vs `package.bat:48-50` | CONFIRMÉ (l'absence) |
| S2-17 | **Les installations en 1.0.71–1.0.73 exécuteront encore l'ANCIEN script à leur prochaine mise à jour.** Vérifié par `git show` sur les tags : elles portent toutes `Expand-Archive -Force`, dont le `finally` efface ce qu'il a extrait. Le script qui protège est celui **déjà en place**, pas celui qu'on télécharge — aucun code livré ne peut couvrir ce passage | `git show v1.0.73:assets/aioupdate.ps1` | CONFIRMÉ |

### Outillage

| # | Constat | Où | Conf. |
|---|---|---|---|
| S2-18 | **`mobskills_gen.h` ne correspond plus à sa source déclarée.** Régénéré en bac-à-sable et comparé id par id : **21 ids portent un nom différent**, 10 ids de l'en-tête n'existent pas dans le res, 10 du res sont absents. `3968` s'affiche « Bubble Cleanse » au lieu de *Kibosh*, `4032` « Death Ray » au lieu de *Tail Blow*. Le piège : les deux fichiers font exactement 3648 lignes (10 retirées, 10 ajoutées) | `src/model/mobskills_gen.h` | CONFIRMÉ (désynchronisation) |
| S2-19 | **`gen_trusts.py` effacerait les métiers des 106 trusts si on le relançait.** Sa regex cherche `TRUSTS[]` dans `party_state_roster.cpp` — la table a déménagé dans `trusts_gen.h`, sa propre sortie. Exécuté : `106 trusts (0 with a known job)`, sortie `{"Shantotto", 0, 0}`. `CLAUDE.md` dit de ne pas éditer à la main et de régénérer : **suivre l'instruction détruit la donnée** | `scripts/gen_trusts.py:23` | CONFIRMÉ (exécuté, sortie diffée) |

---

## 4. Constats S3 — le lot complet, en une table

| Constat | Où |
|---|---|
| Trois stockages du même compteur hebdo Limbus, dont deux morts mais dont les commentaires décrivent une sémantique active | `party_state_zonetracker.cpp:614-628` |
| Les tests neufs n'exercent aucun des deux chemins qui cassent : `g_cleared` n'est jamais armé via `feed()`, et `t_limbus` teste une lambda qui recopie la vraie fonction | `tests/t_omen.cpp:23`, `t_limbus.cpp:57` |
| 4 handlers de paquets sans plancher de taille, alors que 12 sur 16 en ont un et que la convention est écrite dans le dépôt | `party_state.cpp:1917`, `:1962`, `party_state_hate.cpp:93` |
| `read_entities_by_id` confond « lecture impossible » et « aucun mob » ; `refresh_hate` ignore le retour et purge la hate list sur un simple raté | `game_mem.cpp:163`, `party_state_hate.cpp:49` |
| `read_party_aggro_mobs` : code mort (28 l. + 9 Ko de BSS), 3ᵉ copie du bloc d'entités — et la doc le présente toujours comme la source **PRIMAIRE** | `game_mem.cpp:192`, `docs/game-data/target/hate-list.md:31` |
| `timers_draw` appelle `read_usable_ja_bits` (deux appels indirects **dans le code client**) à 60 Hz, et `zone_id()` alors que `f.game->zone` porte déjà la valeur | `hud_timers.cpp:345`, `:704` |
| Un `zt_save()` (création + écriture de fichier) **par ligne de chat Omen**, et deux `lc_save()` consécutifs sur le même fichier pour un seul paquet — la leçon inverse est écrite dans `party_state_roster.cpp:120` | `party_state_zonetracker.cpp:203-224`, `:621-628` |
| Le thread TEXTE lit `zt_` directement, ligne ajoutée par `d97f23e`, six lignes au-dessus du commentaire qui l'interdit | `aiohud.cpp:422` |
| Lecture hors bornes de pile (≤ 4 octets) dans le test de nom de coffre Limbus : la boucle teste `c[1..5]` en n'exigeant que `*c` | `party_state_zonetracker.cpp:576-583` |
| `abil_name_by_recast` : scan linéaire de 626 lignes **par recast et par frame** (~15 000 tests/frame), alors que le jumeau a été indexé dans le même fichier avec le commentaire qui explique pourquoi | `hud_timers.cpp:70-79` |
| Deux commentaires affirment que la durée des chants alliés préfère une **mesure serveur** ; `songdur_check` n'écrit rien, `songPred_` est mort | `song_dur.h:22`, `party_state.cpp:1291` |
| `WindowSkin::load` échoue **en silence**, est réessayée ~240×/s, et `self_check` affiche `(none/proc)` — le testeur lit « thème procédural » là où les assets manquent | `gfx/window.cpp:90-103`, `hud.cpp:244`, `:535` |
| `Font::build` : quand l'atlas déborde, la boucle `break` et publie le slot — les glyphes restants ont `w = h = adv = 0` et disparaissent sans décaler le reste (règle 10 forme 3) | `gfx/font.cpp:89-101` |
| `Font::build` : la cellule d'atlas est réservée sur l'**avance** et non sur l'encre, donc les débords ABC négatifs (italiques) sont rognés d'~1 px | `gfx/font.cpp:90-100` |
| Le buffer de glyphes partagé n'est pas vidé sur le chemin d'erreur SEH → fragment de texte fantôme la frame suivante. Le handler est explicitement conçu pour absorber des fautes **en rafale** | `gfx/font.cpp:35-53`, `hud.cpp:370` |
| `rrect_stroke` : coins feathered, bords droits non feathered — silhouette 1 px plus large aux extrémités. Forme miroir de la règle 2 ; seule primitive du fichier à diverger | `gfx/draw.cpp:478-514` |
| « Reset all settings » ne réinitialise pas `scTP` — **seul** écart sur 302 champs. Vérifié à la main : `scTP` est écrit, relu et comparé, mais absent de `reset_ui_config` (1167-1228) | `ui_config.cpp:1167-1228` |
| `g_pick[24]` (slots du colour-picker) est à 19/24, ne recycle jamais, et déborde **en silence** : le picker ne se dessine plus du tout. Le pendant `ease()` loggue, lui | `config_controls.cpp:567-590` |
| `catOpen_[13]`/`catH_[13]` : seuls tableaux de collapse **sans** `static_assert`, alors que les 4 autres en ont reçu un après s'être fait piéger | `config_page.h:194-195` |
| `box[i].scale` n'est pas re-borné au chargement alors que `x`/`y` le sont sur la même ligne ; `scale = 0` rend la boîte Alliance invisible | `ui_config.cpp:669-675` |
| Le commentaire de `config_rows.h` interdit un usage que la macro suivante fait — **et qui est correct** (`__LINE__` se substitue au point d'invocation). La vraie cause du bug livré était la **boucle** | `config_rows.h:29-31` vs `:55-59` |
| Deux générateurs ne peuvent plus tourner depuis le déménagement du dépôt (`../../res`, `../../addons`) — sans repli, contrairement à leurs voisins | `gen_buff_names.py:6`, `gen_resistances.py:13` |
| La règle de couches n'a **aucun** garde mécanique et est déjà fausse à 3 endroits (exception revendiquée et défendable — le défaut est l'absence de garde, pas l'exception) | `gfx/font.cpp:5`, `texture.cpp:4`, `window.cpp:5` |
| Un des 242 checks ne peut pas échouer : `CHECK(... || true)`. Seule tautologie des 196 sites `CHECK*` | `tests/t_skillchain.cpp:69` |
| Garde-fou règle 10 : le trou `_` est **refermé** (0 hit), mais il reste indexé sur le mot « tried » — le projet écrit ce motif sous le nom `done` (`ui_config.cpp:745`, `font.cpp:210`) | `build.yml:59-91` |
| Après un échec robocopy, le message affirme « your current build was left untouched » — c'est faux, les assets sont partiellement en version N+1 | `aioupdate.ps1:134-138` |

---

## 5. Statut de l'historique — « où on en était »

Chaque constat encore ouvert des neuf rapports précédents a été **rouvert et vérifié dans le code actuel**,
sans faire confiance ni au rapport ni au message de commit.

**56 corrigés · 102 ouverts · 11 classés sans suite.**

Le décompte a été recompté à la granularité **un défaut par ligne** : les 76 « lignes » du premier
dépouillement regroupaient plusieurs défauts chacune (les 4 options absentes de `build.bat`, les 2 de
`deploy.bat`, les 4 helpers dupliqués…). Même périmètre, aucun item nouveau, aucun doublon — la liste
complète est en **annexe §9**. Répartition : **1 S1 · 40 S2 · 61 S3**.

Note d'historique : le dépôt a été recréé le 2026-07-16 (`656a85b`), donc plusieurs correctifs réels
(couches `model/`, OOM texture, `/W4`, alliance dans `party_order`) apparaissent dans le commit initial —
ils sont **antérieurs à l'historique git**, pas absents.

### Les points qui revenaient d'un audit à l'autre, tranchés

| Item | Verdict |
|---|---|
| Garde-fou CI « règle 10 » aveugle aux membres suffixés `_` | **CORRIGÉ** (`b77975b`) — regex exécutée sur l'arbre courant : **0 hit** |
| `u32 bw = h * 0.55f` (`target.cpp:189`) | **CORRIGÉ** (`29814a9`) — `target.cpp:194` est `const float bw` |
| Smash de pile JSON + récursion non bornée | **CORRIGÉS** — `MAX_DEPTH = 64` gardé au point de descente, `num_str` clampe **avant** d'imprimer (et mappe NaN à 0) |
| `relOpen_[]` vs `RELEASES_N` | **CORRIGÉ et verrouillé** — `RELEASES_N = 54` contre `relOpen_[128]`, avec `static_assert` (`config_page.cpp:1652`). Le piège ne peut plus se reproduire sans erreur de compilation |
| Atlas de buffs chargé 7 fois | **CORRIGÉ** (`f2cd04f`) — emprunt à l'atlas propriétaire unique |
| 6 générateurs Python cassés | **PARTIEL** — 19/21 tournent ; 2 échouent (chemins hérités), 1 corromprait sa sortie (S2-19), 1 en-tête désynchronisé (S2-18) |
| Uids `ease()` hand-pickés | **OUVERT mais inoffensif** — 10 sites dans `config_page.cpp`, plages mutuellement exclusives, collision avec un hash FNV 32 bits négligeable. Ne pas le re-signaler comme un défaut |
| `sprintf` → `snprintf` (~200 sites) | **CLASSÉ SANS SUITE** — décision écrite `build.bat:44-45` ; 127 sites utilisent déjà `_snprintf`, le reste est du formatage numérique vers buffer fixe |
| clang-tidy jamais exécuté | **OUVERT** |
| Palier-2 dédup config | **CORRIGÉ** (a/b/c livrés : `9599eb8`, `1cf54bb`, `ac992fe`) |

### Les 5 items ouverts les plus coûteux

1. **Le bouton « Réinitialiser » des Timers vide la colonne Durée** (`hud_timers.cpp:37` → `buff_timers_clear()`).
   **Correction apportée à l'agent** : il l'annonçait irréversible. C'est faux — j'ai vérifié les sites de
   remplissage : `buffTimers_` est repeuplé par le 0x063 order 9 (`party_state_pointwatch.cpp:36-42`), que le
   serveur renvoie à chaque zone-in. La perte dure donc **jusqu'au prochain changement de zone**, pas pour
   la session. Reste un bouton livré qui vide durablement une colonne sans le dire.
2. **Diagnosticabilité pour le testeur NA** : `//aio doctor` en français, `src/gfx/` sans un seul
   `debug::log`, échec de lecture de config muet. La classe de bug la plus chère du projet est
   « ça reproduit chez lui et pas chez toi », et l'outil qui nomme le remède parle une langue qu'il ne lit pas.
3. **Le retour d'écriture ignoré** — le défaut corrigé par `e46144b` est ressorti intact dans le chemin
   des profils (voir S2-13).
4. **Sanitisation des flottants refermée sur une seule instance** : 16 champs sur 101 sont clampés au
   chargement. C'est la racine du S0 qui tuait le client, bouchée pour `mmZoom` seulement.
5. **`profile_load` superpose au lieu de remplacer**, et 20 clés manquent à `default_profile.txt` : charger
   « Default » ne réinitialise rien et le premier Save grave l'héritage. La sémantique (instantané vs
   surcouche) n'a toujours pas été tranchée.

---

## 6. Chiffres mesurés (exécutés, pas lus)

| | |
|---|---|
| **Build** | `build.bat` OK, exit 0, DLL 3 479 552 o, horodatage bougé |
| **Warnings** | **39** en local, **33** en CI (les 6 C4457 sont dans `aiohud_probes.cpp`, untracked). Delta vs 42 au 26/07 : **−3** |
| **Troncature numérique** | **C4244 / C4267 / C4018 : ZÉRO.** Les 3 C4244, dont le bug de rendu livré, sont corrigés. Rien à trier de ce côté |
| **Nature des 39 restants** | 18 C4189 + 12 C4101 (locales inutilisées) + 6 C4457 + 2 C4458 + 1 C4505 — **tous vérifiés site par site, tous inoffensifs** |
| **Tests** | `tests.bat` : **242 checks, 0 échec** (t_omen 86 · t_durations 29 · t_json 26 · t_skillchain 22 · t_limbus 20 · t_config 13) |
| **Règle de couches** | `model → ui/gfx` : **0 violation** (le sens dangereux est propre). `gfx → model` : 3, toutes `model/paths.h`, exception écrite sur place. `gfx → ui` : 0 |
| **Générateurs** | 19/21 tournent · 18 régénèrent à l'identique · 1 désynchronisé · 1 corromprait sa sortie · 2 ne tournent plus · 1 invérifiable |
| **CI / release** | **10/10 runs verts**, aucun rouge ignoré. `v1.0.74` = tag = HEAD = `9c953438` |
| **Garde-fous** | règle 10 (bloquante) **0 hit** · règle 6 (advisory) 5 · règle 9 (advisory) 9 · `%f` **0** · bannière GENERATED **0** |

**`audit-protocol.md` §1 est périmé sur deux lignes** : « une suite offline (75 checks) » → c'est **242** ;
et le tableau des plus gros fichiers date d'avant les derniers commits.

---

## 7. Vérifié et SAIN — résultats négatifs, à ne pas re-scanner

Ce qui a été regardé sérieusement sans rien produire. C'est la moitié utile d'un audit.

**Sûreté mémoire — le résultat le plus fort de cette passe.** *Aucune déréférence nue de pointeur jeu dans
tout le projet.* Les 8 seuls `*(T*)addr` hors `safe_read` sont soit des bit-copies de locales, soit dans un
bloc `__try`, soit des lectures du snapshot plugin. **Toutes** les copies de chaînes venant du jeu ont été
vérifiées une par une (17 destinations) : bornées et terminées, aucun `strcpy`/`sprintf` non borné. Tous les
index issus de la mémoire jeu sont bornés avant usage (entités `< 0x900`, jobs 1..22, statuts `< 1024`,
zones, recasts `< 0x400`). Les 5 marches à pas variable du 0x028 sont identiques et correctes, `getbits`
est borné par la taille déclarée. `feed_packet` est inline sur le thread principal : **pas de réentrance
paquet/rendu**. Les deux rings inter-threads sont des SPSC corrects.

**Cycle de vie du device — inventaire complet des 3 familles de ressources GPU.** Atlas de police,
`WindowSkin`, matériaux procéduraux : dans les trois cas `on_device_lost` **oublie** (met à 0, ne `Release`
pas), `dispose` `Release`, `ensure`/`load` recrée paresseusement. **Aucun `Release` dans un
`on_device_lost`, aucun handle emprunté relâché par un non-propriétaire.**

**Règle 10 dans `gfx/`** : la couche n'utilise pas `TexRetry` et n'en a pas besoin — aucune de ses créations
de ressource n'a de latch one-shot ; le budget de bake est **réarmé chaque frame**, ce n'est pas un budget
épuisable. Les deux latches présents portent sur des sources non transitoires et sont corrects.

**Hygiène D3D** : les 14 passes additives de `window.cpp` et `draw.cpp` ont été tracées une par une —
**toutes**, y compris les sorties conditionnelles, remettent `DESTBLEND = INVSRCALPHA`. Les textures sont
déliées sur les deux chemins de sortie de `draw_window`/`draw_mat`/`Font::draw`. Les 7 paires
`rrect_clip_begin/end` sont équilibrées, sans `return` anticipé entre les deux, sans imbrication.

**Bornes géométriques** : les indices maximaux de chaque tableau de sommets ont été **recalculés**
(`tdisc`, `disc`, `rrect`, `rrect_left`, `arc_feather`, `fill_poly_aa`…) — aucun dépassement possible,
pile max ~13,5 Ko. Divisions par zéro : gardées partout. Demi-pixel : les 14 constructeurs de sommets
l'appliquent, et `gbuf_quad` ne le **double** pas. Feathering : 6 primitives sur 7 sont cohérentes bords
**et** coins (raccords vérifiés analytiquement) ; seul `rrect_stroke` diverge.

**Round-trip config — diff mécanique sur les 302 champs persistables** : tout champ écrit est relu **et**
comparé par `persist_eq`. **Zéro** « écrit-mais-jamais-relu », **zéro** absent de `persist_eq`. Seul écart :
`favColors`, exclu par conception. Aucune collision de clés de fichier (les 7 préfixes ambigus vérifiés).
Écriture **atomique** partout (temp par PID + `MoveFileExA`, retour de `fclose` **et** du rename vérifiés).

**Bornes des tableaux de `hud_timers.cpp`** : les 13 tableaux à capacité fixe vérifiés un par un — aucun
débordement. Aucune soustraction non signée qui passe sous zéro. **Aucune allocation par frame** dans le
fichier. Aucun uid `ease()` choisi à la main dans les 15 `*_config.cpp` (27 contrôles vérifiés).

**Updater** : le checksum est appliqué **avant** l'extraction, lu depuis un **fichier** (jamais un `byte[]`),
fail-closed si le sidecar manque — et le bug `.Content` **n'a jamais été publié** (`c9cbd1f` est *dans* le
tag `v1.0.71`). Comparaison de versions **numérique** via `[version]`, 7 cas exécutés : aucun downgrade
possible. MAX_PATH : **réfuté** comme risque (il faudrait une racine Windower de 177+ caractères).
`init.txt` : mutex + temp/rename des deux côtés.

**Parseurs de `texture.cpp`** : `load_bmp_texture` valide bpp, dimensions et taille en arithmétique 64 bits
avant toute lecture de pixels ; l'index de palette est structurellement borné ; `write_gear_icon_bmp` est
atomique. **UTF-8** : `utf8_next` ne peut pas dépasser le NUL terminal, tous les sites clampent `c` avant
d'indexer.

---

## 8. Ce que je ferais, dans cet ordre

1. **`buffs_[18]` (S1-1)** — le seul défaut qui produit à la fois des lignes alliées jamais dessinées, des
   alertes OUT supprimées et une saturation de session. Le correctif existe déjà 60 lignes plus loin,
   appliqué à `jobShadow_` : le copier.
2. **Sérialiser les updaters (S1-2)** — mutex nommé, `$bak` par PID, retrait du relais `request.txt`.
   Seul constat qui peut encore produire une installation incohérente.
3. **Le paquet zonetracker (S2-1, S2-2, S2-3, S2-4)** — quatre corrections de deux lignes chacune, dans
   deux fichiers, sur du code de la semaine, dont deux régressions franches. Verrouillables par des tests
   offline qui existent déjà en creux dans `t_omen.cpp`.
4. **Le warm-up de police hors state block (S2-9)** — déplacer 6 lignes à l'intérieur du `__try`. C'est le
   seul dessin non protégé du plugin, et il corrompt le state block lui-même.
5. **Les deux garde-fous CI manquants (S2-14, S2-15)** — parser les deux scripts livrés (2 lignes), et
   vérifier le zip **avant** `gh release create`. Ce sont exactement les contrôles qui manquent pour que la
   chaîne détecte le type de défaut qui l'a déjà mordue.
6. **`mobskills_gen.h` + `gen_trusts.py` (S2-18, S2-19)** — l'un est une commande à lancer, l'autre un
   chemin à corriger ; laissés en l'état, le second détruit sa donnée à la première régénération.
7. **`profile_delete` et les retours d'écriture jetés (S2-12, S2-13)** — un buffer local et quatre `if`.
8. **Rendre l'échec racontable** — `try/catch` autour du `Status` du `catch` dans le `.ps1`, un `update.log`
   à côté de la DLL, et une section « update » dans `//aio doctor`. Aujourd'hui un échec de mise à jour ne
   laisse **rien** à envoyer, et le seul message affiché nomme le mauvais remède.

---

## 9. Annexe — les 102 items ouverts hérités des audits précédents

Format : `intitulé — fichier:ligne`. Dédoublonnés : quand deux rapports décrivaient le même défaut, une
seule ligne. **Ne contient pas** les constats nouveaux de cette passe (§2-4).

### S1

- **timers** — Bouton « Réinitialiser » vide la colonne Durée — `hud_timers.cpp:37` + `tm_config.cpp:98` — timers-audit 2026-07-20.
  *Rectification de cette passe : la perte dure jusqu'au prochain changement de zone (le 0x063 order 9 repeuple `buffTimers_`), pas pour la session.*

### S2 — timers
- Mode icône : atlas absent → nom dessiné dans une colonne mesurée pour une icône — `hud_timers.cpp:1070` vs `:1117`
- Aucune troncature ni élision : une ligne alliée longue élargit la boîte sans plafond — `hud_timers.cpp:1069-1078`
- Clignotement SP muet pendant le maintien à 0:00 — `hud_timers.cpp:1121,1129`

### S2 — config
- Sanitisation des flottants partielle : 16 champs clampés sur 101 — `ui_config.cpp:680-700`
- `TextStyle::size`/`outline` jamais clampés à la lecture — `ui_config.cpp:155-158`
- `profile_save()` ignoré aux 4 points d'entrée UI — `config_page.cpp:747,1215,1219,1264`
- `charprof_save` : 2 sorties muettes — `ui_config.cpp:96,99`
- `active.txt` : seul fichier partagé écrit sans temp+rename — `ui_config.cpp:47`
- Échec de lecture de la config totalement muet — `ui_config.cpp:477`
- `profile_load` superpose au lieu de remplacer — `ui_config.cpp:923-933`
- `default_profile.txt` : 20 clés absentes sur 109 — `assets/default_profile.txt`
- Échantillon minimap de l'onglet Aide : re-décode le DAT chaque frame en cas d'échec — `config_page.cpp:1499-1507`
- Règle 9 : 10 uids `ease()` codés en dur — `config_page.cpp:430,535,735,1091,1116,1131,1248,1290,1304,1351`
- Filtre CHR : pas de réconciliation au chargement d'un ancien profil — `tm_config.cpp:161-164`

### S2 — model
- Le filtre minimap confond « mort » et « vitals pas encore reçues » — `game_mem.cpp:146`
- Réticule Quick Draw marqué même sur un tir résisté — `party_state.cpp:1486-1500`

### S2 — hud (autres widgets)
- État color-quad non rétabli après `draw_themed_box` — `hud_hatelist.cpp:172`
- `sprintf(r.dist,"%.1f")` dans un `char[8]` non clampé — `hud_hatelist.cpp:83,92`
- Markers / atlas d'éléments minimap : budget borné mais totalement muet — `minimap.cpp:365-373`
- `draw_icon_cell` : 13 états + bind/unbind par icône, laisse `MODULATE`/`MIPFILTER=NONE` — `hud.cpp:707-717`
- Retry `WindowSkin::load` non borné ni throttlé (I/O disque à 60 Hz) — `hud.cpp:244` + `box_style.cpp:30`

### S2 — gfx
- Aucune instrumentation : 0 `debug::log` dans toute la couche — `src/gfx/*`
- Polices fournies : budget de 8 **appels** sans espacement temporel — `font.cpp:210-216`
- Primitives : 8-15 `DrawPrimitiveUP` là où 1 suffit (~2 400/frame) — `draw.cpp:202,245,352,411,470`
- Vtable relue en 2 `safe_read` SEH à chaque appel device — `d3d.h:49-53` + `windower.h:31-37`
- `Font::begin()` (22 états) rappelé par ligne et par élément — `font.cpp:247-273`

### S2 — updater
- Un tag distant illisible est lu « à jour » — `aioupdate.ps1:36,59,63`

### S2 — plugin / diagnostic
- `//aio doctor` répond en français au testeur NA — `aiohud.cpp:990-995` + `hud.cpp:409-459`
- `//aio doctor` n'est annoncé nulle part en jeu — `aiohud.cpp:195`

### S2 — architecture (transverse)
- Fonctions géantes intactes (`on_action` 823 l., `Party::draw` 678, `Player::draw` 625, `Target::draw` 584, `draw_edit_layout` 419, `Minimap::draw` 401) — `party_state.cpp:728` e.a.
- Le rendu pilote le modèle : 15 opérations de modèle dans `Hud::render` — `hud.cpp:249-284`
- Aucun propriétaire du cycle de vie : 0 `ILifecycle`, 4 resets séparés — `hud.cpp:185` / `aiohud.cpp:295` / `party_state_roster.cpp:138`

### S2 — outillage
- 2 générateurs cassés, pas de `regen_all.bat` — `gen_buff_names.py:6` + `gen_resistances.py:13`
- 3 règles CI restent `continue-on-error` dont 2 déjà à zéro hit — `build.yml:91`
- Règle 6 : 5 lectures de mémoire jeu dans `src/ui` — `hud_skillchains.cpp:169,171,173` + `hud_timers.cpp:345,704`
- `/WX` absent : 39 warnings livrés — `build.bat:49`
- `/Zi` + `/DEBUG` absents : crash de testeur non symbolisable — `build.bat:49`
- `/DNOMINMAX /DWIN32_LEAN_AND_MEAN` absents — `build.bat:49`
- `deploy.bat` ne construit rien et ne vérifie pas la fraîcheur de la DLL — `deploy.bat:21`
- `deploy.bat` : robocopy sans `/PURGE`, pas de garde `errorlevel 8`, NOTICE non copié — `deploy.bat:26`

### S3 — timers
- `tmRecMode` : réglage mort (sauvé, relu, comparé, jamais écrit) — `ui_config.h:319` + `hud_timers.cpp:1142`
- Catégorisation absente au rendu (`job_track_gen.h` non inclus par le renderer) — `hud_timers.cpp:2-29`
- `buff_caster_for` recalculé 5×/frame — `hud_timers.cpp:262,358,392,497,747`
- `abil_name_by_recast` : scan linéaire de 626 entrées — `hud_timers.cpp:57,71`
- `spell_info(id)` appelé 2× dans la même expression, 5 sites — `hud_timers.cpp:306,584,625,881,939`

### S3 — config
- `logoTried_` : champ mort, plus aucun site de chargement — `config_page.h:260`
- `draw_layout_category()` morte (déclarée + définie, 0 appelant) — `config_page.cpp:304`
- `char pre[32]` couplé implicitement à `nameBuf_[32]`, sans `static_assert` — `config_page.cpp:1047,1257`
- Taxe des 3 endroits : 413 champs à maintenir à la main dans save/load/persist_eq — `ui_config.cpp`
- `data\version.txt` écrit et jamais lu — `ui_config.cpp:777`
- Le Help nomme 4 onglets, l'UI en rend 6 (Edit Layout, Debug manquants) — `config_help_data.h:20-29`
- Grimoire (SCH) et Arcade WS sans page d'aide — `config_help_data.h:682-696`

### S3 — model
- `read_player_buffs(…, bool* ok = 0)` : défaut conservé, 4 appelants ne le passent pas — `game_mem.h:196`
- ODR : tables `static const` + accesseurs `inline` dans les 21 `*_gen.h` — `itemnames_gen.h:8,23525`
- `trusts_gen.h` sans `#pragma once` et hors `namespace aio` — `trusts_gen.h:1-6`
- Clamp de `pw_.merits` conditionné à `maxMerits` non nul — `party_state_pointwatch.cpp:88`
- `gs.mapEntN = 0` quand `mmShow` est faux (snapshot faux pour le prochain consommateur) — `game_mem.cpp:1023`
- Tableau d'entités balayé plusieurs fois (pas d'`entity_snapshot`) — `game_mem.cpp:122,163`
- Commentaire `CACHE_VER` périmé (« 3 » pour la valeur 5) — `party_state.cpp:587`
- `for_each_target()` jamais créé (walk cible dupliqué) — `party_state.cpp`
- Quatre horloges, un seul nom `now` (18 sites ms / 5 ticks) — `hud_timers.cpp:233`
- Nombres magiques : `0x900` ×12, `0xCA8` ×13, `TAU` local — `src/model/` + `liquid_bars.cpp:93`

### S3 — hud (autres widgets)
- `char tb[12]` pour `-%d:%02d` (le jumeau utilise `[16]`) — `target.cpp:909`
- Retour de `snprintf` utilisé en offset sans test de troncature — `hud_zonetracker.cpp:407-412`
- `dbShow` testé **après** l'appel `target_debuffs` — `target.cpp:473-474`
- Recherche de widget par `strcmp`, 6 sites/frame — `hud.cpp:216,339-347`
- Règle 1 : `gy = ry + (mh-gh)*0.5f` non snappé — `party.cpp:859,1044` + `player.cpp:385`
- Style Segments : remplissage à angles droits dans un alésage arrondi — `party_gauges.cpp:231-233`
- `preload_texture` appelé par frame (jusqu'à 54×) — `liquid_bars.cpp:462,628`
- `dGetBackBufferSize` interroge le device chaque frame — `hud.cpp:154,181`
- Abstraction `Widget` : 5 types enregistrés, 11 `draw_*` codés en dur — `factory.cpp:12-17` + `hud.cpp:349-365`
- `clampf` redéfini 5 fois — `config_controls.h:54`, `edit_box.cpp:72`, `minimap.cpp:27`, `player.cpp:27`, `target.cpp:30`
- `buff_cell_uv()` : toujours zéro appelant, 9 sites recalculent les UV à la main — `buff_atlas.h:34`
- Noms d'éléments divergents « Thunder » / « Lightning » — `resistances.h:29` + `skillchain.h:36`
- Atlas d'emblèmes de job chargé 2 fois (pas de `ui/job_icons.h`) — `party.cpp:365` + `player.cpp:201`

### S3 — gfx
- Contour de texte : 9 passes de géométrie par glyphe — `font.cpp:303-307`
- `tdisc` : ~13,5 Ko de pile par appel — `draw.cpp:42-66`
- `draw_window` repose CLAMP 3× sur le même stage — `window.cpp:177,185,191`
- Pas de wrapper RAII pour les handles de texture — `src/gfx/`
- Zéro `enum class` ; constantes D3D8 dans un enum anonyme unique — `d3d.h:20`

### S3 — plugin / diagnostic
- `//aio thlog` tronqué à 128 o alors qu'un 0x028 en fait 508 — `aiohud_probes.cpp:855`

### S3 — architecture
- Zéro `[[nodiscard]]` sur les fonctions renvoyant un statut — `src/` + `include/`

### S3 — outillage
- `where rc.exe` sans `else` — `build.bat:43`
- `package.bat` copie `layout.json` sans contrôle d'erreur — `package.bat:40`
- `permissions: contents: write` au niveau workflow, pas de `concurrency:` — `build.yml:16-17`
- Appels API GitHub anonymes à chaque chargement du plugin — `aiohud.cpp:173`
- Pas de `/MP` : rebuild total mono-process — `build.bat:49`
- Notes de travail à la racine ni trackées ni ignorées — récidive constatée (`DISCORD_POST.md`) — `.gitignore`
- `docs/` non versionné hors `game-data/` + `audits/`, décision toujours pas écrite — `.gitignore:47-52`
- 3 blocs de règles mortes dans `.gitignore` ; `pol_*.png` reste global — `.gitignore:14-25`
- `assets/fonts/` absent alors que le sélecteur propose Roboto / Open Sans — `font.cpp:206` + `ui_config.cpp:1233`
- 1 323 gearicons livrés en vrac (pas d'atlas) — `assets/gearicons/`
- `job_icons.raw` et `marker_src/` sans générateur — `assets/` + `scripts/`
- 9 panneaux de config à nom opaque ; `ws_config.cpp` sans `hud_ws.cpp` — `src/ui/`
- `scripts/` : 29 `.py`/`.ps1` encore à plat — `scripts/`

### S3 — docs
- `interface-map.md` : 12 modules sur 13 absents, 6 sondes citées inexistantes — `docs/design/interface-map.md:343-344`
- `skillchains.md` ignore tout v1.0.65 — `docs/game-data/actions/skillchains.md`
- `release-checklist.md` répète la fausse affirmation sur geartrace + pointeur périmé — `docs/architecture/release-checklist.md:35-38`
- `build-deploy.md` sans `package.bat`, séparation repo/runtime ni robocopy — `docs/architecture/build-deploy.md`
- Aucun `docs/tech-stack/generated-data.md` pour les 23 générateurs — `docs/tech-stack/`
- `config-panels.md` cite le filtre « Others' buffs » (legacy) — `docs/architecture/config-panels.md:150`


---

## 10. Corrections appliquées — 2026-08-06

Tous les constats **nouveaux** de ce rapport (§2, §3, §4) ont été traités, sauf **deux** volontairement laissés
en l'état (§10.2). Build vert, 257 checks verts, **aucun warning ajouté** (39 avant, 39 après), garde-fous CI
locaux repassés.

### 10.1 Traités

| Constat | Correction | Vérification |
|---|---|---|
| **S1-1** `buffs_[18]` | Horodatage `seen` par slot + **éviction du plus ancien** (le correctif `jobShadow_` recopié) ; et balayage des slots dont le membre n'est plus dans la party (`party_order > 5`), car le 0x076 ne peut plus les rafraîchir : « périmé » redevient « inconnu » | Relu : `prune_other_buffs_worn` traite un cache absent comme « invérifiable → on garde », et l'émission FOCUS ne lève pas d'OUT sans liste prête. La dégradation va donc dans le sens sûr |
| **S1-2** dual-box | Mutex nommé `Global\AioHudUpdater` : le second installeur sort immédiatement sans toucher `done.txt`. `$bak` par PID. Le relais `request.txt` est **conservé** (un ancien plugin n'a pas d'autre déclencheur) — la collision est réglée en aval | `PSParser::Tokenize` : syntaxe OK |
| **S2-1** `lc_load` | Appelé aussi sur le chemin `zt_load` (celui du rechargement de plugin) | — |
| **S2-2** `LC_VER` | Ne contient plus `sizeof(LimbusWeek)` (la queue est optionnelle à la lecture) **et accepte les deux versions** : le tag replié a été livré en v1.0.73/74, donc le corriger sans ça aurait coûté un second reset | — |
| **S2-3** Omen masqué | `bonusFloor = omen_floor_has_bonus(...) \|\| nrows > 0` — un objectif parsé prouve le bonus | test ajouté |
| **S2-4** casse du bandeau | Un seul jeton `OMEN_TREASURE_TOK`, comparé **sans casse** (`omen_ci_find`) par les deux prédicats | **test ajouté** : la même chaîne dans 4 casses traverse les deux prédicats |
| **S2-5** ligne « N: » avalée | `omen_feed_slots` parse d'abord et refuse la branche bandeau si `L.slot != 0` | **test ajouté** : échouait avant, passe après |
| **S2-6** famine `fm[24]` | Refus de créer une entrée sur une cible d'alliance (illisible par construction) + `live` passe de `<= 17` à `<= 5` + **log** à saturation | — |
| **S2-7** équipement illisible | `else` + une ligne de log par session (`EQUIP UNREADABLE at cast`) | — |
| **S2-8** OUT sous `tmMine` off | L'émission des entrées alliées est conditionnée à `C.tmMine` | — |
| **S2-9** state block | Le pré-bake de police est **déplacé à l'intérieur** du `__try` qui suit `dCreateSB` | — |
| **S2-10** 16 tailles de police | `NSLOT` 16 → 32 **et éviction LRU** (`Slot::used`) ; la nouvelle passe ne remplace l'ancienne qu'après un bake réussi | — |
| **S2-11** `MIPMAPLODBIAS` | Remis à 0 dans `dTexQuadState`, là où sont déjà centralisés `MIPFILTER` et l'adressage | — |
| **S2-12** `profile_delete` | Le nom est **copié** avant `profile_refresh()` | — |
| **S2-13** échecs de sauvegarde | Les 4 appelants honorent le retour : aucun effet de bord en cas d'échec, et un bandeau rouge de 8 s dans l'onglet Profil | — |
| **S2-14** scripts non parsés | Étape CI « Parse the shipped scripts » (`PSParser::Tokenize`) **avant** `package.bat`. Le Lua est délibérément **hors CI** : le runner n'a pas de Lua, et l'heuristique structurelle qui aurait servi de substitut produit de faux échecs — il est vérifié par `luac -p` en local (fait : OK) et **déployé** désormais | étape ajoutée, YAML relu |
| **S2-15** publication avant vérif | Nouvelle étape « Verify the payload before publishing » : sidecar ↔ zip, `plugins/AioHud.dll` présent et > 100 Ko, addon présent, bug `.Content` absent — **avant** `gh release create` | ordre des étapes vérifié |
| **S2-16** `deploy.bat` | Déploie `aioupdate.lua` vers `addons\aioupdate\` | — |
| **S2-17** population ≤ 1.0.73 | Aucun code ne peut protéger ce passage — **à annoncer dans les notes de la prochaine release** | (action éditoriale) |
| **S2-18** `mobskills_gen.h` | Régénéré : 62 lignes corrigées (*Kibosh*, *Tail Blow*, *Subterfuge*…) | diff mesuré |
| **S2-19** `gen_trusts.py` | Lit désormais sa **propre sortie** (`trusts_gen.h`), où la table vit ; **et refuse d'écrire** si le nombre de métiers connus régresse | exécuté : **106 métiers connus** (0 avant), sortie identique à l'existant |
| S3 planchers de paquets | `on_treasure_add` (0x1C), `on_treasure_lot` (0x26), `on_pet_info` (0x0E), `on_pet_status` (0x18) | — |
| S3 `read_entities_by_id` | `refresh_hate` honore le retour : `got == 0 && !entity_array()` → on garde, on ne purge pas | — |
| S3 `read_party_aggro_mobs` | **Supprimé** (28 l. + 9 Ko de BSS) ; `hate-list.md` réécrit — il le présentait comme la source PRIMAIRE | plus aucune référence |
| S3 lectures en draw | `zone_id()` → `f.game->zone` ; `read_usable_ja_bits` → `GameState::jaBits/jaOk` rempli par le poller | règle 6 : 5 hits → **3** |
| S3 `zt_save` par ligne | `zt_save_soon()` + flush unique en fin de `drain_game_text` — **uniquement** sur le chemin texte (les 9 sites du chemin paquet ont été restaurés après une substitution trop large) | 7 sites, vérifiés un par un |
| S3 double `lc_save` | Fusionné en un seul | — |
| S3 lecture de `zt_` hors file | `zt_mode_published()` : un scalaire publié par le thread principal, lu par le thread texte | — |
| S3 débordement nom de coffre | Boucle bornée sur `strlen`, plus sur `*c` | — |
| S3 `abil_name_by_recast` | **Indexé** (comme son jumeau), reconstruit seulement quand le bitmap JA change | — |
| S3 commentaires `songdur` | Corrigés : `songdur_check` est un **contrôle**, pas une source | — |
| S3 `limbusWeekLeft` / `weekSeen` | Marqués « HISTORIQUE — ÉCRIT, JAMAIS LU ». **Non supprimés** : ils font partie des images disque, les retirer invaliderait le cache de zone de tout le monde pour rien | — |
| S3 atlas de police plein | Une ligne de log nommant la police, la taille et le dernier glyphe gravé | — |
| S3 batch de glyphes | `font_reset_batch()` appelé depuis le handler SEH du HUD | — |
| S3 skin muette | Log au premier échec + `failed()` ; `//aio self_check` distingue désormais **trois** états au lieu de deux | — |
| S3 `scTP` | Ajouté à `reset_ui_config` | — |
| S3 `box[].scale` | `CLF(c.box[k].scale, 0.50f, 2.0f)` au chargement | — |
| S3 `g_pick[24]` | Log unique à saturation | — |
| S3 `catOpen_`/`catH_` | Dimensionnés par `CFG_CAT_N = 32` | — |
| S3 commentaire `config_rows.h` | Réécrit : le piège est la **boucle**, pas la macro (`__LINE__` s'expand bien au point d'invocation) | — |
| S3 générateurs cassés | `gen_buff_names.py` et `gen_resistances.py` : `find_res()` / `find_addons()` avec repli + variable d'environnement | exécutés : sorties **identiques** aux fichiers livrés |
| S3 test tautologique | `CHECK(... \|\| true)` remplacé par les ids mesurés | — |
| S3 règle 10 « done/loaded » | Élargie **et promue bloquante** : les 7 sites légitimes portent un `rule10-ok: <raison>` | mesuré : **0 hit** |

### 10.2 Délibérément NON corrigés — et pourquoi

Deux constats de rendu, tous deux **PLAUSIBLE** et purement cosmétiques (~1 px), qu'il est impossible de
vérifier sans device. Les corriger à l'aveugle échangerait un défaut subtil contre un risque de régression
visible sur **tout** le HUD — c'est exactement ce qu'un correctif ne doit pas faire.

- **`Font::build`, débord ABC négatif** (`gfx/font.cpp:90-100`). La correction (réserver la cellule sur l'encre
  et non sur l'avance) recalcule les UV de **chaque glyphe**. À faire avec le jeu sous les yeux.
- **`rrect_stroke`, feather des coins sans feather des bords** (`gfx/draw.cpp:478-514`). Ajouter le feather aux
  4 bandes droites élargit visuellement le liseré de **toutes** les barres de 1 px. Choix esthétique, pas
  correctif mécanique.

### 10.3 Ce que ces corrections ne couvrent pas

- Les **102 items hérités** de l'annexe §9 : non traités. Plusieurs sont des chantiers d'architecture
  (découper `on_action`, RAII sur les handles, `enum class`) dont la correction porterait un risque sans
  commune mesure avec leur gain.
- **Aucune vérification en jeu.** Tout est validé par compilation, tests hors ligne, exécution des générateurs
  et relecture. Les changements à observer en priorité au prochain lancement : les timers de buffs alliés
  (S1-1), la netteté du texte après ouverture de `//aio` (S2-10), et l'aspect de la minimap et des icônes de
  party (S2-11).
- **Pas de version ni de changelog.** Ces corrections ne sont ni commitées ni publiées : `RELEASES[]` /
  `CL_<ver>` dans `ui/config_changelog.h` restent à écrire au moment de la release.


---

## 11. Distances party/alliance — mesuré en jeu, 2026-08-06

Audit du module distance, puis capture en jeu via `//aio rangelog` (Delkfutt Tower, zone 158, Tetsouo +
Kaories). Cette section remplace le raisonnement par des faits : la question de la hauteur était le seul
constat de cet audit que j'avais refusé de trancher sans mesure.

### 11.1 Le protocole

Sonde `//aio rangelog [sec]` (`aiohud.cpp`, fichier SUIVI donc présente en release ; nommée `rangelog` et non
`distlog` parce que `aiohud_probes.cpp` possède déjà le jeton `dist`, qui l'aurait interceptée en silence sur
un build de dev). Elle dumpe une fois par seconde, pour la party ET les deux alliances : l'identité de
l'entité pointée par `member+0x20` (MATCH / STALE), la distance horizontale `dh`, la distance 3D `d3`, l'écart
de hauteur `dy`, et ce que le HUD affiche. Tout en entiers ×10 — `debug::log` passe par `wvsprintfA`, sans
virgule flottante.

### 11.2 Résultat : la constante du code est EXACTE, la hauteur compte faiblement

| Configuration | Frontière du Cure, mesurée | Modèle horizontal prédit | Modèle 3D prédit |
|---|---|---|---|
| À plat (`dy` = 0,4) | **20,8** | 20,8 ✓ | 20,8 ✓ |
| Dénivelé `dy` = 15,5 | **entre 19,5 et 19,8** | 20,8 ✗ | 13,9 ✗ |

Casts (dénivelé 15,5) : `dh` 20,5 → échoue · 19,8 → échoue · 19,5 → passe.
Cast à plat : `dh` 20,79 → passe.

**Les deux modèles candidats sont réfutés.** Conclusions fermes :

1. **`kCastRange = 20.8f` est la bonne valeur** et ne doit pas bouger. Le commentaire de `party.cpp:85`
   (« 20.79 casts, 20.80 fails ») est confirmé au centième par la mesure à plat.
2. **La hauteur réduit la portée horizontale, mais très peu** : ~1,1 yalm perdu pour 15,5 yalms de dénivelé.
   Une distance euclidienne en aurait fait perdre près de 7. Le modèle horizontal est donc *légèrement
   optimiste*, pas faux — ce qui explique qu'il n'ait jamais été signalé comme un bug.
3. Le défaut visible est **borné à ~1 yalm** : entre 19,8 et 20,8 avec du dénivelé, le HUD affiche « à portée »
   (jaune) alors que le sort refuse. Toujours dans le sens optimiste.

**Aucune correction appliquée, délibérément.** Une seule altitude ne détermine pas la contribution de la
hauteur : ajuster une formule sur un point unique remplacerait une petite erreur connue par une grosse erreur
inconnue. Il faut un second dénivelé nettement différent (~30 yalms) pour fixer le coefficient. Ordre de
grandeur d'après ce point : la hauteur pèse comme si elle valait ~0,44× sa valeur mesurée — à confirmer, ou à
réinterpréter (l'unité du Y de l'entité n'a jamais été calibrée contre le yalm).

### 11.3 Ce que la capture confirme AUSSI

- `shown` est rigoureusement égal à `dh` sur les 599+ captures : le HUD affiche bien la distance horizontale,
  sans bug de calcul ni de conversion.
- **Toutes** les lignes sont `MATCH` et `selfRead=OK` : sur cette session, ni index d'entité périmé, ni échec
  de lecture de la position du joueur. Les deux constats S2/S3 §3-4 correspondants ne sont **pas** reproduits
  ici — mais ils ne sont pas réfutés pour autant : aucun membre n'est jamais passé hors zone (`off=1` : zéro
  occurrence), et c'est précisément la condition du constat.

### 11.4 Reste ouvert

- **L'index périmé** (`off=1` + `STALE`) : non testé, demande un membre qui franchit une frontière de zone
  pendant que la sonde tourne. Une soirée d'alliance suffira.
- **Le coefficient de hauteur** : demande une seconde mesure à ~30 yalms de dénivelé.
- Les correctifs §10 (masquage hors zone, opérateur `>=` de la démo, `dist[2]` mort) restent valables et
  indépendants de tout ça.

