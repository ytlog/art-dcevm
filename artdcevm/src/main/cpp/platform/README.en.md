# Platform code organization

English | [中文](README.md)

Only ARM64 is currently supported. There is one implementation across versions; version differences are confined to declarations taken from fixed source versions.

| Directory | Contents |
| --- | --- |
| `common/` | Shared implementation: structural redefinition engine, validation policy, schema inspection through public JVMTI, method retirement and runtime probes |
| `abi/arm64/common/` | Fixed ART declarations that are identical in Android 11–17 source, with source references and `static_assert` checks |
| `abi/arm64/androidNN/` | Only declarations that differ in Android NN source; each version has its own directory and does not contain other versions' declarations |
| `symbols/` | Named ELF symbol catalog, checked against device libraries by `tools/check-structural-symbols.py` |

Before adding code, establish whether there is a real version difference. Shared implementation belongs in `common/`; only differing declarations belong in a version directory. Do not copy the whole implementation. The current differences are the field arrays in `mirror::Class` (`ifields_`/`sfields_` in 11–15, `fields_` in 16–17) and Android 11's `ArtMethod`.

Capabilities depend on actual runtime symbols and runtime probes, rather than a model, SDK or build-number allowlist. Method-retirement hooks are installed only when all required exact names resolve. While ART can still fail a transaction, the engine checks each version's declarations against ART's own lookup services and the JVMTI view; it takes over member mapping only after selecting a matching layout. The SDK is used only to exclude candidates older than the platform and determine their order, because the ART module cannot predate the platform. Private capabilities are accessed only through named symbols, without deriving addresses or offsets from disassembly.
