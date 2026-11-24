# Dérive des tables générées — 2026-11-24

Note d'état, **rien n'est corrigé**. Constat fait avec `python scripts/check_gen_drift.py` (24 SAME, 5 DRIFT,
1 CRASH), puis en régénérant chaque table dans un dossier temporaire (`AIOHUD_GEN_OUT`) pour la comparer à la
version suivie. Le même résultat sort à `HEAD` sans les changements du jour (S1) : ce n'est pas S1.

Sources lues (dates des fichiers de l'install de dev `D:\Windower Tetsouo\res`) : `spells.lua` 2026-11-12,
`monster_abilities.lua`, `key_items.lua`, `item_descriptions.lua` 2026-11-10, `job_abilities.lua` 2026-01-01.

## Résumé

| Table | Cause | Ce qui changerait si on régénérait | Impact en jeu **aujourd'hui** (table suivie) | Gravité |
|---|---|---|---|---|
| `bst_pets_gen.h` | **Bug du générateur** (pas les sources) | Les 50 familiers perdent **tous** leurs Ready moves (`{0}`) | Aucun : la table suivie est bonne | **Bloquant pour toute régénération** |
| `skillchain_gen.h` (CRASH) | **Même bug du générateur** | Ne se génère plus du tout | Aucun : la table suivie est bonne | **Bloquant pour toute régénération** |
| `mobskills_gen.h` | `monster_abilities.lua` mis à jour | 26 lignes : ids 3968-3983 renommés, 4032-4041 réattribués, 3646 → 3635 entrées | Noms de TP moves de mobs **peut-être faux** pour ces ids | Moyenne, à vérifier en jeu |
| `job_track_gen.h` | `spells.lua` marque Impact `unlearnable=true` | Impact sort des listes « par job » (7 lignes, −1 sur 7 jobs) | Aucun aujourd'hui ; **régénérer ferait régresser** | Moyenne (piège) |
| `keyitems_gen.h` | Nouvel objet clé dans `key_items.lua` | +1 : `{3375, "Celestite Case"}` | Cet objet clé s'afficherait sans nom | Faible |
| `song_dur_gen.h` | Description de Miracle Cheer réécrite | Miracle Cheer (22249) sort de la table | La table suivie compte encore +30 pour Miracle Cheer | Faible à moyenne, à vérifier |

## 1. `bst_pets_gen.h` et `skillchain_gen.h` : un bug des générateurs, pas une dérive des sources

- Le 2026-11-17 (commit `f53616e`), `AbilRow` a gagné un 5e champ `status` :
  `{795, "Crossthrash", 2, 102, 0}` au lieu de `{795, "Crossthrash", 2, 102}`.
- Deux générateurs lisent les lignes d'abilities avec une expression régulière écrite pour **4** champs, qui finit
  par `102\}` :
  - `scripts/gen_bst_pets.py:14-15` : `\{(\d+), "([^"]*)", (\d+), 102\}` → ne trouve plus aucune Ready move,
    donc chaque familier sort avec `{0}` (50 lignes changées, toutes vidées) ;
  - `scripts/gen_skillchain.py` (boucle EXTRA_READY) : `...\s*\d+\s*,\s*102\s*\}` → les 8 moves
    `Crossthrash, Disembowel, Extirpating Salvo, Fluid Spread, Fluid Toss, Hoof Volley, Mega Scissors,
    Rhinowrecker` sont introuvables et le script s'arrête (`sys.exit`). Elles sont pourtant bien dans
    `abilities_gen.cpp` (ex. ligne 619 : `{795, "Crossthrash", 2, 102, 0}`) et dans `res/job_abilities.lua`.
- Les tables suivies datent d'avant (`bst_pets_gen.h` / `skillchain_gen.h` : dernier commit 2026-10-29), donc
  **elles sont correctes**. Le risque est la prochaine régénération : BST perdrait l'affichage de ses Ready moves,
  et la prédiction de skillchain des familiers ne pourrait plus être régénérée.
- Correctif à faire (pas fait) : accepter le champ `status` optionnel dans les deux expressions
  (`102\s*(?:,\s*\d+\s*)?\}`), puis vérifier que les deux tables régénérées sont identiques aux suivies.

## 2. `mobskills_gen.h` : les ids de TP moves de monstres ont été réattribués

- `res/monster_abilities.lua` (2026-11-10) donne d'autres noms à 26 ids. Exemples (suivi → nouvelle source) :
  - 3968 `Spinal Cleave` → `Somnic Shell` ; 3969 `Mangle` → `Contaminating Concoction` ; 3970 `Leaping Cleave` →
    `Mog Bomb` ; 3971 `Hex Palm` → `Bill Toss` ; 3972 `Animating Wail` → `Washtub` ; 3973 → `Crystalline Flare` ;
    3974 → `Invite Friend` ;
  - les ids 3975-3983 (`Gen'ei Ryodan`, `Phantom Whorl`, `Turn the Tables`, `Triple Reversal`, `Spin the
    Tables`...) et 4032-4040 (`Pyric Blast`, `Polar Bulwark`, `Barofield`, `Nerve Gas`...) disparaissent ;
  - 4035-4041 deviennent `Slicing Sickle`, `Raptorial Claw`, `Phlegm Expulsion`, `Macerating Bile`, `Preying
    Posture`, `Mantodean Reprisal`, `Immolating Claw`.
- Total 3 646 → 3 635 entrées.
- **Impact** : partout où AioHUD nomme un TP move de mob par son id (cible, chat, skillchains), ces ids
  afficheraient l'**ancien** nom si le jeu les a vraiment réattribués (mise à jour de novembre). Ou l'inverse, si
  c'est une erreur de res.
- **À vérifier en jeu** avant de régénérer : un mob qui utilise l'un de ces moves (les noms Mog Bomb / Washtub /
  Bill Toss font penser à un événement récent), comparer le nom affiché par le jeu à celui d'AioHUD.

## 3. `job_track_gen.h` : Impact devient « non apprenable »

- `res/spells.lua` (2026-11-12) porte désormais `unlearnable=true` sur Impact (id 503), et le générateur écarte
  ces sorts (`gen_job_track.py:150`, prévu pour Dispelga).
- Régénérer retirerait les 7 lignes `{0,503,90,19,"Impact"} // TC_NUKE` et baisserait de 1 le compte de WHM, BLM,
  RDM, DRK, SMN, SCH et GEO.
- **Impact** : aucun aujourd'hui. Mais Impact reste lançable avec le Twilight Cloak ; le retirer ferait disparaître
  la ligne Impact des listes « par job » de Timers : **une régression**.
- Correctif à prévoir : traiter Impact comme Dispelga ou non (décision : faut-il suivre un sort donné par un
  équipement ?), avant toute régénération.

## 4. `keyitems_gen.h` : un nouvel objet clé

- `{3375, "Celestite Case"}` est apparu dans `key_items.lua` (3 231 → 3 232).
- **Impact** : faible. Partout où AioHUD affiche un objet clé par son nom, celui-ci apparaîtrait sans nom
  (ou sous son id) tant que la table n'est pas régénérée. Régénérer est sans risque pour cette table.

## 5. `song_dur_gen.h` : Miracle Cheer

- La table suivie compte Miracle Cheer (22249) avec `30` (« 131 items carry song duration »).
- Sa description actuelle dans `item_descriptions.lua` : *Grants one additional song effect / All Songs +3 /
  Song effect duration for party members +15 min.* Le générateur ne la reconnaît plus comme un bonus de durée
  en pourcentage, et la retire (131 → 130 objets).
- **Impact** : la durée estimée des chansons d'un BRD qui porte Miracle Cheer. Ni l'ancienne valeur (+30) ni
  l'absence ne correspondent à « +15 min pour les membres du groupe ».
- **À vérifier en jeu** avec un BRD équipé (Morphetrix) avant de toucher à la table : durée réelle d'une chanson
  sur un membre du groupe.

## Ordre conseillé (plus tard)

1. Corriger les deux expressions régulières (point 1) : sinon toute régénération casse BST et les skillchains.
2. Régénérer `keyitems_gen.h` (sans risque).
3. Décider pour Impact (point 3), puis régénérer `job_track_gen.h`.
4. Vérifier en jeu les TP moves (point 2) et Miracle Cheer (point 5), puis régénérer.
