# Decisions (locked)

> Nothing below is locked until discovery writes it. Agent proposes; human locks.  
> Stack lock happens **after** D0 stories (DISCOVERY Step ARCH), unless human asked early.

## Product locks

| ID | Decision | Locked? |
|----|----------|---------|
| P1 | North star — see [PRODUCT.md](./PRODUCT.md) | No |
| P2 | Devices day one | No |
| P2b | Deploy / distribution target | No |
| P3 | User model (solo / team / multi-tenant) | No |
| P4 | Offline / sync requirements | No |
| P5 | Out of scope for v1 | No |

## Deploy lock

| Field | Value |
|-------|--------|
| Primary deploy target | _TBD — Q2_ |
| Secondary (later) | — |
| Locked by / date | — |

## NFR locks

| Area | Locked choice | Locked? |
|------|---------------|---------|
| Scale / expected users | — | No |
| Security / privacy | — | No |
| Availability / backup | — | No |
| Accessibility / languages | — | No |
| Compliance | — | No |
| Builder constraints | — | No |

Detail tables may live in [PRODUCT.md](./PRODUCT.md); this section is the lock summary.

## Architecture (locked after DISCOVERY ARCH step — after D0 stories)

**Profile:** _none yet — see ARCHITECTURE_DEFAULTS.md_

| Layer | Choice |
|-------|--------|
| Client | — |
| API | — |
| Data | — |
| Auth | — |
| Sync / jobs | — |
| Concrete starter | — |

```mermaid
flowchart LR
  Todo[Await_D0_stories_then_ARCH]
```

**Why this fits (cite user model, deploy, NFRs, D0 journeys):** —  
**Locked by:** —  
**Date:** —

## Boundaries (ownership)

Who is system of record / UI owner. Fill as MODULES and journeys clarify.

| ID | Decision | Locked? |
|----|----------|---------|
| B1 | _e.g. Module X owns entity Y_ | No |
| B2 | _e.g. No fake CRUD for disabled features — status only_ | No |

## Journey / process locks

| ID | Decision |
|----|----------|
| G1 | Feature work needs journey `build-ready` or waiver ([FLOWS.md](./FLOWS.md), [STORIES.md](./STORIES.md)) |
| G2 | Discovery is hybrid: personas + CLIENT evidence |
| G3 | Agent proposes one architecture; human locks or tweaks one thing |
| G4 | Journey statuses: `draft` → `persona-ready` → `client-validated` → `build-ready` |
| G5 | MODULES stays provisional until boundary lock or dedicated boundary journey |
| G6 | Requirements-first: D0 stories before stack proposal (B-S9), unless human asks early |

## Open but non-blocking defaults

| Topic | Default until revisited |
|-------|-------------------------|
| — | — |

## Open questions (blocking)

| ID | Question | Blocking? |
|----|----------|-----------|
| — | — | — |

## Rejected options (so we don’t re-litigate)

| Option | Why rejected |
|--------|----------------|
| — | — |
