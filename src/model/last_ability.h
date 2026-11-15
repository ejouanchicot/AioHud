// last_ability.h -- which job ability YOU last used on each recast timer, so a shared recast's Timers row names it.
//
// Several abilities share one recast timer : Lasting Emanation and Ecliptic Attrition (244), the Phantom Rolls (193),
// the SP1s (0), the SP2s (254)... The row used to take the generated table's first ability, so an Ecliptic Attrition
// read "Lasting Emanation" (reported 2026-11-14). The model notes the ability from your own 0x028 (category 6) and the
// Timers builder names the row after it ; the old rule (only usable / first row) stays for a recast running with no
// note (a plugin loaded mid-recast). SCH stratagems keep their family name : that timer is the charge bar.
// Pure ; tests/t_timersrules.cpp.
#pragma once

namespace aio {

struct LastAbility {
    struct Slot { unsigned short rid, aid; };
    static const int N = 32;   // recasts that run at once on one job : far fewer ; the oldest note goes first
    Slot s[N];
    int n = 0;

    void note(unsigned rid, unsigned aid) {
        if (rid >= 4096 || !aid) return;
        for (int i = 0; i < n; ++i) if (s[i].rid == rid) { s[i].aid = (unsigned short)aid; return; }
        if (n == N) { for (int i = 1; i < N; ++i) s[i - 1] = s[i]; --n; }
        s[n].rid = (unsigned short)rid; s[n].aid = (unsigned short)aid; ++n;
    }
    unsigned get(unsigned rid) const {
        for (int i = 0; i < n; ++i) if (s[i].rid == rid) return s[i].aid;
        return 0;
    }
    void clear() { n = 0; }
};

// The ability a shared recast's row is named after : the one noted, while your job can still use it (a note left from
// another job names nothing ; the old rule decides then). jaOk false : the usable bitmap is unknown, the note is kept.
inline unsigned last_ability_label(unsigned noted, const unsigned char* jaBits, bool jaOk) {
    if (!noted) return 0;
    if (jaOk && !(noted < 1024 && jaBits && (jaBits[noted >> 3] & (1u << (noted & 7))))) return 0;
    return noted;
}

}  // namespace aio
