---
title: Layout descriptor — exports/layout.json
summary: Le contrat (en français) entre la maquette HTML/CSS et l'UI native C++ — schéma version 1, convention de coordonnées ancre+%, champs config, et le stub du loader natif.
source: EXPORT.md
---
# Layout descriptor — `exports/layout.json`

Contrat **unique** entre la maquette HTML/CSS (`design/`) et l'UI native C++ (Windower).
La maquette est l'éditeur de layout ; le natif est le moteur de rendu. Le descriptor décrit
**où** placer chaque widget, **lequel**, et **avec quels réglages** — jamais le style (le style vit
dans le code natif / un thème séparé).

Généré par le bouton **« Export »** de la barre d'édition : agrège l'état live (positions, config,
visibilité, zones) et écrit `design/exports/layout.json` (+ copie presse-papier).
Code : `design/src/app/exportLayout.js` → `buildLayout()`.

## Schéma (version 1)

```jsonc
{
  "version": 1,
  "viewport": { "w": 2560, "h": 1400 },   // résolution de référence au moment de l'export
  "widgets": [
    {
      "id": "target",            // identifiant stable de la box
      "type": "TargetBar",       // nom de la CLASSE widget native à instancier (factory)
      "anchor": "tl",            // coin d'ancrage : tl | tr | bl | br
      "pos": { "x": 36.5, "y": 49.4 },  // % du viewport, RELATIF au coin d'ancrage
      "size": { "w": 420 },      // largeur en px, ou null = auto (s'adapte au contenu)
      "z": 1,                    // ordre de superposition (croissant = au-dessus)
      "visible": true,           // masquage MANUEL de l'utilisateur
      "jobs": null,              // gating : ['SCH'] = visible seulement si main/sub ∈ liste ; null = toujours
      "growDown": false,         // true = ancrage haut forcé (le haut reste fixe, le contenu varie par le bas)
      "bare": false,             // true = pas de cadre/chrome (ex. image nue)
      "config": { /* … */ }      // réglages effectifs de la box (défauts + valeurs choisies)
    }
  ],
  "zones": [
    { "label": "Menu du jeu", "x": 94, "y": 0, "w": 6, "h": 18, "allow": [] }  // zones UI natives à éviter (%)
  ]
}
```

## Convention de coordonnées

- **`pos` en %** d'un viewport, **relatif à `anchor`** :
  - `anchor: "tl"` → `x` = % depuis la **gauche**, `y` = % depuis le **haut**.
  - `anchor: "tr"` → `x` = % depuis la **droite**, `y` = % depuis le **haut**.
  - `anchor: "bl"` / `"br"` → idem avec le **bas**.
- **`viewport`** = la résolution de référence au moment de l'export. Le natif **convertit en pixels**
  selon la résolution réelle du client (cf. [coordinates](../reference/coordinates.md)). Les % rendent le layout pérenne
  (indépendant de la résolution).
- L'ancrage permet à une box de **grandir du bon côté** quand son contenu change (une box ancrée
  `br` grandit vers le haut-gauche, donc reste collée au coin bas-droit).

## Champs `config`

`config` est l'objet de réglages résolu de chaque box (valeurs par défaut écrasées par les choix
utilisateur), **typé selon le schéma** : champ `number` → nombre, `toggle` → booléen, `select` →
string. Ex. `{ count: 6, alliances: "2" }` pour `PartyList`, `{ equip: "left", bars: "stacked",
hp: true, … }` pour `PlayerHub`. Le natif lit les clés qu'il connaît pour ce widget.

## Stub du loader natif (côté C++)

```cpp
// 1. parser layout.json (version, viewport, widgets[], zones[])
// 2. pour chaque widget : instancier via une FACTORY par `type`
//      Widget* w = WidgetFactory::create(entry.type);   // "TargetBar" -> new TargetBar()
//      if (!w) continue;                                 // type inconnu -> on saute
// 3. position : convertir (anchor + pos% ) -> pixels selon la résolution réelle
//      px = anchorOrigin(anchor, screenW, screenH) + signe(anchor) * pos% * screen
// 4. appliquer size.w (ou auto), z (tri d'affichage), visible, jobs (gating live), growDown, bare
// 5. passer `config` au widget (il lit les clés qu'il comprend)
// 6. enregistrer dans Hud::widgets_ (trié par z) ; rendre dans l'ordre
```

Le `type` est le **point de jointure** : ajouter un widget natif = implémenter une classe + l'enregistrer
dans la factory sous le même nom que le `type` de la maquette.

## Notes

- `visible` = masquage manuel uniquement ; le **gating par job** est porté à part par `jobs` (le natif
  combine : affiché si `visible && (jobs == null || jobMatch(jobs))`).
- L'export ne contient **aucun style** (couleurs, polices, géométrie interne) — c'est volontaire.
- Concerne séparé : `export.mjs`/`export.html` produisent des **captures PNG** (party/alliances), sans
  rapport avec ce descriptor.

## See also
- [Widgets](../architecture/widgets.md)
- [Coordinates](../reference/coordinates.md)
- [Edit-layout GUIDES & ZONES](../design/edit-zones.md)
- [UI composition](../tech-stack/ui-composition.md)
