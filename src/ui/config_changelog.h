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

// One entry per released version, NEWEST FIRST. The Update tab renders each as a collapsible header ; the newest
// (index 0) starts expanded, the rest collapsed (relOpen_ in config_page.h defaults index 0 = true).
struct Release { const char* version; const ChangeLine* lines; int n; };
static const Release RELEASES[] = {
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
