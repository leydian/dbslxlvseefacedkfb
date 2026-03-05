# VXAvatar In-Process Deflate Update Report (2026-03-02)

## Scope

This report summarizes the migration from external PowerShell extraction to in-process deflate decoding for `.vxavatar` ZIP entries (`method=8`).

## Implemented Changes

1. Loader path hardening (`src/avatar/vxavatar_loader.cpp`)
- Removed external extraction path:
  - `ReadZipEntryViaPowershell(...)`
  - `std::system("powershell ...")`
  - `W_PARSE: VX_EXTERNAL_EXTRACTOR` warning
- Added in-process ZIP entry decode:
  - local header + payload range resolver (`ResolveZipEntryDataRange`)
  - raw-deflate decode (`ReadDeflateZipEntry`)
  - shared payload dispatch still handles `stored(0)` and `deflate(8)`

2. Error/diagnostic behavior
- Unsupported compression method:
  - `VX_UNSUPPORTED_COMPRESSION`
- Deflate payload read/decode corruption:
  - `VX_SCHEMA_INVALID`
- Stage contract preserved:
  - `parse -> resolve -> payload -> runtime-ready`

3. Build integration
- Added vendored `miniz` implementation under `third_party/miniz`.
- Added `src/common/miniz_impl.cpp` to compile miniz implementation units into `vsfclone_core`.
- Removed temporary dependency on system `ZLIB` discovery.

## Verification Summary

Build:
- `cmake --build build_vxdeflate --config Release` completed successfully.

Runtime:
1. `sample/demo_mvp.vxavatar`
- `Format: VXAvatar`
- `Compat: full`
- `ParserStage: runtime-ready`
- `PrimaryError: NONE`
- no external extractor warning emitted

2. `sample/demo_mvp.vxa2` (cross-check)
- unchanged behavior (`Compat: partial`, `PrimaryError: VXA2_ASSET_MISSING`)

3. Truncated VX sample (`build/tmp_vx/demo_mvp_truncated.vxavatar`)
- `Compat: failed`
- `PrimaryError: VX_SCHEMA_INVALID`
- process exits normally (no crash)

## Known Limitations

- ZIP methods outside `stored(0)` and `deflate(8)` remain unsupported in MVP.
- Material override semantics remain placeholder policy (not part of this pass).

## Next Steps

1. Add loader-focused regression tests for:
- stored vs deflate entries
- malformed deflate payload
- mismatched local-header vs central-directory metadata
2. Add performance benchmark for large payload decode latency and memory footprint.
3. Continue VXA2 migration tooling (`.vxavatar -> .vxa2`) as separate track.
