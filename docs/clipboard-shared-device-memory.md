# Clipboard shared graphics device (2026-09-20)

The clipboard panel and preview share one D3D11 device and immediate context per UI thread. Each window retains a separate DirectComposition device, visual, HWND target and swap chain, so closing a preview releases its composition resources independently. The thread-local cache holds a weak pointer; releasing the last window frees the shared device. Device removal invalidates the cache and each affected window rebuilds on its next paint. Hardware/WARP fallback, alpha format, system corners and acrylic are preserved.

This follows the Windows composition model: window targets own visual trees, and D3D/DXGI supplies their image content. The D3D immediate context is used only on its UI thread.
https://learn.microsoft.com/en-us/windows/win32/directcomp/architecture-and-components
https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondevice-createtargetforhwnd

The initial experiment also shared the DirectComposition device. It retained more memory after hiding previews, so only D3D sharing is retained. No glyph cache reduction, history removal, working-set purge or periodic rendering was added.

## Evidence and limits

The 100-entry synthetic audit explicitly presents both windows. It asserts that the D3D device is shared, swap chains/targets differ, closing the preview leaves the list device usable, and disabling the panel expires the final weak reference. Native appearance tests cover light/dark acrylic, rounded edges and text after trimming. Four functional/resource suites and the native appearance test pass.

Previous build (build/shared-device-baseline.log), preview-switch cycle 20: private commit 146477056 bytes. New build (build/Testing/Temporary/LastTest.log): 123588608 bytes, about 21.83 MiB lower. Folded after idle trim: previous 108924928, new 114753536 bytes; this run was about 5.56 MiB higher, so no folded-memory improvement is claimed. The hidden preview's initial sample now explicitly pumps and paints before checking device identity; compare warmed switch/fold samples, not the initial preview-open sample. Reopen after folding: 68.940 ms; rapid Space reopen median: 16.108 ms. Timing and allocator/driver retention vary between runs.

The folded audit reports zero thumbnail/preview pixel caches, no payload queue bytes, and a 10752-byte list surface. That rules out original clipboard images remaining in those owned caches as the explanation for the remaining whole-process memory. The loaded rasterizer/font/driver/runtime allocations are not fully attributed by this audit. The previous 16-to-4 MiB glyph-cache experiment had no clear benefit and remains reverted. Further idle-memory reductions need a separate allocation profile rather than smaller thumbnail caches.

These measurements are isolated process results, not a prediction for the user's installed app. Packaging does not install or restart the app. Additional repeat-run data: build/shared-device-confirmation.log. Installer verification: build/shared-device-installer-verification.json.

Repeat run passed: folded private commit 110989312 bytes (105.85 MiB), about 1.97 MiB above the baseline sample; folded reopen 65.174 ms. The observed folded range is therefore about 106–109 MiB, compared with the earlier baseline 104 MiB. This remains a tradeoff, not an idle-memory win.
