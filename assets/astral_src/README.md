# ASTRAL COMPASS — Kit de textures AioHUD

17 pièces × 2 palettes. PNG RGBA8, alpha droit (non prémultiplié).
Textures géométriques déterministes, sans bruit ni shaders. Les aperçus ne sont PAS des textures de production.

## Géométrie et intégration

Lire `manifest.json`. Centre des couches 1024 : (512,512).
R extérieur 500 ; trou 420 ; graduations 428–470 ; centres des pastilles 470.
Pour un diamètre d'anneau visible D en pixels, appliquer le facteur D/1000 à toutes les coordonnées et dimensions de l'anneau. Les pastilles débordent volontairement : rayon total 520 avec leur diamètre 100.
La taille du quad 1024 vaut donc 1,024 × D.

Ordre : carte officielle ronde → lentille_ombre → anneau_base → anneau_graduations → anneau_reflet → pastilles et lettres → cartouches et satellite.
Rotation : seules les graduations suivent la rotation de la carte. Placer les centres des pastilles par sin/cos sur le cercle de rayon 470, mais ne PAS tourner les sprites des pastilles/lettres. Convention JSON : angle positif horaire, 0 = nord en haut.
Reflet : SRCALPHA / ONE ; autres couches : SRCALPHA / INVSRCALPHA. Le reflet ne doit pas être redessiné en alpha classique. Restaurer les états de rendu.
Les pixels transparents proches des bords contiennent un léger débord RGB (2,5 px) pour réduire les franges dues au filtrage bilinéaire ; leur alpha reste nul. Respecter l'alpha au chargement. Ne pas prémultiplier.
Pour la version grise, luminance Rec.709 sur RGB encodé, alpha identique. La teinte multiplicative assombrit les valeurs, elle ne peut pas les éclaircir au-delà de la source.

## Cartouches

Les deux extrémités 64×128 et le milieu 32×128 ont des raccords exacts (dernière/première colonne RGBA identiques). Étirer ou répéter seulement le milieu.
Largeur totale W = 64 + largeur milieu affichée + 64 (W ≥ 128).
Zone texte : x = 38 à W−38 ; y = 26 à 102 pour le haut ; 32 à 96 pour le bas. Coordonnées en pixels de texture à l'échelle native 128.
Satellite 256 : rayon du trou 96, extérieur 120. Dessiner la lune sous son cadre.
Les aperçus montrent des positions et tailles indicatives des cartouches, qui restent libres.

## Contrôles

- 34 fichiers PNG vérifiés en RGBA 8 bits par canal, dimensions puissances de 2.
- Alpha strictement identique entre palettes ; trou central transparent.
- Raccords des 4 paires de cartouches vérifiés pixel à pixel.
- Aperçus assemblés à 200, 300 et 400 px, dont rotation +35° et palette grise.
- Le parchemin des aperçus provient d'un recadrage de la capture AioHUD fournie ; la lune est un disque indicatif. Aucun parchemin, texte d'horloge ou lune n'est incorporé aux textures de production.

Le rendu est contrôlé hors jeu, pas dans le moteur Direct3D 8. Vérifier le filtrage, les coordonnées demi-pixel et le mélange additif dans une capture du jeu.
