# Font-memory root cause and fix (2026-09-20)

## Diagnosis

Windows heap ETW activation failed with OS access denied, despite running outside the filesystem sandbox. No trace was started and the synthetic executable's heap tracing remains disabled. We did not obtain allocation call stacks and do not claim stack attribution. The fallback was in-process GetProcessHeaps/HeapWalk on an isolated synthetic test, paired with source inspection and an A/B binary change. No user clipboard or preferences were inspected.

The folded process retained two busy allocations of 19704391 and 16883591 bytes. System Microsoft YaHei regular/bold collections measure 19704352 and 16883552 bytes, respectively (each allocation adds 39 bytes of allocator overhead). The bundled LumaText font bridge used vector::assign to copy the entire DirectWrite fragment. FontBlob stayed alive in the context/FreeType cache. These are font-file copies, not the bounded glyph bitmap cache, explaining why reducing that cache from 16 to 4 MiB did not help.

## Fix

Vendored the exact upstream base c21ea81f31293d51b066402730d7a4eb18ca2ce6; its public header text matches the previously bundled header. The sibling source tree was read only; its unrelated uncommitted changes were not imported. FontBlob now retains the DirectWrite stream and fragment, and releases the fragment via RAII after its last consumer. FreeType reads the fragment directly. Explicit owned memory/file font APIs retain their old behavior. Public ABI, font selection and rasterization parameters are unchanged.

API lifetime contract:
https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritefontfilestream-readfilefragment
https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritefontfilestream-releasefilefragment

The two full-size heap copies disappeared. A separate HeapOptimizeResources probe reclaimed only about 2 MiB; this experimental call is not added to the application. Diagnostic code runs only in the synthetic audit with LUMASHOT_PROFILE_HEAPS set.

## Results

Old diagnostic folded private commit: 109682688 bytes (104.60 MiB). Patched diagnostic: 74801152 (71.34 MiB), before experimental heap optimization. Final normal audit without that optimization: 72609792 (69.25 MiB). Earlier patched-sharing baseline folded readings were 106–109 MiB; normal variation prevents attributing every byte of difference. The known eliminated font payload is 36587904 bytes (34.89 MiB).

Final normal audit: preview-switch cycle 20 private commit 89415680 (85.27 MiB); folded working set 65536000 (62.50 MiB); disabled settled private commit 32718848 (31.20 MiB). Space reopen median 14.883 ms, p95 17.703 ms; folded reopen including fixture settlement 69.816 ms. The first diagnostic run had noisy timing, so response claims use the final normal regression. These are isolated test-process results; the running installed application is not changed or restarted and requires a fresh post-install observation.

## Validation and reproduction

- scripts/build-lumatext.bat: core /W4 /WX; 8 tests pass, including fragment lifetime and actual FreeType rendering after releasing bridge/font-face owners.
- build.bat: application build passes; 11 focused suites pass across clipboard, controls, settings, recording UI, text editing/export and rendering.
- Native appearance tests pass for light/dark acrylic and rounded corners.
- Five code-preview PNGs match the prior binary byte-for-byte (C#, Markdown, light, dark at 150%, selection). Panel scale comparisons are in build/font-fragment-panel-pixel-comparison.json.
- Logs: build/heap-large.log, build/font-fragment-memory.log, build/font-fragment-app-tests.log, build/font-fragment-build.log, build/font-fragment-appearance.log.
- Installer/payload SHA verification: build/font-fragment-installer-verification.json. New DLL SHA256: 30DC3858D2E0185AB18823897715B36F186F54CBFC33C354E0CFE3917E3CFC16.

Rebuild details and binary promotion steps are in deps/lumatext/README.md. Existing FreeType/HarfBuzz source caches were passed to CMake as read-only FetchContent overrides for this build; the upstream CMake also specifies pinned download hashes for a standalone rebuild.

The six panel images differ in their generated current-time labels (17:11 versus 17:38). Pixel comparison excluding only the four time-label rectangles found no other differences at 100%, 150% or 200%, in light or dark mode.
