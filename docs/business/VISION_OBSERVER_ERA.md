# Vision: Observer Era — ADA Beyond Debugging

## Context

- Large language models (LLMs) are improving rapidly and may soon generate code with near-zero crashes and minimal API misuse.
- Natural language intent will remain ambiguous versus precise product requirements, and most users won’t be trained to author “engineer-grade” prompts.
- OS vendors may embed first‑party agents that generate software from a single sentence. Users will increasingly report unexpected behaviors rather than compiler/runtime errors.

## Thesis

- Ground truth lives at runtime. Source and prompts encode intent; only execution reveals actual behavior.
- ADA evolves from a debugger to a runtime observer that captures, explains, and correlates behavior while programs run, producing machine‑readable evidence for agents and human‑readable summaries for users.

## Strategic role for ADA

- Serve as the OS/agent ecosystem’s “runtime senses”: always‑on lightweight signals with on‑demand deep capture.
- Shorten the path from symptom → instrumentation plan → evidence → explanation.
- Provide stable, agent‑first protocols and narratives consumable by autonomous agents and IDEs.

## Design implications

- Telemetry‑first two‑lane architecture: index lane always on; detail lane windowed via triggers and key‑symbol plans.
- Symptom‑to‑instrumentation planning: turn a natural‑language symptom into a capture recipe (triggers, pre/post roll, key symbols).
- Preflight/remediation as product features: deterministic permission checks, explicit consent, no SIP relaxations.
- Privacy and policies: least‑privilege operation, PII‑aware capture, auditability for enterprise.

## KPIs for the observer era

- Time‑to‑explain unexpected behavior (P50/P95) from symptom to narrative with EvidenceRefs.
- Attach/observe success rate after automated remediation.
- Flight‑recorder coverage: proportion of issues reproducible with a detailed window.
- Narrative precision: top‑3 finding accuracy on golden traces; agent consumption success of EvidenceRefs.

## Risks and mitigations

- OS vendor bundling: differentiate via cross‑OS breadth, openness (stable SDK/protocol), and superior observability quality; consider OEM paths.
- Backend volatility (entitlements/Frida): adapter abstraction, alt backends (eBPF/ETW), compatibility CI.
- Privacy/regulatory: explicit consent gates, policy controls, audit logs, enterprise packaging.

## Placement in repo

- This document complements execution‑focused analysis in `docs/business/BUSINESS_ANALYSIS_FILLED.md`.
- Aligns with `AGENT.md` governance and the Top‑Down Validation Framework (business → user stories → specs → code).
