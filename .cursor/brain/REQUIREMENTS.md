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

- [x] Architecture profile + concrete starter locked **after** D0 stories — DECISIONS §Architecture
- [x] RULES / stack-contracts step done

Stack is **locked** in DECISIONS / [CONTEXT.md](./CONTEXT.md): FFmpeg H.264, Protobuf, Asio, **vcpkg**, Qt Widgets, installers.

## Evidence (optional)

- [x] CLIENT inbox process understood — [CLIENT.md](./CLIENT.md)
- [x] Open conflicts resolved or parked

C1 (Wayland in v1) and C2 (reconnect as D0) resolved. J0–J3 are `build-ready` (self-validated 2026-08-31 from CLIENT evidence + JPEG walkthrough). Feature work uses the production stack.

## Still missing

None for D0. D1 (J4–J6) remains `draft`. Paths: [CODEMAP.md](./CODEMAP.md).
