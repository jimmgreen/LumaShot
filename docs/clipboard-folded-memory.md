# Folded clipboard memory investigation (2026-09-20)

The installed application was read without touching its clipboard or preferences: PID 21720, folded window 28x96, no preview window, working set 101.87 MiB and private commit 110.28 MiB stable over 24 seconds. Its executable matched the previous build. These figures cover the whole application.

## Corrected measurement

The previous synthetic resource audit called Render for the main list without Present. Its hidden test window consequently omitted the list DirectComposition device. The audit now explicitly presents both expanded and folded list frames. Earlier isolated numbers are not whole-application predictions.

With actual presentation, a diagnostic device release/rebuild changed folded private commit from 125464576 to 111845376 bytes. Rebuilding the device was not retained as the implementation.

## Shipped change

Ten seconds after folding, a one-shot UI timer clears unused graphics pipeline bindings, flushes the context and invokes IDXGIDevice3::Trim. The displayed swap chain, visual and acrylic window are retained. Opening the panel cancels the timer; disabling/destroying the panel removes it. No periodic rendering, working-set purge, forced paging, history deletion or thumbnail cache reduction is used.

Microsoft documents that Trim discards internal graphics-driver buffers and should run when going idle, after ClearState, since subsequent rendering can briefly rebuild buffers:
https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgidevice3-trim

Initial same-process measurement: folded private commit 125534208 -> 107560960 bytes (17.14 MiB less), total working set 143093760 -> 136384512 bytes. Reopening including fixture settlement measured 67.425 ms. These are synthetic-process results, not a promised decrease in the user's full running app.

A separate 4 MiB versus 16 MiB LumaText cache-budget experiment showed no clear memory improvement in this workload (trimmed private commit 109330432 bytes versus 107560960). That change was reverted. The final audit waits for the real idle timer rather than manually dispatching it; appearance regression captures both light and dark folded acrylic after trimming.

Final evidence: build/fold-memory-final-tests.log, build/Testing/Temporary/LastTest.log, build/fold-memory-appearance.log. Installer validation is recorded in build/fold-memory-installer-verification.json. The installed user application is not replaced by the test or packaging workflow.

Final restored-16-MiB build: real folded timer reduced private commit from 127860736 to 108924928 bytes (18.06 MiB), working set from 143237120 to 136425472 bytes (6.50 MiB). Reopen including fixture settlement: 71.821 ms. Four focused regressions plus the desktop appearance regression passed.
