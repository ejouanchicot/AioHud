// sc_circle.h -- the Skillchains box's CIRCLE style : the resonance sphere (its animation per skillchain, the countdown
// arc, the state / seconds / TP in its centre, the burst elements round it, the weapon skills that continue the chain
// listed beside it). The engine is hud_scfx.cpp, shared with the //aio scfx dev demo ; hud_skillchains.cpp feeds it
// the live chain (or the looping sample of the config preview / Help).
#pragma once
#include "ui/widget.h"   // Frame

namespace aio {

struct ScCircleItem { const char* ws; unsigned char prop, lvl; unsigned char other, wsk; };   // other = 1 : a move of ANOTHER weapon (scWsScope "all") ; wsk = its weapon (res skill id, 0 none)   // a move that continues the chain -> what it makes

struct ScCircleLive {
    unsigned key = 0;            // the chain's identity (mob + when it formed) : a new one restarts the animation
    int   prop = -1;             // the skillchain formed (SCP_*), or -1 : only opened (a lone move, no chain yet)
    float bt = -1.0f;            // seconds since it formed : the animation's clock
    int   phase = 1;             // 0 too early (WAIT), 1 open (GO), 2 closed chain (BURST only)
    bool  closed = false;        // Lv.4 / step > 5 : nothing continues it
    float timer = 0.0f;          // the seconds shown
    float frac = 1.0f;           // the countdown arc : 1 full .. 0 empty
    int   tp = 0;
    const char* opener = 0;      // the move's name (the title while no chain has formed)
    const ScCircleItem* items = 0; int nItems = 0;
    float CX = 0, CY = 0;        // the sphere's centre (screen px)
    float k = 1.0f, kl = 1.0f;   // the sphere's scale, the list's scale (px per design unit)
    float scaleRel = 1.0f;       // the sphere's size relative to the default (fewer particles when drawn bigger)
    bool  preview = false;       // the config / Help sample (it shows everything, distortion included)
    // the HIT popup over the sphere : 1 your weaponskill (arcade), 2 your magic burst ; hitMs = its age (ms), < 0 none
    int hitKind = 0; const char* hitName = 0; int hitDmg = 0; int hitElem = -1; bool hitHasDmg = true; float hitMs = -1.0f;
};

void  sc_circle_draw(const Frame& f, const ScCircleLive& L);
bool  sc_circle_list_rect(const Frame& f, const ScCircleLive& L, float& x, float& y, float& w, float& h);
bool  sc_circle_hit_rect(const ScCircleLive& L, float& x, float& y, float& w, float& h);   // the Arcade WS / Magic Burst popup's room ; false = both off   // the list's rect round the sphere ; false = no list
void  sc_circle_idle();
// the Absorb-TP dial's ABSORB : the scene sucked into the dial, matter streaming in from the caster, a shockwave closing
// in. ageMs = since the cast (the effect runs 0 .. 900 ms) ; then a quiet breath (late : red). Drawn BEFORE the dial.
void  aw_dial_fx(const Frame& f, float CX, float CY, float R, float k, float ageMs, unsigned rgb, float srcX, float srcY, bool distortOn, bool late);                          // every frame : gives the distortion copy back after 5 s unused
inline float sc_circle_radius(float k) { return 44.0f * k; }

} // namespace aio
