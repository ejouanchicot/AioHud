// factory.cpp -- see factory.h.
#include "ui/factory.h"
#include "ui/liquid_bars.h"
#include "ui/player.h"
#include "ui/party.h"
#include "ui/target.h"
#include "ui/minimap.h"

namespace aio {

Widget* make_widget(const std::string& type, const GameState* state) {
    if (type == "PlayerHub") return new Player(state);        // full Player Hub (embeds the fioles as the shared vial provider)
    if (type == "Minimap")   return new Minimap(state);       // zone minimap (map-record transform ; docs/game-data/world/map-system.md)
    if (type == "PartyList")    return new Party();          // main party list (text + bars)
    if (type == "AllianceList") return new Party();          // alliance party box (same widget, tier set via config)
    if (type == "TargetBar")    return new Target();         // target module : name + HP% bar (//aio tent offsets)
    // TODO: "HateList", "VanaClock", ... as each is implemented.
    return nullptr;                                          // unknown/unimplemented -> HUD skips it
}

} // namespace aio
