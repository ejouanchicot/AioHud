# Song duration (BRD) — the model, and why it was wrong for months

*Measured against the server on 2026-07-25 (`//aio songlog`, two sessions). This is the reference for
`src/model/song_dur.h` + `song_dur_gen.h`; the per-item table is **generated**, see
[`scripts/gen_song_dur.py`](../../scripts/gen_song_dur.py).*

---

## Your own songs are not computed at all

The server sends the exact expiry in **`0x063`**. Timers just counts down to it. No model, no gear reading,
nothing to get wrong. Every self row has always been correct for this reason.

**Ally copies have no such packet.** `0x076` carries only the *presence* of a member's buffs — no duration —
and the partymember struct has no duration field either (RE-proven, see [member-buffs.md](member-buffs.md)).
The game itself does not know how long someone else's buff lasts. So an ally row must be derived, and that
derivation is the only place a song timer can be wrong.

## The formula

```
dur = Base × m1 × m2 × m3 + a3          Base = 120 s   (Miracle Cheer overrides to a flat 900 s)

m1 = 1 + Σ(explicit "Song effect duration +N%")
       + Σ(10% per point of "All songs +N")                   ← every song
       + Σ(10% per point of '"<Song>"+N' of THIS family)      ← that family only
       + 0.05                                                  ← BRD job-point gift

m2 = 2.0 under Troubadour (348), else 1.0
m3 = 1.5 under Soul Voice (52) or Marcato (231) AND family ∈ {11 Hymnus, 12 Mazurka, 14 Scherzo}, else 1.0
a3 = flat merit seconds, Marcato only (`read_jp_u8(0x148)`)
```

Clarion Call and Tenuto do **not** touch duration — Clarion Call adds a song *slot*, Tenuto protects a song
on you from being overwritten. Both once fed a bogus duration bonus here.

## The rule that unblocked it: potency IS duration

> *"Le gear « Song+ » (potence) ajoute **+10 % de durée par 1 Song+ ».
> Tous les bonus de durée d'équipement sont additifs entre eux, et additifs avec le bonus de durée des
> effets Song+ potence."*
> — [`reference-sheets/song_resume.html`](../reference-sheets/song_resume.html) (BG-Wiki *Category:Song*)

So Gjallarhorn's `All songs +4` is **+40 % duration on every song**, and Fili Calot +3's `"Madrigal"+1` is
**+10 % on Madrigal**. A stat that reads as "power" in game is also duration.

This resolved a contradiction the project had been stuck on. The old hand-written table carried a *family*
column believed to be potency, and therefore deliberately **not** counted as duration
([song-potency.md](../reference-sheets/song-potency.md) said so explicitly). It **was** potency — and potency **is** duration.
Both readings were half right, which is why the model sat 22–37 % under the server for months.

## Why nobody noticed

An AoE song also lands on **you**, and those rows display your exact `0x063` timer instead of the estimate —
**the grouping borrows the truth and hides the error underneath**. Only a single-target (Pianissimo) cast
exposes the model. This is the same trap as the old 1800 s enhancing-duration cap
([buffs-on-allies.md](buffs-on-allies.md)); when a duration looks wrong, test it **single-target**.

## Measurements

Server `0x063` vs the model, same equipped ids, captured with `//aio songlog`:

| Song | before | after | real |
|---|---|---|---|
| Valor Minuet IV / V | 223 s | **343 s** | 342 s |
| Blade Madrigal | 223 s | **355 s** | 352 s |
| Honor March | 223 s | **355 s** | 352 s |
| Goblin Gavotte | 126 s | **162 s** | 159 s |
| Gold Capriccio | 126 s | **162 s** | 158 s |

The small overshoot is the truncation the reference sheet documents (*"les décimales sont tronquées après
totalisation"*). All five are locked into [`tests/t_durations.cpp`](../../tests/t_durations.cpp) — the table
is generated, so a bad regeneration would otherwise stay invisible until somebody sang.

The **family term is load-bearing**, and this is the proof: Madrigal and Minuet were cast on the *same sixteen
equipped ids*, with no Troubadour and the same 120 s base, and ran **352 s vs 340 s**. Twelve seconds is
exactly the +10 % of one extra family piece — that player carries two Madrigal-family items against one
Minuet.

## Where the numbers come from

| Source | Covers | Why |
|---|---|---|
| `res/item_descriptions.lua` | **potency**, every item in the game | Stated numerically on the item, so it is read dynamically and needs no maintenance |
| `reference-sheets/song_resume.html` | **explicit %**, keyed by (name, level) | Authoritative: several items carry duration their in-game text never mentions |
| [song-duration-items.txt](song-duration-items.txt) | historical | The `Timers.dll` reverse. Its *formula* was right all along; its per-item values are superseded |

**The sheet has to win over the item text.** Brioso Slippers +4 is the proof: no duration line in game, +15 %
on the sheet, and dropping it put the model exactly 15 points under a measurement. Where both state a number
the generator cross-checks them and reports any divergence rather than silently preferring one.

Items the reverse never saw are therefore covered now, because potency is read from the game itself. Only a
new *explicitly-numbered* piece needs a line in `SHEET_DURATION` — and the generator prints a loud warning
listing any item whose text says "Increases song effect duration" with no number and no sheet entry.

## Verifying after a change

```
//aio songlog          then sing
```

`SONGREAL` prints, for every song that lands on you, the duration the server reports next to the model's
prediction, plus the gear factor the table *should* have produced. A table error surfaces as a number instead
of as a complaint. `SONGUSE` prints the m1 each ally row was built from.

> A learned-from-server duration was tried and **removed**. A correct table beats it: a learned value goes
> stale on a gear change, and can never describe a song evicted before it lands on the caster (Archer's
> Prelude on a full song list). The measurement survives only as the check above.

## See also

- [chants-barde-comment-ca-marche.md](chants-barde-comment-ca-marche.md) — the same mechanics in plain French, no code
- [song-potency.md](../reference-sheets/song-potency.md) — per-song effect strength
- [bard.md](../reference-sheets/bard.md) — the job: song list, JAs, job points
- [`docs/architecture/timers-songs-brd.md`](../../architecture/timers-songs-brd.md) — how the Timers column *displays* songs
