# ViitorX PointCloud Viewer — Feature-Guide PPT (handover)

Self-contained package to build (and rebuild/edit) the feature-guide slide deck.
Everything the deck needs is in this folder — no external/temp paths.

## Contents

| Path | What |
|------|------|
| `ViitorXPC_Feature_Guide.pptx` | The built deck — 23 slides, real screenshots. Open in PowerPoint / Google Slides. |
| `build.js` | pptxgenjs generator. Edit this to change slides, then rebuild. |
| `package.json` | Declares the one dependency (`pptxgenjs`). |
| `img/` | All screenshots the deck embeds (see list below). |
| `FEATURE_INVENTORY.md` | Source-of-truth list of every viewer feature + how to use it (verified against `src/viewer/main.cpp`). The deck's script. |

## Build

```bash
cd docs/ppt
npm install            # pulls pptxgenjs
node build.js          # writes ViitorXPC_Feature_Guide.pptx here
```

Recommended (pptxgenjs writes an uncompressed zip — recompress to shrink it):

```bash
python /path/to/pptx-skill/scripts/rezip.py ViitorXPC_Feature_Guide.pptx
```

## Visual QA (needs a renderer)

This deck was built on a machine with **no PowerPoint or LibreOffice**, so
slide-image QA could not run — only content QA (unzip + `<a:t>` text check)
was done. Before shipping, render and eyeball for overflow/overlap:

```bash
soffice --headless --convert-to pdf ViitorXPC_Feature_Guide.pptx
pdftoppm -jpeg -r 150 ViitorXPC_Feature_Guide.pdf slide
# inspect slide-*.jpg — watch infoCard text overflow and the slide-3 legend
```

## Design system (in build.js)

- Layout `WIDE` (13.3 × 7.5"). Dark theme: bg `0E1524`, cards `1A2740`,
  text `EAF0F8`, muted `93A1B5`, teal accent `35D0C4`, amber `F2A93B`.
- Helpers: `shot()` framed screenshot, `header()` numbered title,
  `infoCard()` heading + bullets, `caption()`. Screenshots are 1296×759
  (AR 1.7075) — keep that ratio when swapping images.

## Screenshots (`img/`)

Full-window (1296×759): `hero`, `welcome`, `convert_source/quality/advanced`,
`color_elev/int/class`, `measure`, `slice`, `campath`, `stereo`, `perf`,
`stats`, `palette`, `prefs_general`, `prefs_input`, `prefs_web`.
Full-window (1858×1096, AR passed to `shot()` explicitly):
`convert_photos` — wizard photo mode (v1.0.61): 271-image scan, GPS found,
engine picker, ODM blocked + board-specific BIOS steps.
Cropped: `menu_{file,edit,view,tools,window,help}_crop` (585×296),
`panel_display` / `panel_anno` (Properties-panel crops).

### Redactions (do NOT un-redact for shared copies)
- `prefs_web.png` — web-remote serving URL/IP + Driver & Viewer PINs blacked out.
- `slice.png` — output path (contained a username) blacked out.

## Re-capturing screenshots

App: `ViitorXPCViewer_v1056.exe` (v1.0.56), sample cloud
`C:\UnrealProject\model\Tikal-13.vxpc`. **Keyboard SendKeys does not reach this
SDL/OpenGL app** — drive it with mouse clicks + menus only (every shortcut has a
menu equivalent). Launch triggers a Windows Firewall prompt that covers the
viewport; dismiss it before framing (View ▸ Frame All). Stereoscopic (F9) can
only be exited via keyboard, so capture it last, then kill the process.
