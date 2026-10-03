# Known gaps and problems

Living backlog of **unresolved** product gaps, correctness issues, and sharp edges.
When you close a gap, **remove it from this file** and record the fix in [`CHANGELOG.md`](CHANGELOG.md) if it is worth noting. Do not leave “done” items here.

Agents: see [AGENTS.md](../AGENTS.md).

---

## `common/grpc` (hole punch / node resolve)

| Gap | Where | Notes |
|-----|--------|--------|
| Factory uses a default empty `AuthStore` | [`resolver_factory.cc`](../common/grpc/resolver_factory.cc) | Not the process store from `message_server`; signing/trust will not match the minion. Inject the real store. |
| `NegotiatedHolePunchArg::params` never filled | [`resolver.cc`](../common/grpc/resolver.cc) `FinishResolution` | Only the reserved FD is moved; client Connect punches with empty/default params. |
| Hardcoded hole-punch timers / zero IPs | [`resolver.cc`](../common/grpc/resolver.cc) `StartHolePunching` | `start_time_ms=1234`, placeholder src/dst, magic connect counts; port range `12345–23456` duplicated vs message send. |
| `StartLocked` silent on bind failure | [`resolver.cc`](../common/grpc/resolver.cc) | Sets `resolving_=false` and returns without `ReportResult` — channel can hang. `StartHolePunching` does report `ResourceExhausted`. |
| Incoming punch blocks CQ thread | [`engine.cc`](../common/grpc/engine.cc) `RunSignaledIncomingHolePunch` | Punch + TLS accept run synchronously on the caller (often a CQ thread). |
| Cancel does not interrupt punch/TLS | [`engine.cc`](../common/grpc/engine.cc) `CancelConnect` | Only sets an `atomic` flag; blocking `tcpv4_hole_punch` / `SSL_connect` are not aborted. |
| No client channel wiring for `node://` | factory + channel builders | `RegisterNodeResolver()` is not called from minions; no `makeRefCountedArg` stub injection for the signaling Message stub. |
| Duplicate port reservation | `StartLocked` vs `StartHolePunching` | Both can bind a random port; logic is split and easy to desync. |
| Unused `GRPCCounterHandler` | [`callback.h`](../common/grpc/callback.h) | No in-repo callers; keep or remove deliberately. |
| EE hole-punch arg is string-key `void*` | [`engine.cc`](../common/grpc/engine.cc) `Connect` | Public `EndpointConfig` has no typed `GetObject` (only Int/String/void*). Resolver uses `ChannelArgs::SetObject`; EE must look up via `GetVoidPointer("grpc.custom.hole_punch_params")`. |

---

## Related / follow-ups

- Hole-punch config (port ranges, timers) should be shared between resolver and [`message_send.cc`](../minion/message/message_send.cc) rather than copy-pasted TODOs.
- `HolePunchEventEngine` holds a non-owning `SSL_CTX*`; safe only while the caller’s `SSL_CTX_ptr` outlives the engine (current `message_server` pattern).
- Including `grpcpp/grpcpp.h` can surface **upstream** deprecation warnings in gRPC TLS headers (`IdentityKeyCertPair`, etc.) — not our call sites; do not silence with `-Wno-deprecated` in project code.
