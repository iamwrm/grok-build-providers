# IV-0009: Raylib paste-chip hover demonstration

**Status:** implemented and locally verified
**Implementation:** `projects/raylib_poc_hover/`
**Reference upstream:** `checkouts/grok-build` at exploration checkpoint `6e38642`
**Initial implementation checkpoint:** repository commit `b0e90c3`
**Doctrine:** [DC-0001](DC-0001-agentic-workspace.md) — read before changing or retiring this initiative

## Lifecycle map

- **Why this exists:** demonstrate, in a standalone graphical application, how
  a large paste can retain its complete model content while occupying one
  compact editor token with an inspectable preview.
- **Durable implementation:** source, build definition, controls, and
  reproduction notes under `projects/raylib_poc_hover/`. Generated toolchains,
  downloaded font data, build trees, and executables are ignored.
- **Known consumers:** humans evaluating the interaction and agents using the
  POC as a reference. No Grok production code consumes this project.
- **External dependency:** CMake fetches raylib 5.5 and a hash-pinned Fira Code
  font from public upstreams.
- **Evidence route:** configure, build, and run the project using
  [Reproduction and evidence](#reproduction-and-evidence), then exercise a
  paste large enough to become a chip.

## Intent and user requirements

The initiating request was to study Grok's compact pasted-text behavior and
build a raylib GUI that exposes pasted content when the mouse moves over the
compact representation. Follow-up requirements established that the POC must:

- use Fira Code rather than raylib's default font;
- render sharply and at a useful size on a 4K Windows display at 150% scaling;
- keep very large pastes, including 100-line input, compact;
- allow the user to scroll through the complete hover preview;
- avoid committing downloaded data, toolchains, build products, or binaries.

## External knowledge and observed upstream behavior

The Grok exploration used the latest upstream checkout available at the
recorded checkpoint:

- `xai-ratatui-textarea/src/textarea.rs` stores atomic `TextElement` ranges,
  renders alternate display content, records screen hit regions, and emits
  hover-enter/hover-leave events from mouse-move hit testing.
- `xai-grok-pager/src/views/prompt_widget/mod.rs` turns sufficiently large
  pastes into `KIND_PASTE` elements while retaining their raw text. Its text
  preview is selected when the caret is on or immediately after a paste chip;
  the generic hover path currently drives image preview state.
- `xai-grok-pager-render/src/render/preview_overlay.rs` renders a bounded,
  bordered preview and, in upstream, collapses long content to the first and
  last three lines with an omitted-line marker.

The relevant principle is the separation of **model content** from **display
content**. The POC adapts Grok's element hit-testing idea to text-paste hover;
it does not claim that current upstream activates text previews by hover.

## Requirements and invariants

- A paste of at least four lines, or more than 10,000 bytes, becomes one
  `[Pasted: N lines]` segment.
- `Segment::content` always retains the complete normalized paste; the chip
  label is display-only.
- A chip rectangle is calculated during layout and used for mouse hit testing.
- Hover opens a seven-line contiguous preview. The mouse wheel advances three
  lines, including accumulated smooth-wheel input; the title reports the
  visible range and a scrollbar reports relative position.
- Double-click expands the raw text without changing it.
- Windows high-DPI support is enabled before window creation. The 720-unit-tall
  design scales to the current window while its usable width grows from 1040
  to 1280 units on wider aspect ratios. Raylib renders the initial 1440×900
  logical window into the monitor-scaled framebuffer. After a resize, raylib
  5.5 framebuffer-sized Windows reports are normalized back to logical units.
- Windows builds use the GUI subsystem, so launching the executable from
  Explorer does not create a separate console window.
- Fira Code is loaded at 96 pixels and filtered for clean scaled rendering.
- Font and license downloads are SHA-256 checked. They are copied beside the
  executable at build time but never tracked in this repository.

## Implementation locations and consumers

| Location | Lifecycle role |
|---|---|
| `projects/raylib_poc_hover/src/main.cpp` | paste model, layout, DPI scaling, chip hit testing, scrolling preview, controls |
| `projects/raylib_poc_hover/CMakeLists.txt` | raylib fetch, checked Fira Code download, build and runtime asset copy |
| `projects/raylib_poc_hover/README.md` | user controls, build procedure, and upstream adaptation notes |
| `projects/raylib_poc_hover/.gitignore` | excludes local toolchain, build tree, and generated/downloaded assets |

No patch under `patches/grok-build/` depends on this POC. If production Grok
behavior changes, update this initiative only when the POC is intended to
remain a faithful interaction reference.

## Decisions and assumptions

1. **Hover rather than caret activation:** deliberate adaptation of Grok's
   element event mechanism to the requested graphical interaction.
2. **Scrollable contiguous preview:** replaces the initial first/last summary
   so a 100-line paste remains inspectable without expansion.
3. **Responsive logical canvas:** scales positions, font sizes, borders, and
   clipping together while allowing wider windows to add usable horizontal
   space. Hit rectangles are laid out before input each frame so they remain
   aligned during a resize.
4. **Build-time font retrieval:** keeps the repository text/source-only while
   preserving reproducible font identity through hashes.
5. **Assumption:** network access is available on a clean first configure to
   fetch raylib and Fira Code. Existing build caches permit later rebuilds.

## Non-goals

- Replacing Grok's terminal textarea or upstreaming raylib into Grok.
- A production text editor with arbitrary caret movement, selection, undo,
  persistence, or scrolling after a chip is expanded.
- Exact pixel parity with Grok's TUI themes.
- Bundling a compiler, CMake, raylib source, font binary, or executable.
- Full Unicode input/font coverage; the current demonstration focuses on the
  ASCII source-text interaction.

## Reproduction and evidence

From `projects/raylib_poc_hover/` with a C++17 compiler and CMake:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\raylib_poc_hover.exe
```

Single-config generators usually place the executable at
`build/raylib_poc_hover.exe`.

The implementation checkpoint was verified by:

- a clean CMake configure that downloaded hash-matching Fira Code and license
  files;
- a successful raylib 5.5 release build with warnings enabled for the POC;
- runtime initialization on a 3840×2160 monitor at 150% scaling, where raylib
  reported a 1440×900 logical screen and 2160×1350 render framebuffer;
- runtime font loading at 96 pixels;
- visual checks of the responsive layout and atomic nine-line example chip;
- code-path verification that a 100-line paste has a 94-line scroll range when
  seven lines are visible, while the stored `Segment::content` remains intact.

Manual interaction loop:

1. Paste four or more lines with Ctrl/Cmd+V.
2. Confirm one chip appears with the correct line count.
3. Hover the chip and confirm the preview title starts at `lines 1-7 of N`.
4. Scroll down and up; verify title range and scrollbar move in three-line
   steps and remain clamped.
5. Double-click and verify the original text expands.
6. Resize the window and verify typography, clipping, chip hit testing, and the
   preview scale together.

Evidence above is a checkpoint. Rerun this procedure after raylib, font,
window-scaling, or interaction changes.

## Retirement guidance

Start here, then remove `projects/raylib_poc_hover/` and its links from
`docs/repo.md`. Search for `raylib_poc_hover`, `IV-0009`, and
`raylib paste` before deletion. Keep any interaction knowledge that has gained
a separate production consumer by moving it to that consumer's initiative or
an applicable doctrine first.
