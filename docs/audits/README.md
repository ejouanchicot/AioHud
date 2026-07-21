# Audits

Point-in-time audit findings and test plans. Unlike the rest of `docs/` these are **git-tracked**
(like `game-data/`): a dated snapshot is expensive to reproduce and worth keeping even once the code
has moved past it. Read each one with its date in mind — the living spec lives in the topic files it
links to, not here.

- [Timers module audit (2026-07-20)](timers-audit-2026-07-20.md) — the initial full audit of the Timers module at v1.0.42; most items were fixed same-day (see the header for what remains open), with the real state in [../game-data/timers.md](../game-data/timers.md).
- [In-game test plan](TEST_PLAN.md) — the (French) deploy-and-validate checklist for a session's UI/audit/asset changes: smoke test first, then per-change targets.
