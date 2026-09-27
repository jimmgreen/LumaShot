# Clipboard syntax highlighting

## Implementation

Lexilla 5.5.3 is statically linked with seven lexer implementations (CPP, Python, JSON, Bash, PowerShell, SQL, Markdown). The CPP lexer is configured separately for C/C++, C#, JavaScript and TypeScript. Only Scintilla 5.6.6 interface headers are included; no editor control, browser or additional runtime DLL is shipped. Official archives and SHA-256 values are pinned in deps/lexilla-source.json. Both upstream licenses ship with the installer.

Supported selections: automatic, plain text, C/C++, C#, Python, JavaScript, TypeScript, JSON, Bash, PowerShell, SQL and Markdown. Automatic detection is intentionally heuristic; ambiguous or unsupported snippets remain plain text and users can override the language in the header. Markdown highlights the original source, not a rendered document; fenced code receives a code-block colour rather than nested-language parsing. Lexical highlighting does not provide compiler semantic type resolution.

One background worker retains only the latest pending bounded request. Generation checks discard stale results. UTF-8 byte styles are mapped to UTF-16 character ranges without splitting surrogate pairs. Lexing does not run while scrolling or selecting text. Workers are joined before preview destruction.

LumaText renders the original DirectWrite layout once into a viewport-sized coverage mask. Token rectangles apply theme colours to that mask, preserving the existing hit testing, glyph shaping, clipboard text and selection geometry. No colour markup is inserted into copied text.

## Verification

Tests cover all selected languages; raw/verbatim strings; multiline comments; Markdown headings and fences; Unicode mapping; plain-text fallback; async stale-job rejection; actual colour pixels; light/dark/high-DPI rendering; language menu override; unchanged character selection and resource release.

Local Release x64 measurement: main executable grew from 2,199,040 to 2,433,024 bytes (233,984 bytes / about 229 KiB). The lexer-only 65,000-character C++ benchmark averaged about 2.9 ms over 100 passes. Its sampled private-memory increase was about 0.94 MiB, with final increase about 1.09 MiB. These are fixture-specific lexer measurements, not total preview-window memory or a worst-case guarantee; rendered masks and existing graphics resources are additional.
