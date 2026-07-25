# raylib paste-chip hover POC

A small raylib GUI that preserves a large paste as raw text while rendering it as one atomic `[Pasted: N lines]` chip. Like Grok's normal prompt mode, the demo folds pastes of at least four lines (or more than 10,000 bytes). Moving the mouse over the chip opens a seven-line preview; use the mouse wheel to scroll through the complete paste. Double-clicking expands the chip back to its original text.

## Controls

- **Ctrl/Cmd+V**: paste clipboard text
- **Hover a paste chip**: preview its raw content
- **Mouse wheel while hovering**: scroll the preview three lines at a time
- **Double-click a chip**: expand it inline
- **Backspace**: remove the last typed character or chip
- **Esc**: clear
- **Ctrl+R**: restore the built-in example

## Build

CMake downloads raylib 5.5 and Fira Code automatically, then copies the font beside the executable:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\raylib_poc_hover.exe
```

With a single-config generator, the executable is usually `build/raylib_poc_hover` (or `.exe` on Windows).

## What was adapted from Grok

Investigated latest `xai-org/grok-build` main at `6e38642`:

- `xai-ratatui-textarea/src/textarea.rs`: atomic text elements retain a raw buffer range, render alternate display text, calculate screen bounds, and hit-test mouse movement to emit hover enter/leave events.
- `xai-grok-pager/src/views/prompt_widget/mod.rs`: multiline pastes become `KIND_PASTE` elements; the full text is recovered from the element range; an overlay is selected for the element near the cursor.
- `xai-grok-pager-render/src/render/preview_overlay.rs`: the preview shows the first three and last three lines with an omitted-lines separator in a bordered overlay.

The important separation is **model content vs. display content**. This POC stores each paste's complete text in `Segment::content`, draws only `ChipLabel(segment)` in the editor, and stores the chip's `Rectangle` during layout. Each frame uses `CheckCollisionPointRec` to choose the hovered paste and render its preview.

One distinction in current upstream: Grok's text-paste overlay is activated by the caret being on/right after the chip, while its generic mouse-hover event path currently drives image-chip previews. This POC deliberately connects the same element hit-testing idea to text-paste previews to demonstrate the requested mouse-hover behavior.

The build downloads **Fira Code** from Google Fonts under the SIL Open Font License and places `OFL.txt` beside the built font.
