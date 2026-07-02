---
title: Reading the live roster
summary: How party_state.cpp reads the 18-slot party/alliance member array each frame, plus the packets that top it up.
source: ARCHITECTURE.md §4
---
# Reading the live roster (`party_state.cpp`)

`load_from_memory()` (every frame) is the live party+alliance table:
- Anchor: `g = *(LuaCore+0x1C8400)`, `pp = *(g+0x248)` (4 bytes into member[0]), `base = pp-4`,
  self-validated against the player id.
- One member = `0x7C` bytes ; **18 slots** : 0..5 = your party, 6..11 = alliance party 2,
  12..17 = alliance party 3. Counts at `allianceinfo +0x13/+0x14/+0x15`, gated by the party
  leader ids `+0x04/+0x08/+0x0C` (so a wrong count can't spawn phantom boxes).
- `read_member()` does ONE SEH-guarded 0x7C block copy then parses the fields from the local
  buffer (name `+0x0A`, id `+0x1C`, hp/mp/tp `+0x28/+0x2C/+0x30`, %s `+0x34/+0x35`, zone `+0x36`,
  flags `+0x3C`, jobs `+0x71/+0x73`). One source of truth for the offsets, shared by the party
  and alliance loops. Full field table in [party array](../game-data/party-array.md).

Packets top up what the array lacks or lags: `0x0DD/0x0DF` (vitals), `0x028` (cast bar),
`0x076` (other members' buffs — self buffs come from `player+0x1C`). See [party packets](../game-data/party-packets.md), [cast bar](../game-data/cast-bar.md), [member buffs](../game-data/member-buffs.md).

## See also
- [The per-frame data flow](data-flow.md)
- [Reverse-engineering recipe](reverse-engineering-recipe.md)
- [Party array](../game-data/party-array.md)
- [Party packets](../game-data/party-packets.md)
- [Member buffs](../game-data/member-buffs.md)
- [Cast bar](../game-data/cast-bar.md)
