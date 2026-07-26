# Audits

Point-in-time audit findings and test plans. Unlike the rest of `docs/` these are **git-tracked**
(like `game-data/`): a dated snapshot is expensive to reproduce and worth keeping even once the code
has moved past it. Read each one with its date in mind — the living spec lives in the topic files it
links to, not here.

- [Timers module audit (2026-07-20)](timers-audit-2026-07-20.md) — the initial full audit of the Timers module at v1.0.42; most items were fixed same-day (see the header for what remains open), with the real state in [../game-data/timers.md](../game-data/buffs-and-timers/timers.md).
- [Full project audit (2026-07-26, second pass)](audit-projet-2026-07-26b.md) — transverse pass run after the morning audit and the `50117e7`/`358d9d8` fixes. Headline: the CI rule-10 guardrail regex cannot see `mmTried_ = true` (member `_` suffix), which is why the last one-shot latch had to be found by hand. Plus a `u32`-truncated pixel width the compiler was already flagging, the WS-popup whitelist narrowed on one character, and an updater with no integrity check. Includes verified status of the still-open items.
- [In-game test plan](TEST_PLAN.md) — the (French) deploy-and-validate checklist for a session's UI/audit/asset changes: smoke test first, then per-change targets.
