// config_debug.h -- the in-game Debug tab data : known bugs + planned work, mirroring Debug.txt so testers can
// see the state without the file. DATA ONLY. Included ONCE, by config_page.cpp (from INSIDE `namespace aio`, so
// this file must NOT open the namespace). Keep it in sync with Debug.txt when an item is fixed / added.
#pragma once

struct DebugLine    { const char* en; const char* fr; };
struct DebugSection { const char* titleEn; const char* titleFr; const DebugLine* lines; int n; };

// --- To fix : confirmed bugs, not yet done ---
static const DebugLine DBG_FIX[] = {
    { "Party : the distance value is sometimes wrong (garbage). Cause not yet found -- being investigated.",
      "Party : la distance affiche parfois n'importe quoi. Cause pas encore trouv\xC3\xA9""e -- en cours d'investigation." },
};

// --- Planned : features not started ---
static const DebugLine DBG_TODO[] = {
    { "Distance : show the right range band for a ranged job (RNG / COR) from the equipped ranged weapon (bow / crossbow / gun).",
      "Distance : afficher la bonne bande de port\xC3\xA9""e pour un job \xC3\xA0 distance (RNG / COR) selon l'arme distance \xC3\xA9quip\xC3\xA9""e (arc / arbal\xC3\xA8te / fusil)." },
    { "Party : a buff filter (blacklist) and a way to reorder the buffs on party rows.",
      "Party : un filtre de buffs (blacklist) et un moyen de r\xC3\xA9ordonner les buffs des lignes de party." },
    { "Skillchains : finish / verify the module.",
      "Skillchains : finir / v\xC3\xA9rifier le module." },
};

// --- Fixed : the full list is in the Update tab's changelog ---
static const DebugLine DBG_DONE[] = {
    { "See the Update tab for the full per-version list of what's already fixed (buff/gear icons that stuck, the timer-vs-game offset, dead-member display, the target liquid bar, and more).",
      "Voir l'onglet Mise \xC3\xA0 jour pour la liste compl\xC3\xA8te, par version, de ce qui est d\xC3\xA9j\xC3\xA0 corrig\xC3\xA9 (ic\xC3\xB4nes de buffs/\xC3\xA9quip qui restaient bloqu\xC3\xA9""es, le d\xC3\xA9""calage minuteur-vs-jeu, l'affichage des morts, la barre liquide de la cible, et plus)." },
};

static const DebugSection DEBUG_SECTIONS[] = {
    { "To fix",  "\xC3\x80 corriger", DBG_FIX,  (int)(sizeof(DBG_FIX)  / sizeof(DBG_FIX[0]))  },
    { "Planned", "\xC3\x80 faire",    DBG_TODO, (int)(sizeof(DBG_TODO) / sizeof(DBG_TODO[0])) },
    { "Fixed",   "Corrig\xC3\xA9",     DBG_DONE, (int)(sizeof(DBG_DONE) / sizeof(DBG_DONE[0])) },
};
static const int DEBUG_SECTIONS_N = (int)(sizeof(DEBUG_SECTIONS) / sizeof(DEBUG_SECTIONS[0]));
