# Requirements checklist

**Index only** — do not duplicate long prose here. Tick boxes as [DISCOVERY.md](./DISCOVERY.md) fills the linked files.  
Agent acts as **requirements engineer**: complete groups top-to-bottom; propose stack only after Functional (D0) is checked (unless human asks early).

## Problem & scope

- [x] North star + audience — [PRODUCT.md](./PRODUCT.md)
- [x] Success metric (first useful version) — PRODUCT
- [x] Pains + current workaround — PRODUCT
- [x] Out of scope (v1) — PRODUCT
- [x] Assumptions / risks noted — PRODUCT

## Users

- [x] User model (solo / team / multi-tenant) — PRODUCT / [DECISIONS.md](./DECISIONS.md)
- [x] Personas with role cards — [PERSONAS.md](./PERSONAS.md)

## Functional (D0)

- [x] Journey list + D0 details — [FLOWS.md](./FLOWS.md)
- [x] Acceptance stories for D0 journeys — [STORIES.md](./STORIES.md)
- [x] Provisional modules — [MODULES.md](./MODULES.md)

## Platforms & deploy

- [x] Devices day one — PRODUCT
- [x] Deploy / distribution target — PRODUCT + DECISIONS §Deploy

## Non-functionals

- [x] NFR table filled (scale, security, backup, a11y, compliance, builder constraints) — PRODUCT / DECISIONS §NFR

## Stack locked

- [ ] Architecture profile + concrete starter locked **after** D0 stories — DECISIONS §Architecture
- [x] RULES / stack-contracts step done

Stack is **proposed** in DECISIONS (native C++ peer desktop). Human lock still required.

## Evidence (optional)

- [x] CLIENT inbox process understood — [CLIENT.md](./CLIENT.md)
- [x] Open conflicts resolved or parked

C1 (Wayland in v1) and C2 (reconnect as D0) resolved. Journeys are `persona-ready`; human still marks `client-validated` / `build-ready`.

## Still missing

_List gaps that block `build-ready` or stack lock:_

- Human lock (or one tweak) of DECISIONS §Architecture
- Human walkthrough: mark J0–J3 `client-validated`, then `build-ready` when ready to code
- Persona name/role corrections if Sam / Riley / Jordan hats are wrong
