# dist-storage

`dist-storage` is a C++ distributed-storage playground built around gRPC services,
Protocol Buffers messages, and OpenSSL-backed peer authentication.

The repository contains reusable common libraries (`crypto`, `peer`, `grpc`,
generated proto code) and several minion binaries that use them.

## Highlights

- C++ codebase with CMake-based builds
- gRPC + Protobuf communication stack
- OpenSSL certificate/signature flow for peer trust
- Shared common libraries under `common/`
- Executable services/tools under `minion/`

## Repository layout

- `common/crypto/` - OpenSSL helpers, signer/verifier, certificate utilities
- `common/peer/` - peer trust/auth store and signed-message validation
- `common/grpc/` - gRPC transport helpers (resolver, SSL endpoint, engine)
- `common/proto/` - `.proto` definitions and generated code
- `minion/message/` - message service binary (`message_server`)
- `minion/obj_store/` - object store executable (`obj_store`)
- `minion/resource_guard/` - resource guard executable (`resource_guard`)
- `minion/planner/parse/` - query parser target(s)

## Requirements

Typical Linux development environment with:

- CMake (modern version with C++ support)
- C++ compiler (GCC or Clang)
- OpenSSL development headers/libraries
- Git + internet access (first configure/build fetches gRPC dependencies)

The project fetches gRPC and related dependencies with CMake `FetchContent`.

## Build

Recommended out-of-source build:

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

If you need to reconfigure from scratch:

```bash
rm -rf build
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

## Main targets

After build, useful targets include:

- `message_server`
- `obj_store`
- `resource_guard`
- `dist_storage_crypto`
- `dist_storage_peer`
- `dist_storage_grpc`
- `my_proto_lib`

You can build a single target:

```bash
cmake --build build --target message_server -j"$(nproc)"
```

## Generated Protobuf code

Proto sources live in `common/proto/*.proto`.
Generated C++ and Python files are produced by the `common/proto/CMakeLists.txt`
generation step.

In this repository, generated C++ headers/sources are written to:

- `common/proto/cc/`

Consumers should link against `my_proto_lib` rather than manually wiring include
paths.

## Running binaries

Built binaries are typically placed under the build tree mirroring source
directories. Example commands:

```bash
./build/minion/message/message_server
./build/minion/obj_store/obj_store
./build/minion/resource_guard/resource_guard
```

Some binaries require runtime configuration/arguments depending on your setup.

## Development notes

- Keep changes scoped; avoid unrelated refactors.
- Prefer linking existing common libraries rather than duplicating logic.
- For crypto/auth changes, rebuild at least:
  - `dist_storage_crypto`
  - `dist_storage_peer`
  - dependents (`message_server`, etc.)

## Documentation

- **[AGENTS.md](AGENTS.md)**: project conventions, pitfalls, and contributor/agent guidance.
- Module-specific docs/comments are kept near their code.
