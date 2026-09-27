# Clipboard performance verification — 2026-09-16

Copying now prepares the DIB and optional file-drop payload in the existing export worker. The UI thread only publishes the prepared handles. Cancellation releases unpublished buffers and temporary files. Both screenshot completion and pinned-image copying use this path. Unmarked pins no longer copy their source Frame before preparing the clipboard.

Clipboard PNG encoding uses the WIC Up filter. Explicit PNG saves retain their original filter choice. JPEG conversion uses 32 rows of scratch BGR data (360 KiB at 3840 pixels wide) instead of allocating a second full image. BMP can submit the existing 32-bit pixels directly.

`lumashot_encoding_perf_test` compares the pre-change encoder with the production encoder using synthetic UI, gradient and noise images at 1920×1080 and 3840×2160. Each encoder timing is the average of three runs; decoding and fixture generation are excluded. These are local encoding measurements, not target-application paste latency.

| 4K fixture | PNG before / after (ms) | JPEG before / after (ms) |
| --- | ---: | ---: |
| UI | 184.0 / 98.2 | 55.9 / 46.5 |
| Gradient | 213.6 / 101.0 | 52.7 / 48.1 |
| Noise | 1264.7 / 846.1 | 133.1 / 122.2 |

PNG files were smaller in all six fixtures; JPEG/BMP sizes were unchanged. BMP timing was roughly unchanged. PNG/BMP decoded pixels and dimensions are checked exactly, JPEG is compared with the old encoder's decoded pixels, and a nonopaque PNG verifies alpha preservation. Raw results are generated in `build/encoding_perf_results.csv`.

`lumashot_clipboard_perf_test` separately measures synchronous DIB preparation/publication against preparation plus the new UI publication step, using medians after warmup. `clipboard_file` checks publication ownership, bottom-up orientation, source lifetime, cancellation, invalid dimensions and file/image compatibility. `pin_interaction` also exercises the real asynchronous pinned-image copy.

The final clipboard-only run measured 1080p synchronous UI 7.74 ms vs staged publication 4.49 ms, and 4K 25.18 ms vs 15.89 ms. Background preparation was 2.98 ms / 8.94 ms respectively. Clipboard-listener lock waits are excluded from these successful-call medians; timings vary with system clipboard consumers and are not a guarantee for another application's paste action.

