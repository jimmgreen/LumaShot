# Minimal clipboard preview

- Image windows fit decoded image proportions inside work-area bounds, with minimum chrome size. Content always preserves aspect ratio. Text and file lists retain a fixed 540 x 460 DIP initial size.
- Preview uses the same normal-HWND DirectComposition presentation and system acrylic/corners as the clipboard panel. No window region or opaque inner card.
- Text/code content supports mouse character selection, Shift-click extension, Ctrl+A, Ctrl+C, scrolling and Page Up/Down/Home/End. DirectWrite hit testing uses the same layout drawn by LumaText, including surrogate-pair cluster boundaries.
- Copy without a selection delegates to the original clipboard item; copying a selection exports only that Unicode text. The display limit is 65,536 UTF-16 units, with an explicit truncation hint; the original item remains intact.
- Clicking text can activate the preview without folding its owner or replacing the original paste target.
- Existing supported preview content remains text, clipboard images and file lists. This change does not add PDF/Office/media decoding depicted in earlier concept images.

Validation uses synthetic fixtures only: portrait/wide/square image geometry; fixed-size restoration; Chinese/emoji and scrolled multiline selection; Ctrl+A; LumaText rendering; corner coverage and no-region assertions; resource reuse and release checks. DirectComposition resources are detached and committed before device release, followed by driver trim. Resource benchmarks warm asynchronous compositor caches before assessing steady-state growth.
