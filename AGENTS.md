# AGENTS.md — dist-storage

**Pointers:** [README.md](README.md) (overview + doc index) · Cursor always-on rule [`.cursor/rules/agents-md.mdc`](.cursor/rules/agents-md.mdc) (points here).

Instructions for AI coding agents (and humans) working in this repo. This project uses **C++**, **gRPC**, **Protobuf**, and **OpenSSL**. Keep entries **short and actionable**; split deep docs into focused files when they grow past ~400–500 lines.

---

## Project layout

| Area | Path | Notes |
|------|------|--------|
| Crypto, SSL helpers | `common/crypto/` | `guard_ptr`, `SSLSigner` / `SSLVerifier`, X509 helpers |
| Peer auth & `AuthStore` | `common/peer/` | Signed message validation, cert trust |
| gRPC glue | `common/grpc/` | Resolvers, SSL endpoint, engine |
| Protos | `common/proto/` | `.proto` sources; generated code under `common/proto/cc/` (build tree) |
| Minion message service | `minion/message/` | `message_server`, Send / Stream handlers |

---

## Build & check

From the repo root (after a configured `build/` directory):

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

Useful targets: `message_server`, `obj_store`, `dist_storage_crypto`, `dist_storage_peer`, `dist_storage_grpc`, `my_proto_lib`.

After crypto or `guard_ptr` changes, rebuild `dist_storage_crypto` (and dependents) to catch compile errors early.

---

## C++ conventions

- **Scope**: Fix only what the task needs; avoid drive-by refactors and unrelated files.
- **Style**: Match surrounding code (naming, includes, error handling). Prefer small, clear functions over layers of helpers.
- **Comments**: Do not add noisy comments or docstrings on obvious code; keep proto / public API comments accurate when behavior changes.

---

## `guard_ptr` and OpenSSL

Defined in [`common/utils/guard_ptr.h`](common/utils/guard_ptr.h).

- The deleter is a **non-type template parameter** `auto free_func`, so both `void`-returning and `int`-returning OpenSSL free functions work (e.g. `BIO_free` vs `X509_free`). The return value of `int` frees is discarded; no wrapper needed.
- Prefer **implicit conversion** to the raw pointer at call sites: pass `bio` or `cert` where a `BIO*` / `X509*` is expected. Use **`.get()` or `.handle()`** only when you must (e.g. overload resolution, or transferring ownership out).
- Typical typedefs live next to crypto types, e.g. `BIO_ptr`, `X509_ptr` in [`common/crypto/auth.h`](common/crypto/auth.h).

---

## Protobuf & messaging

- Prefer **documenting field semantics in `.proto` comments** when names are easy to misread (e.g. `MessageData.has_recipient_cert`: whether the **sender** already had the **recipient** peer in its trust store when signing—not a generic “need cert” flag).
- **`SignedMessage`**: signature covers **`data` only** (see `fillMessageData` in [`common/peer/message.cc`](common/peer/message.cc)); `sender_cert` is auxiliary for trust bootstrapping.
- **`AuthStore`** ([`common/peer/store.h`](common/peer/store.h)): signing, peer cache, authorities. When extending certificate bootstrap (`learn_peer`, `push`, `sign_message(..., add_cert)`), keep **send** and **receive** paths consistent and rebuild all callers.

---

## gRPC & minion

- Node resolver / hole punch: [`common/grpc/resolver.cc`](common/grpc/resolver.cc) — understand `auth_store` and `sign_message` before changing call signatures.
- Message service entry: [`minion/message/message.cc`](minion/message/message.cc); handlers under `message_send.cc`, `message_stream.cc`, `stream_router.cc`, `recipient_dispatch.cc`.

---

## Agent workflow (Cursor and similar)

- **Plans**: If the user attaches a plan and says **do not edit the plan file**, respect that; implement in code only.
- **Execution**: Prefer running builds/tests in the environment rather than only suggesting commands—this repo is set up for local builds.
- **Rules**: Project-specific guardrails may also live in `.cursor/rules/` or user rules; avoid duplicating long prose here—**point to files** instead of copying whole policies.

---

## Pitfalls to avoid (from recent work)

1. **Assuming `.proto` names match intent** — confirm comments and `sign_message` / handlers before changing behavior.
2. **`guard_ptr` + `void`-only deleter** — use `auto` deleter template parameter; do not add redundant `bio_free_`-style wrappers for `BIO_free`.
3. **Unnecessary `.get()`** on `guard_ptr` when an implicit conversion suffices.
4. **Certificate flow** — receiver must learn unknown senders **before** strict validation when using `sender_cert`; authority roots must be present in `AuthStore` for `push` to succeed.
5. **Large rules in one file** — split agent instructions if this file grows unwieldy (e.g. per-subsystem notes under `minion/` or `common/peer/`).

---

## References

- Cursor: [Rules](https://cursor.com/docs/context/rules) — keep rules focused; use examples and `@file` references.
- Broader **AGENTS.md** usage: plain Markdown, sections for stack, setup, conventions, testing, and boundaries—tools (Cursor, Copilot, Codex, etc.) often pick up `AGENTS.md` at the repo root automatically.

When you fix a recurring mistake, add a **one-line bullet** under *Pitfalls* or a short *Conventions* note so the next agent does not repeat it.
