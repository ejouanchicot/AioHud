// config_changelog.h -- the per-version Update-tab changelog, split out of config_page.cpp (PURE MOVE).
//
// This block grows by one CL_<ver> group EVERY release by design, so keeping it here means the release
// ritual stops touching the biggest file in the repo. Data only. Included ONCE, by config_page.cpp.
// NOTE: included from INSIDE `namespace aio` in config_page.cpp, so this file must NOT open the
// namespace itself (that would nest it as aio::aio::).
#pragma once

// A short, user-facing "what changed" list for THIS build -- shown under the Update controls so a player knows
// what the version they run (or just updated to) actually fixes / adds. PLAIN language, what not how ; no dev
// jargon. UPDATE this with each release. `*bold*` markup works (draw_wrapped colours it brighter).
struct ChangeLine { const char* en; const char* fr; };

// Per-version change lines. Grouped BY VERSION so the Update tab can show the newest release expanded and the
// older ones as collapsible headers. `*bold*` markup works (draw_wrapped colours it brighter).
static const ChangeLine CL_41[] = {
    { "*A memory leak that could cost ~13 MB per second is fixed.* If one of the vial images failed to load — most likely while an update was replacing it — the plugin re-created three others every single frame without ever freeing the previous ones.",
      "*Une fuite mémoire pouvant coûter ~13 Mo par seconde est corrigée.* Si l'une des images de fiole échouait à se charger — typiquement pendant qu'une mise à jour la remplaçait — le plugin recréait trois autres textures à chaque image sans jamais libérer les précédentes." },
    { "*Your settings can no longer be silently truncated.* The config file was written in place, so another client reading it at that exact moment got a half-written file, treated it as complete, and could then save that loss back to disk. It is now written fully before replacing the old one.",
      "*Vos réglages ne peuvent plus être tronqués en silence.* Le fichier de config était écrit sur place, donc un autre client le lisant à cet instant précis obtenait un fichier à moitié écrit, le considérait comme complet, et pouvait ensuite réécrire cette perte sur le disque. Il est désormais écrit entièrement avant de remplacer l'ancien." },
    { "*Chat lines and //aio commands are now handled on the game's main thread.* They were being processed on separate threads while the display was reading the same data — a race that would only ever show up as a rare, unreproducible glitch.",
      "*Les lignes de chat et les commandes //aio sont désormais traitées sur le thread principal du jeu.* Elles l'étaient sur des threads séparés pendant que l'affichage lisait les mêmes données — une situation qui ne se serait manifestée que par un défaut rare et non reproductible." },
    { "Several guards were added against malformed data : equipment slots are bounds-checked before being read, cached ally-buff names are always terminated, and the zone-map file paths can no longer overflow on a deep install path.",
      "Plusieurs protections ont été ajoutées contre des données malformées : les emplacements d'équipement sont bornés avant lecture, les noms de buffs alliés en cache sont toujours terminés, et les chemins de fichiers de cartes ne peuvent plus déborder sur une installation profonde." },
    { "Performance : the nearby-entity scan no longer runs when the minimap is hidden, and spell names for recast timers are looked up once instead of being searched every frame.",
      "Performance : le balayage des entités proches ne tourne plus quand le minimap est masqué, et les noms de sorts des minuteurs de recast sont indexés une fois au lieu d'être recherchés à chaque image." },
};
static const ChangeLine CL_40[] = {
    { "*A failed load no longer disables things until you restart.* A project-wide sweep found the same flaw in several more places : minimap markers and clock icons, the zone-map file tables (one unreadable file left EVERY map unavailable for the session), and the per-character profile list. They all retry now.",
      "*Un chargement raté ne désactive plus des éléments jusqu'au redémarrage.* Une revue de tout le projet a trouvé le même défaut à plusieurs autres endroits : les marqueurs et icônes du minimap, les tables de fichiers des cartes de zone (un seul fichier illisible rendait TOUTES les cartes indisponibles pour la session), et la liste des profils par personnage. Tous réessaient désormais." },
    { "*Profile changes from another client are no longer lost.* If the file was being written at the exact moment we read it, the change was marked as seen and that client stayed on the old settings for good.",
      "*Les modifications de profil venant d'un autre client ne sont plus perdues.* Si le fichier était en cours d'écriture au moment précis où on le lisait, la modification était marquée comme vue et ce client restait sur les anciens réglages définitivement." },
    { "*Auto profile switching on a job change* no longer gives up when the load happens to fail — it retries on the next tick instead of skipping that job change entirely.",
      "*Le changement automatique de profil au changement de job* n'abandonne plus quand le chargement échoue — il réessaie au cycle suivant au lieu d'ignorer ce changement de job." },
    { "The equipment viewer's set bonuses (Composure, enhancing duration) no longer collapse to their base value for a moment after zoning, and the pop-item tracker no longer claims you own none of them when it cannot read your inventory.",
      "Les bonus de set de l'afficheur d'équipement (Composure, durée d'amélioration) ne retombent plus à leur valeur de base un instant après un changement de zone, et le suivi d'objets de pop n'annonce plus que vous n'en possédez aucun quand il ne peut pas lire votre inventaire." },
    { "Gear icons and zone maps now look for your FFXI install in the same six registry locations. They used three and six respectively, so an install found by only one of them gave you maps but no equipment icons.",
      "Les icônes d'équipement et les cartes de zone cherchent désormais votre installation FFXI aux six mêmes emplacements de registre. Elles en utilisaient trois et six, donc une installation trouvée par une seule des deux listes donnait les cartes mais pas les icônes d'équipement." },
};
static const ChangeLine CL_39[] = {
    { "*Status icons could vanish from the Timers and Debuffs boxes.* The icon sheet was loaded with a single attempt : one badly-timed miss — typically while an update was replacing it — and every icon stayed missing for the rest of the session. It now retries.",
      "*Les icônes d'état pouvaient disparaître des boîtes Timers et Debuffs.* La planche d'icônes était chargée en une seule tentative : un échec au mauvais moment — typiquement pendant qu'une mise à jour la remplaçait — et toutes les icônes restaient absentes jusqu'à la fin de la session. Elle réessaie désormais." },
    { "*Hidden + Focus works for spells that share a buff with another spell* (BLU Cocoon and Reactor Cool both give Defense Boost). Hiding one is now recognised on its own instead of requiring every spell granting that buff to be hidden.",
      "*Caché + Focus fonctionne pour les sorts partageant un buff avec un autre* (Cocoon et Reactor Cool de BLU donnent tous deux Defense Boost). Cacher l'un est désormais reconnu seul, au lieu d'exiger que tous les sorts donnant ce buff soient cachés." },
    { "*The red \"lost buff\" alert now actually appears*, and holds for the time you set. It could never fire : once your last buff expired the plugin read the empty buff list as \"no data\" and assumed everything was still active.",
      "*L'alerte rouge de perte de buff apparaît enfin*, et tient la durée que vous avez réglée. Elle ne pouvait jamais se déclencher : dès l'expiration de votre dernier buff, le plugin lisait la liste vide comme « pas de données » et supposait tout encore actif." },
    { "The hand-off from the timer to that alert is seamless : the game keeps a buff about two seconds longer than our countdown, and the row used to disappear during the gap.",
      "Le passage du minuteur à cette alerte se fait sans rupture : le jeu conserve un buff environ deux secondes de plus que notre décompte, et la ligne disparaissait pendant cet intervalle." },
    { "*Multi-character on one Windower* : switching character on the same client no longer carries the previous one's data over — party roster, tracker run, Limbus chips, buff tags and XP/h rates are all reset together. Your saved party list is also per character now.",
      "*Multi-personnage sur un même Windower* : changer de personnage sur le même client ne reporte plus les données du précédent — liste de groupe, run du suivi de zone, chips Limbus, tags de buffs et cadences XP/h sont réinitialisés ensemble. Votre liste de groupe sauvegardée est également par personnage désormais." },
    { "*Editing a profile that several characters use now updates them all* : each client re-applies the profile when another one saves it. A client with unsaved changes is left alone.",
      "*Modifier un profil utilisé par plusieurs personnages les met tous à jour* : chaque client réapplique le profil quand un autre l'enregistre. Un client ayant des modifications non sauvegardées n'est pas touché." },
    { "*The minimap no longer stays black.* The map file was read correctly but building its image could fail on the first try after zoning, and that counted as a success — so it never tried again. It now retries, and the map panel simply waits instead of showing a black square that fills in a moment later.",
      "*Le minimap ne reste plus noir.* Le fichier de carte était lu correctement mais la construction de son image pouvait échouer à la première tentative après un changement de zone, ce qui comptait comme une réussite — il ne réessayait donc jamais. Il réessaie désormais, et le panneau attend au lieu d'afficher un carré noir qui se remplit ensuite." },
};
static const ChangeLine CL_38[] = {
    { "*Updating with two clients open no longer reports a file error.* Every client runs its own updater, and they all downloaded to the same file — so the second one failed on \"file in use\" and the Update tab showed a failure, even though the update had actually gone through. Each updater now downloads to its own file.",
      "*Mettre à jour avec deux clients ouverts ne signale plus d'erreur de fichier.* Chaque client lance son propre updater, et tous téléchargeaient vers le même fichier — le second échouait donc sur « fichier en cours d'utilisation » et l'onglet MAJ affichait un échec, alors que la mise à jour était bel et bien passée. Chaque updater télécharge désormais vers son propre fichier." },
    { "Housekeeping with no visible effect : the per-character save files added in 1.0.35 are renamed to match the naming the plugin already used elsewhere, so one character no longer appears twice in the folder. Your existing data is renamed with them — nothing is lost and nothing resets.",
      "Rangement sans effet visible : les fichiers de sauvegarde par personnage ajoutés en 1.0.35 sont renommés pour suivre la nomenclature déjà utilisée ailleurs par le plugin, afin qu'un même personnage n'apparaisse plus deux fois dans le dossier. Vos données existantes sont renommées avec eux — rien n'est perdu et rien n'est réinitialisé." },
};
static const ChangeLine CL_37[] = {
    { "*Updating with two clients open no longer reports a file error.* AioHud kept its image files open in a way that stopped the updater replacing them, so an update could fail on one of them the moment a client reloaded and drew its first frame. The files are now opened so they can be replaced while in use.",
      "*Mettre à jour avec deux clients ouverts ne signale plus d'erreur de fichier.* AioHud gardait ses fichiers d'images ouverts d'une façon qui empêchait l'updater de les remplacer, donc une mise à jour pouvait échouer sur l'un d'eux dès qu'un client se rechargeait et affichait sa première image. Ils sont désormais ouverts de manière à pouvoir être remplacés pendant leur utilisation." },
    { "Same cause, same fix for the equipment icons and the zone maps : two clients reading and writing the same icon file could collide.",
      "Même cause, même correctif pour les icônes d'équipement et les cartes de zone : deux clients lisant et écrivant le même fichier d'icône pouvaient entrer en conflit." },
};
static const ChangeLine CL_36[] = {
    { "*Switching character on the same client no longer mixes up your Limbus chips.* Each character already had its own saved file since 1.0.35, but the previous character's chips stayed in memory until the next Limbus entry — long enough to be written into the new character's file.",
      "*Changer de personnage sur le même client ne mélange plus vos chips Limbus.* Chaque personnage avait déjà son fichier depuis la 1.0.35, mais les chips du précédent restaient en mémoire jusqu'à l'entrée en Limbus suivante — assez longtemps pour être écrits dans le fichier du nouveau." },
    { "Reloading the plugin during a run no longer risks losing that run. Restoring the saved run needs the character to be identified, and right after a reload that can take a frame or two ; the restore only ever tried once, so it could fail silently. It now waits instead.",
      "Recharger le plugin pendant une run ne risque plus de la perdre. Restaurer la run sauvegardée nécessite d'identifier le personnage, ce qui peut prendre une image ou deux juste après un rechargement ; la restauration n'essayait qu'une fois et pouvait donc échouer en silence. Elle attend désormais." },
};
static const ChangeLine CL_35[] = {
    { "*Each character keeps its own tracker data again.* All characters on one Windower shared a single saved file, so the last one to save won and the next one loaded ITS values — you could see another character's Temenos units on yours. The saved data is now per character.",
      "*Chaque personnage conserve à nouveau ses propres données de suivi.* Tous les personnages d'un même Windower partageaient un seul fichier de sauvegarde : le dernier à enregistrer l'emportait et le suivant chargeait SES valeurs — vous pouviez voir les unités Temenos d'un autre personnage sur le vôtre. Les données sauvegardées sont désormais par personnage." },
    { "This affected the whole Zone Tracker, not just Limbus : Dynamis, Abyssea, Omen, Nyzul and Sheol shared the same file too.",
      "Cela concernait tout le Suivi de zone, pas seulement Limbus : Dynamis, Abyssea, Omen, Nyzul et Sheol partageaient aussi le même fichier." },
    { "Note : each character starts from an empty tracker once, because the old shared file cannot be attributed to anyone. The Limbus chip row resets a single time ; your units come back on their own at the next reward.",
      "Note : chaque personnage repart une fois d'un suivi vide, l'ancien fichier partagé ne pouvant être attribué à personne. La rangée de chips Limbus se réinitialise une seule fois ; vos unités reviennent d'elles-mêmes à la prochaine récompense." },
};
static const ChangeLine CL_34[] = {
    { "*Temenos now shows its progress bar.* It never appeared : the floor gauge is labelled differently in Temenos than in Apollyon, and only the Apollyon wording was recognised — so the area name showed while the bar stayed missing.",
      "*Temenos affiche enfin sa barre de progression.* Elle n'apparaissait jamais : la jauge d'étage est nommée autrement en Temenos qu'en Apollyon, et seule la formulation d'Apollyon était reconnue — d'où le nom de la zone affiché mais la barre absente." },
    { "*Temenos units were shown on the Apollyon line.* The live total always went to the Apollyon row whatever wing you stood in. If your client never sends the currency packet, your Temenos total could stay empty no matter how much Temenos you ran.",
      "*Les unités Temenos s'affichaient sur la ligne Apollyon.* Le total en direct partait toujours sur la ligne Apollyon quelle que soit l'aile. Si votre client n'envoie pas le paquet de monnaies, votre total Temenos pouvait rester vide quel que soit le nombre de runs Temenos." },
    { "*Coffers are no longer confused with points of interest.* Both award units with the same message, so checking a point of interest was recorded as if you had opened a coffer. The chest itself is now identified, so only real coffers count.",
      "*Les coffres ne sont plus confondus avec les points d'intérêt.* Les deux donnent des unités avec le même message, donc checker un point d'intérêt était enregistré comme l'ouverture d'un coffre. Le coffre lui-même est désormais identifié, et seuls les vrais coffres comptent." },
    { "Temenos coffers were never recorded at all — the tower name in the game's data did not match what was expected, so every Temenos coffer was silently dropped.",
      "Les coffres de Temenos n'étaient jamais enregistrés — le nom de la tour dans les données du jeu ne correspondait pas à ce qui était attendu, donc chaque coffre Temenos était ignoré en silence." },
    { "Temenos tower labels corrected to *N4 W4 E4 C3* (they read N7 W7 E7 C4), the floor is no longer cut to three letters (\"Nor #1\" -> \"North #1\"), and the weekly \"you may collect data N more times\" counter is now read.",
      "Libellés des tours de Temenos corrigés en *N4 W4 E4 C3* (ils affichaient N7 W7 E7 C4), l'étage n'est plus tronqué à trois lettres (« Nor #1 » -> « North #1 »), et le compteur hebdomadaire « you may collect data N more times » est désormais lu." },
};
static const ChangeLine CL_33[] = {
    { "*Equipment showing item numbers instead of icons is fixed.* When a piece wasn't among the icons shipped with AioHud, it was rebuilt from the game's own files — and that worked. But the rebuilt icon was then saved for next time, and *if that save failed, the icon was thrown away* and its number shown instead. Saving fails on installs where the folder is write-protected, typically FFXI under Program Files. The icon is now displayed first and saved afterwards ; a failed save just means it gets rebuilt next session.",
      "*L'équipement qui affichait des numéros au lieu des icônes est corrigé.* Quand une pièce ne faisait pas partie des icônes livrées avec AioHud, elle était reconstruite depuis les fichiers du jeu — et ça fonctionnait. Mais l'icône reconstruite était ensuite enregistrée pour la fois suivante, et *si cet enregistrement échouait, l'icône était jetée* et son numéro affiché à la place. L'enregistrement échoue sur les installations où le dossier est protégé en écriture, typiquement FFXI sous Program Files. L'icône est désormais affichée d'abord et enregistrée ensuite ; un enregistrement raté signifie simplement qu'elle sera reconstruite à la prochaine session." },
    { "This mostly affected players whose gear differs from the icons bundled with AioHud — the more of your equipment was missing from that set, the more numbers you saw.",
      "Cela touchait surtout les joueurs dont l'équipement diffère des icônes fournies avec AioHud — plus votre matériel manquait à ce lot, plus vous voyiez de numéros." },
    { "An icon whose save was interrupted (game closed at the wrong moment, disk full) left a damaged file behind that broke that item's icon *permanently*, even after reinstalling. Saving is now all-or-nothing, so a damaged file can no longer be created — and an existing one repairs itself.",
      "Une icône dont l'enregistrement était interrompu (jeu fermé au mauvais moment, disque plein) laissait un fichier abîmé qui cassait l'icône de cet objet *définitivement*, même après réinstallation. L'enregistrement est désormais tout-ou-rien, donc un fichier abîmé ne peut plus être créé — et un fichier déjà abîmé se répare tout seul." },
    { "Equipping a full set of unseen gear no longer causes a brief stutter : the icons are now rebuilt a couple per frame instead of all sixteen at once.",
      "Équiper un set entier de pièces jamais vues ne provoque plus de saccade brève : les icônes sont désormais reconstruites deux par image au lieu des seize d'un coup." },
};
static const ChangeLine CL_32[] = {
    { "*Scrolling works again in the config window.* It broke in 1.0.29 : the fix that stopped the game's cursor showing over the overlay was swallowing the mouse wheel before the config could see it. Sorry — that one was on us.",
      "*Le défilement fonctionne à nouveau dans la fenêtre de config.* Il était cassé depuis la 1.0.29 : le correctif qui empêchait le curseur du jeu d'apparaître sur l'overlay avalait la molette avant que la config puisse la voir. Désolé, celle-là vient de nous." },
    { "*Multi-client (dual-box) updates* : after an update, every running client comes back on its own. Until now only the first one reloaded and the others had to be started by hand with //load AioHud — they were all watching the same signal, and the fastest one consumed it.",
      "*Mises à jour en multi-client (dual-box)* : après une mise à jour, chaque client relancé revient tout seul. Jusqu'ici seul le premier se rechargeait et les autres devaient être relancés à la main via //load AioHud — ils surveillaient tous le même signal, et le plus rapide le consommait." },
    { "Note : this fix travels with the update itself, so THIS update still leaves the other client to reload by hand one last time. The ones after it are clean.",
      "Note : ce correctif voyage avec la mise à jour elle-même, donc CELLE-CI laissera encore l'autre client à recharger à la main une dernière fois. Les suivantes seront propres." },
};
static const ChangeLine CL_31[] = {
    { "Truncated text now ends with \"...\" everywhere. The Hate List used two dots while the party name and spell used three.",
      "Le texte tronqué se termine partout par « ... ». La liste de haine en utilisait deux, alors que le nom et le sort du groupe en utilisaient trois." },
    { "Removed the *Cursor* option, which did nothing : AioHud's pointer was always drawn regardless. Since the config window now hides the game's own pointer, turning the option off would have left you with no cursor at all.",
      "Retiré l'option *Curseur*, qui ne faisait rien : le pointeur d'AioHud était dessiné dans tous les cas. Comme la fenêtre de config masque désormais le pointeur du jeu, désactiver l'option n'aurait laissé aucun curseur." },
    { "Housekeeping with no visible effect : several drawing helpers existed in four to seven copies that had started to disagree with each other, and are now single shared ones. Icon edges also stop picking up a stray coloured fringe from the box frame behind them.",
      "Rangement sans effet visible : plusieurs fonctions de dessin existaient en quatre à sept exemplaires qui commençaient à diverger, et n'en font plus qu'une. Les bords d'icônes cessent aussi d'attraper un liseré coloré parasite venu du cadre derrière elles." },
};
static const ChangeLine CL_30[] = {
    { "In *Help > Zone Tracker*, the six zone examples stayed on one row again : the last one dropped to a second line after visiting another Help page. They were being laid out against the width reserved for reading text rather than the panel's real width, leaving them 16 pixels from wrapping.",
      "Dans *Aide > Suivi de zone*, les six exemples de zones tiennent à nouveau sur une seule rangée : le dernier passait à la ligne après un passage sur une autre rubrique. Ils étaient disposés selon la largeur réservée à la lecture du texte plutôt que celle du panneau, à 16 pixels du basculement." },
    { "Every module box is a touch crisper : their frames were drawn at fractional pixel positions, which softened borders and bars. Text was never affected, which is why it read as \"slightly blurry\" rather than as a bug.",
      "Chaque boîte de module est un peu plus nette : leurs cadres étaient dessinés à des positions de pixel fractionnaires, ce qui adoucissait bordures et barres. Le texte n'était jamais concerné, d'où l'impression de « léger flou » plutôt que de défaut." },
};
static const ChangeLine CL_29[] = {
    { "The game's own mouse pointer no longer appears next to AioHud's over the config window when you come back from another application — it used to show up and flicker as soon as the mouse touched the game, even before clicking. AioHud's pointer now also follows the mouse over the game window while another window has focus.",
      "Le pointeur du jeu n'apparaît plus à côté de celui d'AioHud sur la fenêtre de config quand tu reviens d'une autre application — il se montrait et clignotait dès que la souris touchait le jeu, avant même de cliquer. Le pointeur d'AioHud suit désormais aussi la souris au-dessus du jeu pendant qu'une autre fenêtre a le focus." },
    { "*Party distance colours* : the Close / Normal / Far pickers each control their own colour again. Changing one used to drag the others with it.",
      "*Couleurs de distance Party* : les sélecteurs Proche / Normale / Loin pilotent à nouveau chacun leur couleur. Modifier l'un entraînait les autres." },
    { "*Hate List* no longer jiggles : the box now reserves room for the longest possible name from the start, so a mob switching target — or a distance ticking — cannot resize it and shift its left edge.",
      "*Liste de haine* ne saute plus : le cadre réserve d'emblée la place du nom le plus long possible, donc un mob qui change de cible — ou une distance qui varie — ne peut plus le redimensionner ni décaler son bord gauche." },
    { "*Reset all settings* now really resets everything : the Timers module was skipped entirely, so its position, size, fused mode, typography and per-job tracking lists survived a factory reset.",
      "*Réinitialiser tous les réglages* remet vraiment tout à zéro : le module Timers était entièrement oublié, donc sa position, sa taille, son mode fusionné, sa typographie et ses listes de suivi par job survivaient à une remise à zéro." },
    { "Several fixes you should never notice, found by a full code audit : a crash that could hit the game when reopening the Help tab after changing zone, the Sheol box showing the sample monster's resistances after opening the config, the target's damage trail glowing instead of staying solid, and the Limbus gauge caption spilling outside its frame.",
      "Plusieurs corrections que tu ne devrais jamais remarquer, issues d'un audit complet du code : un plantage possible du jeu en rouvrant l'aide après un changement de zone, la box Sheol affichant les résistances du monstre d'exemple après ouverture de la config, la traînée de dégâts de la cible qui brillait au lieu de rester nette, et la légende de la jauge Limbus qui débordait de son cadre." },
};
static const ChangeLine CL_28[] = {
    { "*Limbus* joins the Zone Tracker. In Apollyon or Temenos the box shows the area and its level, the floor you are on with its progress gauge, your Temenos and Apollyon units, what the run has banked so far, and how many data collections you have left this week.",
      "*Limbus* rejoint le Suivi de zone. Dans Apollyon ou Temenos, le cadre affiche la zone et son niveau, l'étage où tu es avec sa jauge de progression, tes units Temenos et Apollyon, ce que le run a rapporté jusque-là, et le nombre de collectes de données qu'il te reste cette semaine." },
    { "Limbus coffers get a row of dots, one per quadrant : dim when you have not opened it, red for a 3k, green for a 5k. Reopening a quadrant updates its dot, and finding a 5k clears the others while keeping the green one — so you can see at a glance where the good one was. The row is kept per zone and survives restarts.",
      "Les coffres Limbus ont une rangée de pastilles, une par quadrant : éteinte tant que tu ne l'as pas ouvert, rouge pour un 3k, verte pour un 5k. Rouvrir un quadrant met sa pastille à jour, et trouver un 5k efface les autres en gardant la verte — tu vois d'un coup d'œil où était le bon. La rangée est gardée par zone et survit aux redémarrages." },
    { "The whole Zone Tracker is now yours to arrange : for every zone (Dynamis, Abyssea, Omen, Nyzul, Sheol, Limbus) each line can be hidden on its own, each piece of text has its own font, size, outline and colour, and the bars, dots and icons can be resized. Pick the zone with *Content* at the top of the panel — it drives both the preview and the options shown below.",
      "Le Suivi de zone s'arrange entièrement : pour chaque zone (Dynamis, Abyssea, Omen, Nyzul, Sheol, Limbus) chaque ligne se masque séparément, chaque texte a sa police, sa taille, son contour et sa couleur, et les barres, pastilles et icônes se redimensionnent. Choisis la zone avec *Contenu* en haut du panneau — il pilote l'aperçu et les options affichées en dessous." },
    { "Party members can be coloured by distance (close / normal / far), and the detached equipment box can have its own frame.",
      "Les membres du groupe peuvent être colorés selon la distance (proche / normale / loin), et la box d'équipement détachée peut avoir son propre cadre." },
    { "Note for those who had styled the Zone Tracker : its rows now each carry their own text style instead of sharing *Body*, so a custom *Body* setting falls back to the default once on those lines.",
      "Note pour ceux qui avaient stylisé le Suivi de zone : ses lignes ont désormais chacune leur propre style de texte au lieu de partager *Corps*, donc un réglage *Corps* personnalisé revient une fois au défaut sur ces lignes." },
};
static const ChangeLine CL_27[] = {
    { "Fixed the version headers in this changelog spilling over the update card (and the footer) while scrolling.",
      "Corrig\xC3\xA9 les en-t\xC3\xAAtes de version de ce changelog qui d\xC3\xA9""bordaient sur la carte de mise \xC3\xA0 jour (et le pied de page) pendant le d\xC3\xA9""filement." },
};
static const ChangeLine CL_26[] = {
    { "Minimap gains four options : a copper-bezel width slider, a cardinal-marks size slider, a No-bezel toggle (just the round lens), and a square-frame border width (which now grows OUTWARD). The target line on the minimap is coloured like the target's own marker.",
      "La minimap gagne quatre options : un curseur de largeur de l'anneau cuivre, un curseur de taille des points cardinaux, un bouton Sans-anneau (juste la lentille ronde), et une largeur de bordure du cadre carr\xC3\xA9 (qui grandit vers l'EXT\xC3\x89RIEUR). Le trait vers la cible prend la couleur de la puce de la cible." },
    { "Target debuffs : the detached box lays out in two columns past 16, the in-box list goes up to 32, and the Max is reflected live in the config preview.",
      "D\xC3\xA9""buffs de cible : la box d\xC3\xA9tach\xC3\xA9""e passe en deux colonnes au-del\xC3\xA0 de 16, la liste dans la cible monte jusqu'\xC3\xA0 32, et le Max se refl\xC3\xA8te en direct dans l'aper\xC3\xA7u." },
    { "Party fixes: HP turns yellow below 75% like the game; the member buff strip no longer overflows the config preview when Max Buffs is high; the Gil toggle now lives in Content (or Equipment when the gear is detached); Speed keeps its own row when Name/Level are hidden; the minimap retries loading on a zone-in; and the Treasure Pool clears on a zone change.",
      "Corrections Party : les PV passent au jaune sous 75 % comme le jeu ; le bandeau de buffs des membres ne d\xC3\xA9""borde plus de l'aper\xC3\xA7u quand Max Buffs est haut ; le bouton Gil est d\xC3\xA9sormais dans Contenu (ou \xC3\x89quipement si l'\xC3\xA9quipement est d\xC3\xA9tach\xC3\xA9) ; Speed garde sa propre ligne quand Nom/Niveau sont masqu\xC3\xA9s ; la minimap r\xC3\xA9""essaie de charger au changement de zone ; et le Treasure Pool se vide au changement de zone." },
    { "Removed the vestigial Cursor option ; and the game's native cursor no longer reappears on top of AioHud's own pointer when you move the mouse back into the window with the config open.",
      "Retir\xC3\xA9 l'option Curseur devenue inutile ; et le curseur natif du jeu ne r\xC3\xA9""appara\xC3\xAet plus par-dessus le pointeur d'AioHud quand tu ram\xC3\xA8nes la souris dans la fen\xC3\xAAtre avec la config ouverte." },
};
static const ChangeLine CL_25[] = {
    { "Crowd-control debuffs now clear from what the mob DOES : any DoT or hit from anyone wakes Sleep ; the mob taking an action clears Sleep / Petrification / Stun / Terror ; and casting a spell clears Silence. So a mob you can see act no longer keeps a stale icon.",
      "Les d\xC3\xA9""buffs de contr\xC3\xB4le se retirent d\xC3\xA9sormais selon ce que FAIT le mob : un DoT ou un coup de n'importe qui r\xC3\xA9veille le Sleep ; le mob qui agit enl\xC3\xA8ve Sleep / P\xC3\xA9trification / Stun / Terror ; et lancer un sort enl\xC3\xA8ve Silence. Un mob qu'on voit agir ne garde plus d'ic\xC3\xB4ne p\xC3\xA9rim\xC3\xA9""e." },
};
static const ChangeLine CL_24[] = {
    { "Sleep debuffs (Sleep / Lullaby) now clear the instant the mob wakes -- hit, DoT (Requiem / Dia), or a natural wear-off -- read from the game's own \"no longer asleep\" message. And recasting a sleep that has No Effect no longer resets its timer to full.",
      "Les d\xC3\xA9""buffs de sommeil (Sleep / Lullaby) dispara\xC3\xAEssent d\xC3\xA9sormais d\xC3\xA8s que le mob se r\xC3\xA9veille -- coup, DoT (Requiem / Dia) ou expiration naturelle -- lu depuis le message \xC2\xAB no longer asleep \xC2\xBB du jeu. Et relancer un sommeil sans effet ne remet plus son timer \xC3\xA0 fond." },
    { "A debuff timer past its estimate now counts NEGATIVE (-0:30) instead of showing \"???\", so you see how far over it is.",
      "Un timer de d\xC3\xA9""buff au-del\xC3\xA0 de son estimation compte maintenant en N\xC3\x89GATIF (-0:30) au lieu d'afficher \xC2\xAB ??? \xC2\xBB, pour voir de combien il d\xC3\xA9""passe." },
};
static const ChangeLine CL_23[] = {
    { "Fixed target debuffs vanishing when you AoE a pack of same-name mobs -- the tracker ran out of room and two mobs ended up sharing one slot, so each cast wiped the other's box. It now holds a whole pack, and each mob keeps its own debuffs.",
      "Corrig\xC3\xA9 les d\xC3\xA9""buffs de cible qui disparaissaient en AoE sur un pack de mobs du m\xC3\xAAme nom -- le suivi manquait de place et deux mobs partageaient un emplacement, donc chaque cast effa\xC3\xA7""ait la box de l'autre. Il encaisse maintenant un pack entier, chaque mob garde ses propres d\xC3\xA9""buffs." },
    { "The detached Equipment grid (and its gil) now shows in the Player config Live Preview, stacked under the Hub like the detached debuffs under the target.",
      "La grille d'\xC3\xA9quipement d\xC3\xA9tach\xC3\xA9""e (et ses gils) appara\xC3\xAet maintenant dans l'aper\xC3\xA7u de la config Player, empil\xC3\xA9""e sous le Hub comme les d\xC3\xA9""buffs d\xC3\xA9tach\xC3\xA9s sous la cible." },
};
static const ChangeLine CL_22[] = {
    { "Fixed for real the doubled typing inside AioHud's fields on some keyboards -- the earlier fix relied on a value that a few Windower builds fill with garbage ; press/release is now read from a signal that holds on every setup.",
      "Corrig\xC3\xA9 pour de bon la saisie doubl\xC3\xA9""e dans les champs d'AioHud sur certains claviers -- l'ancien correctif se fiait \xC3\xA0 une valeur que quelques versions de Windower remplissent de d\xC3\xA9""chet ; l'appui/rel\xC3\xA2""chement est d\xC3\xA9sormais lu sur un signal fiable partout." },
};
static const ChangeLine CL_21[] = {
    { "*Target debuffs* can now be DETACHED into their own list (Target > Debuffs > Standalone) -- a mob-name header then icon / name / timer rows, your debuffs in gold, others' in white. Icon / Name / Icon+Name display, place it with //aio edit.",
      "*Les d\xC3\xA9""buffs de la cible* peuvent \xC3\xAAtre D\xC3\x89TACH\xC3\x89S dans leur propre liste (Target > Debuffs > Autonome) -- nom du mob puis lignes ic\xC3\xB4ne / nom / timer, tes d\xC3\xA9""buffs en or, ceux des autres en blanc. Affichage Ic\xC3\xB4ne / Nom / Ic\xC3\xB4ne+Nom, place-la avec //aio edit." },
    { "*Border On/Off* : every box can now hide its frame border while keeping the background (config > any module > Box > Border).",
      "*Bordure On/Off* : chaque cadre peut masquer son contour tout en gardant le fond (config > un module > Cadre > Bordure)." },
    { "*Target distance* : a new Minimal display -- just the number, coloured by its range zone -- next to Speed / TH, instead of the full gauge.",
      "*Distance de la cible* : un nouvel affichage Minimal -- juste le nombre, color\xC3\xA9 selon sa zone de port\xC3\xA9""e -- \xC3\xA0 c\xC3\xB4t\xC3\xA9 de Speed / TH, au lieu de la jauge compl\xC3\xA8te." },
    { "The game no longer sees the mouse OR the numpad-End key while the config is open, and the Hide key is now the dedicated End only.",
      "Le jeu ne re\xC3\xA7oit plus la souris NI la touche Fin du pav\xC3\xA9 num\xC3\xA9rique quand la config est ouverte, et la touche masquer est d\xC3\xA9sormais la touche Fin d\xC3\xA9""di\xC3\xA9""e uniquement." },
    { "Fixed : the mouse wheel now resizes every floating box in //aio edit (it did nothing before).",
      "Corrig\xC3\xA9 : la molette redimensionne maintenant chaque cadre flottant dans //aio edit (elle ne faisait rien avant)." },
};

static const ChangeLine CL_42[] = {
    { "Timers : your songs no longer vanish when a Trust bard sings over them. All marches share one status id, so a Trust's cast used to steal the attribution and the buff-source filter then hid YOUR own song while it was plainly still up.",
      "Timers : vos chants ne disparaissent plus quand un trust barde chante par-dessus. Toutes les marches partagent un identifiant de statut, donc l'incantation d'un trust volait l'attribution et le filtre de source masquait VOTRE propre chant alors qu'il tournait toujours." },
    { "Timers : a buff someone else put on you now shows WHO cast it, and Trust songs show their real name and tier instead of borrowing yours. Rolls name their caster too.",
      "Timers : un buff pose par quelqu'un d'autre indique desormais QUI l'a lance, et les chants des trusts portent leur vrai nom et leur palier au lieu d'emprunter les votres. Les rolls nomment aussi leur lanceur." },
    { "Timers : the Duration column is ordered your own casts first, then real party players, then Trusts -- each block still sorted soonest-to-expire.",
      "Timers : la colonne Duree est classee vos propres lancers d'abord, puis les vrais joueurs de l'equipe, puis les trusts -- chaque bloc restant trie par expiration la plus proche." },
    { "Timers : song tiers, modifier tags and casters now survive //unload + //load, even after several minutes. The cache is written on unload and no longer discarded after two minutes.",
      "Timers : les paliers de chants, les marqueurs et les lanceurs survivent desormais a //unload + //load, meme apres plusieurs minutes. Le cache est ecrit au dechargement et n'est plus jete au bout de deux minutes." },
    { "Fixed : status icons could be missing for a whole session on a machine left running more than 24 days -- the retry that reloads the icon atlas never armed.",
      "Corrige : les icones de statut pouvaient manquer toute une session sur une machine allumee depuis plus de 24 jours -- la relance qui recharge l'atlas ne s'armait jamais." },
    { "Fixed : dragging the Timers row-spacing slider rewrote the whole config file every frame, making it stutter.",
      "Corrige : faire glisser le curseur d'espacement des lignes de Timers reecrivait tout le fichier de config a chaque image, ce qui le rendait saccade." },
    { "Fixed : one field of the action packet was read on 10 bits instead of 6 (confirmed against the game's own parser), which could inflate the target count of an area spell.",
      "Corrige : un champ du paquet d'action etait lu sur 10 bits au lieu de 6 (confirme face a l'analyseur du jeu), ce qui pouvait gonfler le nombre de cibles d'un sort de zone." },
    { "Minimap : alliance members now have their own colour instead of sharing the party one -- read from the client's own party/alliance flags on the entity.",
      "Minimap : les membres d'alliance ont desormais leur propre couleur au lieu de partager celle de l'equipe -- lue directement sur les drapeaux party/alliance du client." },
};

static const ChangeLine CL_43[] = {
    { "Buff countdowns now match the game's own display to the second. They were running 2-3 s low: we derived the current time from the Windows clock, while the client keeps its own counter re-synced from the server on every zone -- so any drift between your PC and the server landed on every timer.",
      "Les decomptes de buffs correspondent desormais a la seconde pres a ceux du jeu. Ils accusaient 2 a 3 s de retard : nous calculions l'heure courante depuis l'horloge Windows, alors que le client tient son propre compteur resynchronise sur le serveur a chaque zonage -- toute derive entre ton PC et le serveur atterrissait donc sur chaque minuteur." },
    { "Same fix, second cause : remaining time was rounded down where the game rounds up, costing one further second on almost every row.",
      "Meme correctif, seconde cause : le temps restant etait arrondi a l'inferieur la ou le jeu arrondit au superieur, ce qui coutait une seconde de plus sur presque chaque ligne." },
    { "Aftermath is now attributed to you rather than left without an owner -- it can only ever come from your own weaponskill, and the game announces no cast for it.",
      "Aftermath t'est desormais attribue au lieu de rester sans proprietaire -- il ne peut venir que de ton propre weaponskill, et le jeu n'en annonce aucune incantation." },
};

static const ChangeLine CL_44[] = {
    { "Timers : a song another player casts on you now shows the job abilities that were up when they cast it -- Nightingale, Troubadour, Soul Voice, Marcato -- and a Corsair's roll shows Crooked Cards. Read from the party-buff cache, so it works for party members (not alliance).",
      "Timers : un chant qu'un autre joueur vous pose affiche desormais les aptitudes qui etaient actives quand il l'a lance -- Nightingale, Troubadour, Soul Voice, Marcato -- et un roll de Corsair affiche Crooked Cards. Lu depuis le cache de buffs du groupe, donc valable pour les membres de votre equipe (pas l'alliance)." },
    { "Timers : ability tags are amber like your own, the caster's name is grey beside them -- what was active and who cast it are two different things to read.",
      "Timers : les marqueurs d'aptitudes sont en ambre comme les votres, le nom du lanceur en gris a cote -- ce qui etait actif et qui l'a lance sont deux informations differentes a lire." },
    { "Fixed : another player's songs showed only a generic status name, with no tier and no tags. A safety check meant to stop a trust claiming your long song was rejecting real players' songs too, since we predicted their duration without knowing their Troubadour. It now applies to trusts only, and a party member's song duration accounts for the abilities they had up.",
      "Corrige : les chants d'un autre joueur n'affichaient qu'un nom de statut generique, sans palier ni marqueur. Un controle de securite cense empecher un trust de revendiquer votre long chant rejetait aussi les chants des vrais joueurs, car nous predisions leur duree sans connaitre leur Troubadour. Il ne s'applique plus qu'aux trusts, et la duree d'un chant d'un membre du groupe tient compte des aptitudes qu'il avait actives." },
};

static const ChangeLine CL_45[] = {
    { "Target : the HP bar is now a flowing liquid vial tinted by the target's allegiance -- a boiling blood-red for a monster, its own vivid hue for a player or NPC, every colour at one consistent intensity. The whole box (HP, sub-target, distance) shares the same recessed glass frame.",
      "Cible : la barre HP est desormais une fiole de liquide en mouvement, teintee selon l'allegeance de la cible -- un rouge sang bouillonnant pour un monstre, sa propre teinte vive pour un joueur ou un PNJ, toutes a la meme intensite. Toute la boite (HP, sous-cible, distance) partage le meme cadre en verre." },
    { "Timers : Scholar stratagems now show \"Stratagem\" with the number of charges available and the time to the next one, instead of always showing \"Penury\" (they all share one recast).",
      "Timers : les stratagemes de Scholar affichent desormais \"Stratagem\" avec le nombre de charges disponibles et le temps avant la prochaine, au lieu d'afficher toujours \"Penury\" (ils partagent tous un seul recast)." },
    { "Timers : a song's SV / NT / TR / M tags no longer vanish a few minutes in on a long (Troubadour'd) song -- the tag now lasts as long as the song itself.",
      "Timers : les marqueurs SV / NT / TR / M d'un chant ne disparaissent plus au bout de quelques minutes sur un chant long (sous Troubadour) -- le marqueur tient desormais aussi longtemps que le chant." },
    { "Party : a dead member stays in place with all their info and HP 0, shown in a pulsing red instead of being greyed out as if in another zone. Death and out-of-zone are now told apart by the member's actual zone, not by missing HP.",
      "Party : un membre mort reste a sa place avec toutes ses infos et HP 0, affiche en rouge pulsant au lieu d'etre grise comme s'il etait dans une autre zone. Mort et hors-zone sont desormais distingues par la vraie zone du membre, pas par l'absence de HP." },
    { "Fixed : damage job abilities (Jump, High Jump, Weapon Bash, Shield Bash...) no longer trigger the weaponskill popup as if they were a WS -- they share an id with real weaponskills, so they are now told apart by the action message.",
      "Corrige : les aptitudes de degats (Jump, High Jump, Weapon Bash, Shield Bash...) ne declenchent plus le popup de weaponskill comme si c'en etait un -- elles partagent un identifiant avec de vrais WS, elles sont donc desormais distinguees par le message d'action." },
};

static const ChangeLine CL_46[] = {
    { "Equipment viewer : a gear icon that failed to decode from the game's ROM on the FIRST touch -- which happens on a Program Files install when antivirus or Controlled Folder Access briefly blocks the file -- no longer sticks as a raw item id for the whole session. The failed slots are now retried for ~15 seconds before giving up, and the give-up is logged.",
      "Visualiseur d'equipement : une icone qui n'a pas pu etre decodee depuis la ROM du jeu au PREMIER acces -- ce qui arrive sur une installation en Program Files quand l'antivirus ou Controlled Folder Access bloque brievement le fichier -- ne reste plus bloquee en identifiant brut pour toute la session. Les emplacements en echec sont desormais reessayes pendant ~15 secondes avant d'abandonner, et l'abandon est journalise." },
};

static const ChangeLine CL_47[] = {
    { "Hardening pass across every module : a texture or icon that failed to load on a bad-timed frame (device not ready at zone-in, an asset briefly held by the updater or antivirus) no longer stays missing for the whole session. The buff-icon atlas, party / player / target / grimoire / zone-tracker / treasure icons, and the FFXI window skin all retry now.",
      "Passe de durcissement sur tous les modules : une texture ou une icone qui n'a pas pu se charger sur une image mal synchronisee (peripherique pas pret au changement de zone, un fichier brievement tenu par la mise a jour ou l'antivirus) ne reste plus absente toute la session. L'atlas d'icones de buffs, les icones party / player / cible / grimoire / suivi de zone / tresor, et le skin de fenetre FFXI reessaient desormais." },
    { "Fixed : the moon on the minimap clock could stay a blank hole for up to an hour if its image failed to build on a bad frame.",
      "Corrige : la lune de l'horloge du minimap pouvait rester un trou vide jusqu'a une heure si son image echouait a se construire sur une mauvaise image." },
    { "Fixed : your Corsair roll pips, bard song tags and buff casters could all be lost for the session if the cache file was momentarily locked at //unload+//load. It retries now.",
      "Corrige : les pips de rolls Corsair, les marqueurs de chants barde et les lanceurs de buffs pouvaient tous etre perdus pour la session si le fichier cache etait momentanement verrouille au //unload+//load. Il reessaie desormais." },
    { "Fixed : a false red buff \"OUT\" alert could flash forever for an alliance member (or a party member out of zone), whose buffs the game simply does not report to us -- \"no data\" was read as \"buff gone\".",
      "Corrige : une fausse alerte rouge de buff \"OUT\" pouvait clignoter indefiniment pour un membre d'alliance (ou un membre de party hors zone), dont le jeu ne nous transmet simplement pas les buffs -- \"pas de donnees\" etait lu comme \"buff parti\"." },
    { "Fixed : the Hate list could silently lose rows on multi-target actions (the target list was read with a fixed stride that breaks when an earlier target crits or procs).",
      "Corrige : la liste de haine pouvait perdre des lignes en silence sur les actions multi-cibles (la liste des cibles etait lue avec un pas fixe qui casse des qu'une cible precedente fait un critique ou un proc)." },
    { "New command : //aio selfcheck writes the health of every texture/icon load to aiohud_debug.log (used to verify the fixes above).",
      "Nouvelle commande : //aio selfcheck ecrit la sante de chaque chargement de texture/icone dans aiohud_debug.log (sert a verifier les corrections ci-dessus)." },
};

static const ChangeLine CL_48[] = {
    { "Fixed : the Treasure Pool box could appear with no pool in the game for about a minute. The pool is now checked against the game's own treasure window every frame, so a leftover item can no longer linger.",
      "Corrige : la boite Treasure Pool pouvait s'afficher sans pool dans le jeu pendant environ une minute. Le pool est desormais compare a la fenetre tresor du jeu a chaque image, donc un objet residuel ne peut plus trainer." },
    { "Fixed : the config scrollbars could not be grabbed -- only the mouse wheel scrolled. You can now drag the scrollbar to move through the options, Help, Update and Debug tabs.",
      "Corrige : les barres de defilement de la config ne pouvaient pas etre attrapees -- seule la molette faisait defiler. On peut desormais glisser la barre pour parcourir les onglets options, Aide, Mise a jour et Debug." },
    { "Fixed (edit layout) : when boxes overlap, only the top one now reacts to the mouse, and the top toolbar always keeps priority -- a box parked underneath no longer steals its clicks.",
      "Corrige (edition de la mise en page) : quand des boites se chevauchent, seule celle du dessus reagit a la souris, et la barre du haut garde toujours la priorite -- une boite garee dessous ne vole plus ses clics." },
    { "New : a Debug tab in the config (next to Update) lists the known bugs and the planned work, kept up to date.",
      "Nouveau : un onglet Debug dans la config (a cote de Mise a jour) liste les bugs connus et le travail prevu, tenu a jour." },
};

static const ChangeLine CL_62[] = {
    { "*Buffs you cast on your whole party at once now group instantly.* A Protect, Regen or similar spell cast as an area effect -- under Scholar's Accession or Paladin's Majesty -- shows as \"(AoE N)\" right away, instead of briefly listing one party member by name for about a second before grouping.",
      "*Les buffs que vous lancez sur tout votre groupe d'un coup se regroupent instantanement.* Un Protect, Regen ou sort similaire lance en zone -- sous Accession (Erudit) ou Majesty (Paladin) -- s'affiche directement en \"(AoE N)\", au lieu de lister brievement un membre du groupe par son nom pendant environ une seconde avant de se regrouper." },
};
static const ChangeLine CL_61[] = {
    { "*The Zone Tracker size slider no longer rewrites your settings file while you drag it.* Like every other slider, it now saves once when you let go, instead of about 60 times a second during the drag. Behind the scenes several config controls were also unified -- no change to how they work.",
      "*Le curseur de taille du Zone Tracker ne reecrit plus votre fichier de reglages pendant que vous le glissez.* Comme tous les autres curseurs, il enregistre desormais une seule fois au relachement, au lieu d'environ 60 fois par seconde pendant le glissement. En coulisses, plusieurs controles de config ont aussi ete unifies -- sans changement de fonctionnement." },
};
static const ChangeLine CL_60[] = {
    { "*More packet guards (continued).* The truncated-packet protection added in 1.0.58 now also covers the PointWatch handlers (XP / CP / Merits / Master Level). Purely defensive : no change to normal behaviour.",
      "*Plus de protections paquets (suite).* La protection contre les paquets tronques ajoutee en 1.0.58 couvre maintenant aussi les handlers PointWatch (XP / CP / Merites / Master Level). Purement defensif : aucun changement en fonctionnement normal." },
};
static const ChangeLine CL_59[] = {
    { "Small fix : the gold bullets in the Update and Debug tabs no longer spill outside the list when you scroll -- only the bullet markers were affected (the text was already clipped to the box).",
      "Petit correctif : les puces dorees des onglets Mise a jour et Debug ne debordent plus hors de la liste quand on fait defiler -- seules les puces etaient concernees (le texte etait deja clippe a la box)." },
};
static const ChangeLine CL_58[] = {
    { "*More guards against malformed packets.* Several Zone Tracker packet handlers (Odyssey / Limbus / Nyzul) now reject a truncated packet instead of reading stale bytes past its real end -- the same length check the rest of the packet handlers already do. Purely defensive : no change to normal behaviour.",
      "*Plus de protections contre les paquets malformes.* Plusieurs handlers de paquets du Zone Tracker (Odyssey / Limbus / Nyzul) rejettent desormais un paquet tronque au lieu de lire des octets perimes au-dela de sa fin -- le meme controle de longueur que font deja les autres handlers. Purement defensif : aucun changement en fonctionnement normal." },
};
static const ChangeLine CL_57[] = {
    { "*Each box can now use its OWN FFXI window skin, independent of the party.* Set a box (Timers, Hate List, Debuffs, ...) to Custom -> FFXI and pick any of the game skins in the grid -- it no longer has to match the party's skin, and it works even when the party is on a procedural theme. (Before, every FFXI box was forced to show the party's skin, and could not use FFXI at all unless the party did too.)",
      "*Chaque boite peut desormais utiliser son PROPRE skin de fenetre FFXI, independamment de la party.* Mets une boite (Timers, Hate List, Debuffs...) en Custom -> FFXI et choisis n'importe quel skin du jeu dans la grille -- il n'est plus oblige de suivre le skin de la party, et ca marche meme quand la party est sur un theme procedural. (Avant, toute boite FFXI etait forcee d'afficher le skin de la party, et ne pouvait meme pas etre en FFXI si la party ne l'etait pas.)" },
};
static const ChangeLine CL_56[] = {
    { "*Your HUD layout can no longer be corrupted by a crash while it saves.* The edit-mode layout file is now written to a temporary copy and swapped in atomically, so a crash mid-save can never leave it half-written or empty.",
      "*Ta disposition HUD ne peut plus etre corrompue par un crash pendant sa sauvegarde.* Le fichier de disposition (mode edition) est desormais ecrit dans une copie temporaire puis echange de facon atomique, donc un crash en pleine sauvegarde ne peut plus le laisser a moitie ecrit ou vide." },
    { "*Config-page preview icons no longer go missing for the rest of the session.* The Timers, Zone Tracker, Debuffs and Treasure preview panels gave up loading their icons after a single badly-timed miss; they now retry, like the rest of the HUD already did.",
      "*Les icones d'apercu de la page Config ne disparaissent plus pour le reste de la session.* Les apercus Timers, Zone Tracker, Debuffs et Treasure abandonnaient le chargement de leurs icones apres un seul echec mal tombe ; ils reessaient maintenant, comme le reste du HUD le faisait deja." },
};
static const ChangeLine CL_55[] = {
    { "*Area song counts are more accurate when you re-sing.* If a party member is out of range when you re-cast, their older song is now listed by name (\"Kaories - Victory March\") on its own timer, instead of being hidden inside the \"(AoE N)\" count as if they were still fully buffed. A song still on an ally but no longer on you (pushed off by newer songs) also stops showing a false \"(AoE 2)\".",
      "*Les comptes de chants de zone sont plus precis quand tu rechantes.* Si un membre est hors de portee au moment ou tu relances, son ancien chant est desormais liste par nom (\"Kaories - Victory March\") sur son propre minuteur, au lieu d'etre cache dans le compte \"(AoE N)\" comme s'il etait encore pleinement buffe. Un chant encore sur un allie mais plus sur toi (pousse dehors par de nouveaux chants) n'affiche plus non plus un faux \"(AoE 2)\"." },
    { "*A song you replace on purpose no longer flashes a false \"OUT\" alert.* When you Pianissimo a song onto a party member and it pushes one of their songs out of a slot, that swap is now silent instead of blinking a red \"OUT\" as if you had lost it.",
      "*Un chant que tu remplaces expres ne clignote plus une fausse alerte \"OUT\".* Quand tu Pianissimo un chant sur un membre et qu'il en pousse un autre hors slot, cet echange est desormais silencieux au lieu de clignoter un \"OUT\" rouge comme si tu l'avais perdu." },
};
static const ChangeLine CL_54[] = {
    { "*The duration shown for a Regen you cast on a party member is now correct.* It was reading too short : gear that specifically lengthens Regen (a staff, certain armor pieces) was not being counted -- unlike other enhancing spells, Regen has its own duration-boosting gear. AioHUD now reads that straight from the game's item data, so it is right for any set.",
      "*La duree affichee pour un Regen que tu poses sur un membre de la party est desormais correcte.* Elle etait trop courte : l'equipement qui allonge specifiquement le Regen (un baton, certaines pieces) n'etait pas compte -- contrairement aux autres sorts d'enhancing, le Regen a son propre gear de duree. AioHUD le lit maintenant directement dans les donnees d'items du jeu, donc c'est juste pour n'importe quel set." },
};
static const ChangeLine CL_53[] = {
    { "*Bard song timers are more accurate.* A song now shows its own remaining time (Victory March no longer borrows Honor March's), two songs of the same name no longer share one timer, and area songs on your party regroup correctly when you re-sing one -- and now survive a plugin reload or a zone change.",
      "*Les minuteurs de chants du barde sont plus precis.* Un chant affiche desormais son propre temps restant (Victory March n'emprunte plus celui d'Honor March), deux chants de meme nom ne partagent plus un minuteur, et les chants de zone sur ta party se regroupent correctement quand tu en rechantes un -- et survivent maintenant a un rechargement du plugin ou a un changement de zone." },
    { "*Songs you put on a party member no longer show a false \"OUT\" alert once that member leaves your zone.* When you change zones (or they do), those song rows are cleaned up instead of lingering in red as if you had lost them.",
      "*Les chants que tu poses sur un membre de la party n'affichent plus une fausse alerte \"OUT\" une fois ce membre hors de ta zone.* Quand tu changes de zone (ou lui), ces lignes de chant sont nettoyees au lieu de rester en rouge comme si tu les avais perdus." },
    { "*The Timers list is grouped more logically :* your own buffs first, then the buffs you cast on allies, then buffs other players cast on you, then trusts last.",
      "*La liste des Timers est regroupee plus logiquement :* d'abord tes propres buffs, puis les buffs que tu poses sur tes allies, puis ceux que d'autres joueurs te posent, et les trusts en dernier." },
};
static const ChangeLine CL_52[] = {
    { "*The colour picker is redesigned and better proportioned.* A real square saturation/value box on top, a full-width hue slider below it, and a larger preset grid -- laid out as a compact card instead of one strip stretched across the whole panel. Applies to every colour in every module.",
      "*Le selecteur de couleur est repense et mieux proportionne.* Un vrai carre saturation/valeur en haut, un slider de teinte en dessous, et une grille de presets plus grande -- en carte compacte au lieu d'une bande etiree sur toute la largeur. S'applique a chaque couleur de chaque module." },
    { "*Favourite colours.* Build your own palette right in the colour picker : the *+* button saves the current colour, a click on a saved swatch reuses it, and hovering one shows a small *x* to remove it. Shared by every picker and kept across sessions.",
      "*Couleurs favorites.* Compose ta propre palette dans le selecteur : le bouton *+* enregistre la couleur actuelle, un clic sur un favori la reapplique, et le survol affiche un petit *x* pour l'enlever. Partagee par tous les selecteurs et conservee d'une session a l'autre." },
    { "Fix : in Arcade WS, the three colour pickers (Name / Damage A / Damage B) no longer interfere with each other -- dragging one used to move the others.",
      "Correctif : dans Arcade WS, les trois selecteurs de couleur (Nom / Degats A / Degats B) n'interferent plus entre eux -- deplacer l'un bougeait les autres." },
};
static const ChangeLine CL_51[] = {
    { "*The Timers buff-filter alerts are clearer and warn you earlier.* The four states are now *Show / Show+alert / Hide / Hide+alert* (the old \"focus\" wording is gone), and a *+alert* buff now BLINKS its name and timer the moment it drops under the *Alert under* time -- an early \"recast soon\" cue, not just the last-10-seconds flash. The two sliders are *Alert under* and *Alert duration*.",
      "*Les alertes du filtre de buffs sont plus claires et te previennent plus tot.* Les quatre etats sont maintenant *Afficher / Afficher+alerte / Masquer / Masquer+alerte* (le mot \"focus\" disparait), et un buff *+alerte* CLIGNOTE desormais son nom et son minuteur des qu'il passe sous le temps *Alerter sous* -- un avertissement \"recast bientot\", pas seulement le clignotement des 10 dernieres secondes. Les deux curseurs sont *Alerter sous* et *Duree de l'alerte*." },
    { "*A buff someone re-casts over yours now shows the right caster and timer.* When an ally or a trust overwrites your Protect / Shell -- or a weaker spell is refused with \"no effect\" (e.g. a lower Phalanx over your Phalanx II) -- the row no longer keeps the old name and owner while only the time updates. Covers the Majesty AoE case too : your own copy stops folding into the \"(AoE N)\" group once someone re-casts it on you.",
      "*Un buff qu'on relance par-dessus le tien affiche le bon lanceur et le bon temps.* Quand un allie ou un trust ecrase ton Protect / Shell -- ou qu'un sort plus faible est refuse en \"no effect\" (ex. un Phalanx inferieur sur ton Phalanx II) -- la ligne ne garde plus l'ancien nom et proprietaire avec juste le temps qui change. Couvre aussi le cas Majesty AoE : ta propre copie ne se replie plus dans le groupe \"(AoE N)\" une fois qu'on te la relance." },
    { "Fix : the minimap and its markers could stop loading -- a black map on every zone-in -- after the PC had been running for about 25 days. They load again.",
      "Correctif : la minicarte et ses marqueurs pouvaient cesser de se charger -- carte noire a chaque changement de zone -- apres environ 25 jours d'allumage du PC. Ils se rechargent." },
    { "The in-game Help was refreshed to match the current modules : Timers rewritten for the family filter, plus several stale control names and two wrong descriptions (Treasure Pool's index and timer colours, PointWatch) corrected.",
      "L'Aide en jeu a ete mise a jour pour coller aux modules actuels : Timers reecrit pour le filtre par famille, plus plusieurs noms de controles perimes et deux descriptions fausses (l'index et les couleurs du Pool de tresor, PointWatch) corriges." },
};
static const ChangeLine CL_50[] = {
    { "*Timers has a new buff filter, organised by family.* The old per-job track list is gone -- buffs are grouped by magic family (Protect/Shell, Haste, Barspells, Songs, Rolls, Ninjutsu...) and Job Abilities are listed by job with your current job in gold. Each buff has ONE setting shared across all your jobs (Show / Show+alert / Hide / Hide+alert), so hiding e.g. Shell hides it on every job. Recasts are always shown.",
      "*Timers a un nouveau filtre de buffs, range par famille.* L'ancienne liste 'suivi par job' disparait -- les buffs sont groupes par famille de magie (Protect/Shell, Haste, Barspells, Chants, Rolls, Ninjutsu...) et les capacites de job sont listees par job, ton job actuel en dore. Chaque buff a UN seul reglage partage entre tous tes jobs (Afficher / Afficher+alerte / Masquer / Masquer+alerte), donc masquer par ex. Shell le masque sur tous tes jobs. Les recast sont toujours affiches." },
    { "*The buff-source filter now handles another player's trusts and their mixes.* On a second character in your party (your mule, while your main's trusts are out), a trust's buffs are correctly seen as trust-cast, so 'me only' / 'me + players' hide them. In particular a Trust's stat-boost mix (STR/DEX/VIT... Boost from Monberaux/Ygnas) no longer counts as your own -- only a Gain-X you actually cast stays.",
      "*Le filtre de source des buffs gere maintenant les trusts d'un autre joueur et leurs mix.* Sur un second personnage dans ta party (ton mule, pendant que les trusts de ton main sont sortis), les buffs d'un trust sont bien vus comme lances par un trust, donc 'moi seulement' / 'moi + joueurs' les cachent. En particulier le mix de boost de stats d'un trust (STR/DEX/VIT... Boost de Monberaux/Ygnas) ne compte plus comme le tien -- seul un Gain-X que tu as reellement lance reste." },
    { "Small fix : a song sung by someone else no longer wears a previous singer's job-ability tag (e.g. Ulmia's Madrigal showing \"NT\").",
      "Petit correctif : un chant chante par quelqu'un d'autre ne porte plus le tag de capacite d'un chanteur precedent (par ex. le Madrigal d'Ulmia affiche \"NT\")." },
};
static const ChangeLine CL_49[] = {
    { "*Minimap dots no longer spill past the frame.* Party, NPC and monster markers are now cut cleanly at the map's edge instead of poking outside it just before they vanish -- on the round map and the square one, with or without a border.",
      "*Les points du minimap ne debordent plus du cadre.* Les marqueurs de joueurs, PNJ et monstres sont desormais coupes net au bord de la carte au lieu de depasser juste avant de disparaitre -- en carte ronde comme carree, avec ou sans bordure." },
    { "*The Vana'diel clock now fits its text in French.* The long day names (\"Jour de Lumiere\") no longer run under the frame, and the clock keeps the same centred margins whether it sits above, below, left or right of the map (left / right now hug the map with a clean gap).",
      "*L'horloge de Vana'diel s'adapte au texte francais.* Les noms de jour longs (\"Jour de Lumiere\") ne passent plus sous le cadre, et l'horloge garde les memes marges centrees qu'elle soit en haut, en bas, a gauche ou a droite de la carte (gauche / droite collent proprement a la carte)." },
    { "*Point Watch no longer shifts when you switch XP, CP or ML.* The three modes share one layout now -- the box keeps the same size and the numbers stay aligned across modes -- and the live preview plus the edit-mode box show the mode you actually picked.",
      "*Point Watch ne bouge plus quand on passe de XP a CP ou ML.* Les trois modes partagent une meme disposition -- la boite garde la meme taille et les nombres restent alignes d'un mode a l'autre -- et l'apercu live comme la boite en mode edition affichent le mode reellement choisi." },
    { "*Keep-out zones can be resized one side at a time.* In the Rules editor, a selected zone now has a handle in the middle of each side (on top of the corner handles), so you can stretch just the width or just the height without changing the other.",
      "*Les zones interdites se redimensionnent cote par cote.* Dans l'editeur Rules, une zone selectionnee a maintenant une poignee au milieu de chaque cote (en plus des coins), pour etirer seulement la largeur ou seulement la hauteur sans changer l'autre." },
    { "Small fix : in the party-buffs preview, the \"+N\" more-buffs marker is no longer glued to the last icon.",
      "Petit correctif : dans l'apercu des buffs de groupe, l'indicateur \"+N\" (buffs supplementaires) n'est plus colle a la derniere icone." },
};

// One entry per released version, NEWEST FIRST. The Update tab renders each as a collapsible header ; the newest
// (index 0) starts expanded, the rest collapsed (relOpen_ in config_page.h defaults index 0 = true).
struct Release { const char* version; const ChangeLine* lines; int n; };
static const Release RELEASES[] = {
    { "1.0.62", CL_62, (int)(sizeof(CL_62) / sizeof(CL_62[0])) },
    { "1.0.61", CL_61, (int)(sizeof(CL_61) / sizeof(CL_61[0])) },
    { "1.0.60", CL_60, (int)(sizeof(CL_60) / sizeof(CL_60[0])) },
    { "1.0.59", CL_59, (int)(sizeof(CL_59) / sizeof(CL_59[0])) },
    { "1.0.58", CL_58, (int)(sizeof(CL_58) / sizeof(CL_58[0])) },
    { "1.0.57", CL_57, (int)(sizeof(CL_57) / sizeof(CL_57[0])) },
    { "1.0.56", CL_56, (int)(sizeof(CL_56) / sizeof(CL_56[0])) },
    { "1.0.55", CL_55, (int)(sizeof(CL_55) / sizeof(CL_55[0])) },
    { "1.0.54", CL_54, (int)(sizeof(CL_54) / sizeof(CL_54[0])) },
    { "1.0.53", CL_53, (int)(sizeof(CL_53) / sizeof(CL_53[0])) },
    { "1.0.52", CL_52, (int)(sizeof(CL_52) / sizeof(CL_52[0])) },
    { "1.0.51", CL_51, (int)(sizeof(CL_51) / sizeof(CL_51[0])) },
    { "1.0.50", CL_50, (int)(sizeof(CL_50) / sizeof(CL_50[0])) },
    { "1.0.49", CL_49, (int)(sizeof(CL_49) / sizeof(CL_49[0])) },
    { "1.0.48", CL_48, (int)(sizeof(CL_48) / sizeof(CL_48[0])) },
    { "1.0.47", CL_47, (int)(sizeof(CL_47) / sizeof(CL_47[0])) },
    { "1.0.46", CL_46, (int)(sizeof(CL_46) / sizeof(CL_46[0])) },
    { "1.0.45", CL_45, (int)(sizeof(CL_45) / sizeof(CL_45[0])) },
    { "1.0.44", CL_44, (int)(sizeof(CL_44) / sizeof(CL_44[0])) },
    { "1.0.43", CL_43, (int)(sizeof(CL_43) / sizeof(CL_43[0])) },
    { "1.0.42", CL_42, (int)(sizeof(CL_42) / sizeof(CL_42[0])) },
    { "1.0.41", CL_41, (int)(sizeof(CL_41) / sizeof(CL_41[0])) },
    { "1.0.40", CL_40, (int)(sizeof(CL_40) / sizeof(CL_40[0])) },
    { "1.0.39", CL_39, (int)(sizeof(CL_39) / sizeof(CL_39[0])) },
    { "1.0.38", CL_38, (int)(sizeof(CL_38) / sizeof(CL_38[0])) },
    { "1.0.37", CL_37, (int)(sizeof(CL_37) / sizeof(CL_37[0])) },
    { "1.0.36", CL_36, (int)(sizeof(CL_36) / sizeof(CL_36[0])) },
    { "1.0.35", CL_35, (int)(sizeof(CL_35) / sizeof(CL_35[0])) },
    { "1.0.34", CL_34, (int)(sizeof(CL_34) / sizeof(CL_34[0])) },
    { "1.0.33", CL_33, (int)(sizeof(CL_33) / sizeof(CL_33[0])) },
    { "1.0.32", CL_32, (int)(sizeof(CL_32) / sizeof(CL_32[0])) },
    { "1.0.31", CL_31, (int)(sizeof(CL_31) / sizeof(CL_31[0])) },
    { "1.0.30", CL_30, (int)(sizeof(CL_30) / sizeof(CL_30[0])) },
    { "1.0.29", CL_29, (int)(sizeof(CL_29) / sizeof(CL_29[0])) },
    { "1.0.28", CL_28, (int)(sizeof(CL_28) / sizeof(CL_28[0])) },
    { "1.0.27", CL_27, (int)(sizeof(CL_27) / sizeof(CL_27[0])) },
    { "1.0.26", CL_26, (int)(sizeof(CL_26) / sizeof(CL_26[0])) },
    { "1.0.25", CL_25, (int)(sizeof(CL_25) / sizeof(CL_25[0])) },
    { "1.0.24", CL_24, (int)(sizeof(CL_24) / sizeof(CL_24[0])) },
    { "1.0.23", CL_23, (int)(sizeof(CL_23) / sizeof(CL_23[0])) },
    { "1.0.22", CL_22, (int)(sizeof(CL_22) / sizeof(CL_22[0])) },
    { "1.0.21", CL_21, (int)(sizeof(CL_21) / sizeof(CL_21[0])) },
};
static const int RELEASES_N = (int)(sizeof(RELEASES) / sizeof(RELEASES[0]));
