# Changelog

Notable completed work and dependency bumps. Open problems stay in [`known-gaps.md`](known-gaps.md) only.

---

## 2026-10-09 — planner ANTLR pin + deprecation cleanup

- Confirmed / hardened planner ANTLR pin at **4.13.2** (latest stable): FetchContent runtime tag + complete JAR with SHA256, `find_package(Java)`, codegen into the build tree via `*.cpp` / `*.h` globs.
- `query_parser_obj`: C++17, SYSTEM ANTLR includes, `-Werror=deprecated-declarations` on project TUs; dropped stale `ParserRuleContext.h` / `ANTLR4_INCLUDE_DIR` usage.
- Unblocked proto build after the resource/auth reshape: unused `resource.proto` import in `holder/obj.proto`, `message.proto` → `auth.ResourceConfig`, completed `session.proto` syntax/package.

---

## 2026-10-03 — gRPC / protobuf stack upgrade

- Bumped FetchContent pins: **gRPC v1.84.0** (recursive submodules), **GoogleTest v1.18.0**.
- Protobuf/Abseil via gRPC module provider: **protobuf 35.1** (`third_party/protobuf/version.json`). ANTLR unchanged.
- Resolver: `EndpointAddresses` / `EndpointAddressesList` + vector ctor; dropped `ServerAddress` / `server_address.h`; left `service_config` default (no fake `OkStatus()`).
- `ref_counted_arg.h`: `<grpc/grpc.h>` instead of codegen headers.
- EventEngine / SSL endpoint kept modern `Read`/`Write`/`Connect`/`CreateListener` shapes; `ctest -R dist_storage_grpc` green; `message_server` links.
- Post-upgrade ownership review: no new allocation/object-passing regressions; CQ self-delete still deferred-only; hole-punch FD still `unique_fd` → arg → Connect worker.
