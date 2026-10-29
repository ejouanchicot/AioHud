# BST box icons: prompts for an image generator (2026-10-28)

The player's rule: the BST box keeps AioHUD's theme (chrome and colours, like every module); the "nature" look comes
from the elements inside, kept simple: an informative box, not a game screen.
So every icon is a WHITE silhouette on a transparent background: the code tints it with the theme's colours (a full
charge gold, an empty one dim, a family in the value colour) and crops it for a charge filling up.

Delivery: one PNG per icon, 128 x 128, transparent background, the names below. Conversion to the box's .raw files
and the drawing code come after (scripts/gen_bst_pet_art.py style).

## Style block (paste it before every icon)

```
Flat UI icon for a fantasy MMO interface, beastmaster / nature theme.
Pure white (#FFFFFF) solid silhouette on a fully transparent background (PNG with alpha).
No shading, no gradient, no outline of another colour, no shadow, no text, no frame, no background shape.
Simple bold shapes with a slightly organic, hand-cut feel, readable at 16 to 24 pixels.
Centered, filling about 80 % of a 128 x 128 square.
```

## Icons

| File | Prompt (after the style block) | Used for |
|---|---|---|
| `bst_charge.png` | `A single animal paw print (four toes and a pad), symmetrical.` | the three Ready charges: full (theme gold), empty (dim), the one recharging filled from the bottom |
| `bst_eco_beast.png` | `The head of a wolf in side view, ears up.` | correlation: Beast |
| `bst_eco_lizard.png` | `A lizard seen from above, tail curved.` | Lizard |
| `bst_eco_vermin.png` | `A beetle seen from above, six short legs.` | Vermin |
| `bst_eco_plantoid.png` | `A leaf with a small sprout, simple veins cut out.` | Plantoid |
| `bst_eco_bird.png` | `A bird in flight seen from below, wings spread.` | Bird |
| `bst_eco_aquan.png` | `A fish in side view, simple fins.` | Aquan |
| `bst_eco_amorph.png` | `A slime blob with two small eyes cut out.` | Amorph |
| `bst_eco_undead.png` | `A skull seen from the front, simple.` | Undead |
| `bst_eco_arcana.png` | `A floating crystal with a small eye in its centre.` | Arcana |
| `bst_eco_demon.png` | `A horned demon head seen from the front, two curved horns.` | Demon |
| `bst_eco_dragon.png` | `The head of a dragon in side view, one horn, open jaw.` | Dragon |

Tip for a consistent set: ask for all of them in the same conversation, the style block first, then "same style,
next icon: ...". If one comes out with a background or a colour, ask again: "transparent background, white only".

## How the box would use them (to build once the icons exist)

- Ready: three paw prints in place of the dial's three segments (full gold, empty dim, the recharging one filling up
  from the bottom), the seconds under them; the dial's ring removed.
- Correlation: `[family icon] Beast  ▲ [Lizard icon]  ▼ [Plantoid icon]`, the names still written (the icons alone
  would not say it to a new player).
- Moves, HP, TP: unchanged (their icons and gauges already follow the theme).
