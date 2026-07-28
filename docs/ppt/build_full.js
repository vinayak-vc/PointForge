/**
 * ViitorX PointCloud Viewer — COMPLETE Feature Guide deck generator.
 *
 * Separate from build.js (the original 22-slide guide, kept untouched).
 * Output: ViitorXPC_Complete_Feature_Guide.pptx
 *
 * Build:   npm install && node build_full.js
 *
 * Content source of truth: FEATURE_INVENTORY.md (verified against src/viewer/main.cpp).
 * Screenshots: img/ (1296x759 full-window unless noted — see HANDOVER.md).
 *
 * OPTIONAL ASSETS (auto-embedded when present, graceful fallback when absent):
 *   video/*.mp4|mov|m4v      -> first file (sorted) embeds on the "Video Demo" slide
 *   img/jobs_console.png     -> Jobs & Console slide screenshot
 *   img/scene_multi.png      -> Scene panel slide (else: crop from hero.png)
 *   img/edl_off.png + img/edl_on.png -> EDL before/after comparison
 *   img/shortcuts_overlay.png -> F1 overlay screenshot on the palette slide
 * See CAPTURE_LIST.md for how to capture each one in-app.
 */
const pptxgen = require("pptxgenjs");
const path = require("path");
const fs = require("fs");

const IMG = (n) => path.join(__dirname, "img", n);
const OPT = (n) => (fs.existsSync(IMG(n)) ? n : null); // optional screenshot

// first video in video/ (if any)
let DEMO_VIDEO = null;
const VID_DIR = path.join(__dirname, "video");
if (fs.existsSync(VID_DIR)) {
  const v = fs.readdirSync(VID_DIR).filter((f) => /\.(mp4|mov|m4v)$/i.test(f)).sort();
  if (v.length) DEMO_VIDEO = path.join(VID_DIR, v[0]);
}

const AR = 1296 / 759;            // full-window screenshot aspect ratio
const NAT = { w: 1296, h: 759 };  // native px of full-window shots
const W = 13.3, H = 7.5;

// ---- palette (same design system as build.js) ----
const BG    = "0E1524";   // deep navy-black
const BG2   = "0B1120";   // darker (title/close)
const CARD  = "1A2740";   // card surface
const CARD2 = "223256";   // lighter card
const TXT   = "EAF0F8";   // primary text
const MUT   = "93A1B5";   // muted
const ACC   = "35D0C4";   // teal accent
const ACC2  = "F2A93B";   // amber accent (sparing)

const pres = new pptxgen();
pres.defineLayout({ name: "WIDE", width: W, height: H });
pres.layout = "WIDE";
pres.author = "ViitorX";
pres.title = "ViitorX PointCloud Viewer — Complete Feature Guide";

const shadow = () => ({ type: "outer", color: "000000", blur: 9, offset: 3, angle: 90, opacity: 0.45 });
function bg(slide, c = BG) { slide.background = { color: c }; }

// framed screenshot: rounded card behind + shadow, image on top. returns image height.
function shot(slide, file, x, y, w, ar = AR) {
  const h = w / ar;
  const p = 0.07;
  slide.addShape(pres.shapes.ROUNDED_RECTANGLE, {
    x: x - p, y: y - p, w: w + 2 * p, h: h + 2 * p,
    fill: { color: CARD }, line: { color: CARD2, width: 1 },
    rectRadius: 0.06, shadow: shadow(),
  });
  slide.addImage({ path: IMG(file), x, y, w, h });
  return h;
}

// framed CROP of a full-window screenshot. rect = {px,py,pw,ph} in native pixels.
// w = displayed width of the cropped region (inches). returns displayed height.
function cropShot(slide, file, x, y, w, rect, nat = NAT) {
  const sc = w / rect.pw;                     // inches per native px
  const h = rect.ph * sc;
  const p = 0.07;
  slide.addShape(pres.shapes.ROUNDED_RECTANGLE, {
    x: x - p, y: y - p, w: w + 2 * p, h: h + 2 * p,
    fill: { color: CARD }, line: { color: CARD2, width: 1 },
    rectRadius: 0.06, shadow: shadow(),
  });
  slide.addImage({
    path: IMG(file), x, y, w: nat.w * sc, h: nat.h * sc,
    sizing: { type: "crop", x: rect.px * sc, y: rect.py * sc, w, h },
  });
  return h;
}

// slide header with auto number chip + title
let SLIDE_NO = 0;
function header(slide, title, kicker) {
  const num = ++SLIDE_NO;
  slide.addShape(pres.shapes.OVAL, { x: 0.55, y: 0.42, w: 0.62, h: 0.62, fill: { color: ACC } });
  slide.addText(String(num), { x: 0.55, y: 0.42, w: 0.62, h: 0.62, align: "center", valign: "middle",
    fontFace: "Cambria", fontSize: 22, bold: true, color: BG });
  if (kicker) slide.addText(kicker.toUpperCase(), { x: 1.35, y: 0.42, w: 10, h: 0.3, margin: 0,
    fontFace: "Arial", fontSize: 11, bold: true, color: ACC, charSpacing: 3 });
  slide.addText(title, { x: 1.33, y: kicker ? 0.66 : 0.5, w: 11.4, h: 0.62, margin: 0,
    fontFace: "Cambria", fontSize: 30, bold: true, color: TXT });
}

// info card: heading + bullets (items: strings or {t, c?, bold?, b?:false})
function infoCard(slide, x, y, w, h, heading, items, opt = {}) {
  slide.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y, w, h,
    fill: { color: opt.fill || CARD }, line: { type: "none" }, rectRadius: 0.09, shadow: shadow() });
  let cy = y + 0.22;
  if (heading) {
    slide.addText(heading, { x: x + 0.28, y: cy, w: w - 0.5, h: 0.4, margin: 0,
      fontFace: "Arial", fontSize: 15, bold: true, color: opt.headColor || ACC });
    cy += 0.5;
  }
  const runs = items.map((it) => {
    const isObj = typeof it === "object";
    const t = isObj ? it.t : it;
    return { text: t, options: {
      bullet: (isObj && it.b === false) ? false : { code: "2022", indent: 14 },
      breakLine: true, fontFace: "Arial", fontSize: opt.fontSize || 13.5,
      color: (isObj && it.c) ? it.c : TXT, paraSpaceAfter: opt.gap != null ? opt.gap : 7, bold: !!(isObj && it.bold) } };
  });
  slide.addText(runs, { x: x + 0.28, y: cy, w: w - 0.5, h: h - (cy - y) - 0.2, valign: "top", margin: 0 });
}

// step card: numbered "How to use" steps (amber numerals)
function stepCard(slide, x, y, w, h, heading, steps, opt = {}) {
  slide.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y, w, h,
    fill: { color: opt.fill || CARD }, line: { type: "none" }, rectRadius: 0.09, shadow: shadow() });
  let cy = y + 0.22;
  if (heading) {
    slide.addText(heading, { x: x + 0.28, y: cy, w: w - 0.5, h: 0.4, margin: 0,
      fontFace: "Arial", fontSize: 15, bold: true, color: ACC });
    cy += 0.5;
  }
  const runs = [];
  steps.forEach((t, i) => {
    runs.push({ text: `${i + 1}   `, options: { bold: true, color: ACC2, fontFace: "Consolas",
      fontSize: opt.fontSize || 13.5, breakLine: false } });
    runs.push({ text: t, options: { fontFace: "Arial", fontSize: opt.fontSize || 13.5, color: TXT,
      breakLine: true, paraSpaceAfter: opt.gap != null ? opt.gap : 9 } });
  });
  slide.addText(runs, { x: x + 0.28, y: cy, w: w - 0.5, h: h - (cy - y) - 0.2, valign: "top", margin: 0 });
}

function caption(slide, x, y, w, text) {
  slide.addText(text, { x, y, w, h: 0.3, align: "center", margin: 0,
    fontFace: "Arial", fontSize: 11, italic: true, color: MUT });
}

// bottom one-liner: "Label:  text"
function footNote(slide, label, text, y = 6.55) {
  slide.addText([{ text: label + ":  ", options: { bold: true, color: ACC } },
    { text, options: { color: TXT } }],
    { x: 0.55, y, w: 12.2, h: 0.4, margin: 0, fontFace: "Arial", fontSize: 13 });
}

/* ================= 1. TITLE ================= */
{
  const s = pres.addSlide(); bg(s, BG2);
  shot(s, "hero.png", 6.7, 1.7, 6.2);
  s.addText("VIITORX", { x: 0.7, y: 1.4, w: 6, h: 0.5, margin: 0, fontFace: "Arial", fontSize: 16, bold: true, color: ACC, charSpacing: 6 });
  s.addText("PointCloud Viewer", { x: 0.66, y: 1.8, w: 6.1, h: 1.6, margin: 0, fontFace: "Cambria", fontSize: 50, bold: true, color: TXT, lineSpacingMultiple: 0.95 });
  s.addText("Complete Feature Guide", { x: 0.7, y: 3.4, w: 6, h: 0.5, margin: 0, fontFace: "Arial", fontSize: 20, color: MUT });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 0.7, y: 4.1, w: 1.5, h: 0.5, fill: { color: CARD2 }, rectRadius: 0.25 });
  s.addText("v1.0.56", { x: 0.7, y: 4.1, w: 1.5, h: 0.5, align: "center", valign: "middle", margin: 0, fontFace: "Arial", fontSize: 14, bold: true, color: ACC });
  s.addText("Every feature, with screenshots and step-by-step usage", { x: 0.7, y: 4.9, w: 5.9, h: 0.4, margin: 0, fontFace: "Arial", fontSize: 13.5, color: TXT });
  s.addText("LAS · LAZ · E57 · PLY · PTS · XYZ  →  .vxpc", { x: 0.7, y: 5.3, w: 5.9, h: 0.4, margin: 0, fontFace: "Arial", fontSize: 12.5, bold: true, color: MUT, charSpacing: 1 });
  caption(s, 6.7, 6.55, 6.2, "Tikal-13 — 12.4M points, streamed live");
  s.addNotes("Complete feature guide for ViitorX PointCloud Viewer v1.0.56. Built from FEATURE_INVENTORY.md; every claim verified against src/viewer/main.cpp.");
}

/* ================= 2. CONTENTS ================= */
{
  const s = pres.addSlide(); bg(s);
  s.addText("WHAT'S INSIDE", { x: 0.7, y: 0.5, w: 10, h: 0.3, margin: 0, fontFace: "Arial", fontSize: 11, bold: true, color: ACC, charSpacing: 3 });
  s.addText("Contents", { x: 0.66, y: 0.74, w: 11.4, h: 0.62, margin: 0, fontFace: "Cambria", fontSize: 30, bold: true, color: TXT });
  const groups = [
    ["Getting started", ["Overview & workspace", "Toolbar & status bar", "Opening clouds", "Menus at a glance"]],
    ["Importing scans", ["Convert wizard — Source", "Quality presets & Advanced", "Destination & review", "Background jobs & console"]],
    ["Viewing", ["Navigation & camera", "Color modes (5)", "Display tuning & EDL", "Scene panel — multi-cloud"]],
    ["Tools", ["Measure distances", "Annotations & bookmarks", "Clip box & slice export"]],
    ["Present & export", ["Camera paths", "Video export + demo", "Screenshots & zen mode", "Stereoscopic 3D"]],
    ["System", ["Performance monitor", "Command palette", "Preferences", "Web remote & controllers", ".vxpc package format", "Keyboard shortcuts"]],
  ];
  const cw = 3.95, gx = 0.2, x0 = 0.55;
  groups.forEach((g, i) => {
    const col = i % 3, row = Math.floor(i / 3);
    const cx = x0 + col * (cw + gx), cy = 1.6 + row * 2.75;
    const ch = row === 1 ? 2.85 : 2.55;
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: cx, y: cy, w: cw, h: ch, fill: { color: CARD }, rectRadius: 0.1, shadow: shadow() });
    s.addText(g[0], { x: cx + 0.25, y: cy + 0.18, w: cw - 0.5, h: 0.35, margin: 0, fontFace: "Arial", fontSize: 14, bold: true, color: ACC });
    const runs = g[1].map((t) => ({ text: t, options: { bullet: { code: "2022", indent: 12 }, breakLine: true,
      fontFace: "Arial", fontSize: 12, color: TXT, paraSpaceAfter: 4 } }));
    s.addText(runs, { x: cx + 0.25, y: cy + 0.6, w: cw - 0.45, h: ch - 0.8, valign: "top", margin: 0 });
  });
}

/* ================= 3. OVERVIEW ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "What is ViitorX?", "Overview");
  infoCard(s, 0.55, 1.7, 5.5, 4.9, "Built to open clouds bigger than RAM", [
    "Ingests very large scans (LAS/LAZ, E57, PLY, PTS/XYZ) — billions of points, larger than memory.",
    "Converts each scan into a streamable on-disk octree packaged as a single .vxpc file.",
    "Frustum culling + screen-space-error LOD keep only what you can see on the GPU.",
    "Async loading with an LRU GPU-memory budget — smooth navigation, bounded memory.",
    "Your bookmarks, measurements, annotations and camera paths travel inside the .vxpc.",
  ], { fontSize: 13.5, gap: 11 });
  const tiles = [
    ["Out-of-core", "Streams data far larger than RAM"],
    ["Octree LOD", "Screen-space-error detail control"],
    ["GPU-budgeted", "LRU eviction keeps a steady frame rate"],
    ["60 FPS", "12.4M points drawn interactively"],
  ];
  const tx = 6.35, tw = 3.25, th = 2.2, gx = 0.35, gy = 0.3;
  tiles.forEach((t, i) => {
    const cx = tx + (i % 2) * (tw + gx), cy = 1.9 + Math.floor(i / 2) * (th + gy);
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: cx, y: cy, w: tw, h: th, fill: { color: CARD }, rectRadius: 0.1, shadow: shadow() });
    s.addText(t[0], { x: cx + 0.25, y: cy + 0.3, w: tw - 0.5, h: 0.7, margin: 0, fontFace: "Cambria", fontSize: 25, bold: true, color: ACC });
    s.addText(t[1], { x: cx + 0.25, y: cy + 1.05, w: tw - 0.5, h: 1.0, margin: 0, fontFace: "Arial", fontSize: 13, color: TXT });
  });
  s.addNotes("Two-step workflow: convert once (wizard or pfconvert CLI) to a streamable octree, then view interactively. The .vxpc package is a single portable file.");
}

/* ================= 4. WORKSPACE TOUR ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "The Workspace", "Docked shell");
  const ix = 0.55, iy = 1.75, iw = 8.35; const ih = shot(s, "hero.png", ix, iy, iw);
  const marks = [
    [0.03, 0.05, "1"],   // menu bar
    [0.03, 0.085, "2"],  // toolbar
    [0.09, 0.40, "3"],   // scene
    [0.45, 0.55, "4"],   // viewport
    [0.90, 0.33, "5"],   // properties
    [0.42, 0.975, "6"],  // status bar
  ];
  marks.forEach(([fx, fy, n]) => {
    const mx = ix + fx * iw - 0.16, my = iy + fy * ih - 0.16;
    s.addShape(pres.shapes.OVAL, { x: mx, y: my, w: 0.34, h: 0.34, fill: { color: ACC2 }, line: { color: BG, width: 1.5 } });
    s.addText(n, { x: mx, y: my, w: 0.34, h: 0.34, align: "center", valign: "middle", margin: 0, fontFace: "Arial", fontSize: 13, bold: true, color: BG2 });
  });
  const legend = [
    ["1", "Menu bar", "File · Edit · View · Tools · Window · Help"],
    ["2", "Toolbar", "Open, Convert, Frame, tools, color & quality"],
    ["3", "Scene panel", "Loaded clouds, visibility, add / close"],
    ["4", "Viewport", "Central 3D view — orbit, fly, measure"],
    ["5", "Properties", "Display, camera, annotations, clip"],
    ["6", "Status bar", "Mode hint · GPU · points · FPS · version"],
  ];
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 9.25, y: 1.75, w: 3.5, h: 4.9, fill: { color: CARD }, rectRadius: 0.1, shadow: shadow() });
  let ly = 1.95;
  legend.forEach(([n, h1, d]) => {
    s.addText(n, { x: 9.45, y: ly, w: 0.35, h: 0.35, align: "center", valign: "middle", margin: 0, fontFace: "Arial", fontSize: 13, bold: true, color: ACC2 });
    s.addText([{ text: h1 + "\n", options: { bold: true, color: TXT, fontSize: 13.5 } }, { text: d, options: { color: MUT, fontSize: 10.5 } }],
      { x: 9.85, y: ly - 0.03, w: 2.75, h: 0.72, margin: 0, fontFace: "Arial", valign: "top", lineSpacingMultiple: 1.0 });
    ly += 0.79;
  });
  s.addNotes("Docked ImGui shell. Window menu toggles each panel; Reset Layout restores the default arrangement. Bottom dock (Jobs/Console/Performance) is closed by default.");
}

/* ================= 5. TOOLBAR & STATUS BAR ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Toolbar & Status Bar", "Getting started");
  // cropped strips from the hero full-window shot (native px rects)
  const h1 = cropShot(s, "hero.png", 0.55, 1.85, 12.2, { px: 0, py: 28, pw: 1296, ph: 52 });
  caption(s, 0.55, 1.85 + h1 + 0.1, 12.2, "Menu bar + toolbar — Open (with recent dropdown) · Convert… · Frame · Nav / Measure / Annotate / Clip · Color mode · Quality · Shot");
  const y2 = 1.85 + h1 + 0.55;
  const h2 = cropShot(s, "hero.png", 0.55, y2, 12.2, { px: 0, py: 724, pw: 1296, ph: 33 });
  caption(s, 0.55, y2 + h2 + 0.1, 12.2, "Status bar — active mode + hint · hover XYZ · job pill · gamepad/serial indicators · GPU MB · points drawn/total · FPS · version");
  infoCard(s, 0.55, y2 + h2 + 0.65, 5.95, 2.35, "Toolbar", [
    "One-click access to the four tools — the active tool is highlighted",
    "Color-mode combo and Quality combo (Low / Medium / High / Ultra)",
    "Shot = screenshot (F12); Open remembers your recent clouds",
  ], { fontSize: 12.5, gap: 7 });
  infoCard(s, 6.8, y2 + h2 + 0.65, 5.95, 2.35, "Status bar", [
    "Left: current mode and a context hint (e.g. “LMB orbit – RMB look – F frame”)",
    "Hovered point coordinates (XYZ) while over the cloud",
    "Right: GPU memory in use · drawn/total points · FPS · build version",
  ], { fontSize: 12.5, gap: 7 });
  s.addNotes("Both strips are cropped live from hero.png. Every toolbar action also exists in the menus and the Ctrl+P command palette.");
}

/* ================= 6. OPENING CLOUDS ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Opening a Cloud", "Getting started");
  shot(s, "welcome.png", 0.55, 1.75, 7.7);
  stepCard(s, 8.7, 1.75, 4.05, 4.8, "How to open", [
    "File ▸ Open Cloud… (Ctrl+O) — pick a .vxpc package (default filter) or an octree's meta.bin",
    "Or drag & drop a .vxpc / octree folder anywhere onto the window",
    "File ▸ Open Octree Folder… for legacy loose-folder octrees",
    "Welcome screen “Recent” re-opens past clouds — each entry shows its point count and size",
    "Drop a raw LAS/LAZ/E57/PLY/PTS/XYZ file → the Convert wizard opens automatically",
  ], { gap: 10, fontSize: 13 });
  caption(s, 0.55, 6.62, 7.7, "Welcome screen — quick actions, recent clouds (with point counts), drag-and-drop hint");
  s.addNotes("Open Recent (File menu) keeps up to 10 entries + Clear Recent. Preferences > General can auto-load the last cloud at startup.");
}

/* ================= 7. MENUS ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Menus at a Glance", "Reference");
  const menus = [
    ["menu_file_crop.png", "File", "Open, Convert, Save to package, Screenshot"],
    ["menu_edit_crop.png", "Edit", "Preferences (Ctrl+,)"],
    ["menu_view_crop.png", "View", "Presets, color mode, EDL, stereo, zen"],
    ["menu_tools_crop.png", "Tools", "Navigate · Measure · Annotate · Clip"],
    ["menu_window_crop.png", "Window", "Toggle panels + Reset Layout"],
    ["menu_help_crop.png", "Help", "Shortcuts (F1), Command Palette, About"],
  ];
  const cols = 3, cw = 4.02, ch = 2.05, gx = 0.2, x0 = 0.55, y0 = 1.75;
  const mAR = 585 / 296;
  menus.forEach((m, i) => {
    const cx = x0 + (i % cols) * (cw + gx), cy = y0 + Math.floor(i / cols) * (ch + 0.35 + 0.35);
    shot(s, m[0], cx, cy, cw, mAR);
    const capY = cy + cw / mAR + 0.12;
    s.addText([{ text: m[1] + "  ", options: { bold: true, color: ACC, fontSize: 13 } }, { text: m[2], options: { color: MUT, fontSize: 10.5 } }],
      { x: cx, y: capY, w: cw, h: 0.35, margin: 0, fontFace: "Arial", align: "left" });
  });
  s.addNotes("File > Save Project Data to Package embeds bookmarks/paths/measurements/annotations into the loaded .vxpc (single-cloud only). Tools are mutually exclusive modes.");
}

/* ================= 8. CONVERT: SOURCE ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Importing Scans — 1. Source", "Convert wizard");
  shot(s, "convert_source.png", 0.55, 1.75, 7.7);
  stepCard(s, 8.7, 1.75, 4.05, 4.8, "Pick your scan files", [
    "Open the wizard: File ▸ Convert a Scan… (Ctrl+I) — or just drop a raw scan on the window",
    "Browse for files… — multi-select is supported for batch conversion",
    "Accepts LAS, LAZ, E57, PLY, PTS and XYZ",
    "Check the per-file size list; remove any file with ✕",
    "Follow the breadcrumb: Source ▸ Quality ▸ Destination ▸ Convert",
  ], { gap: 11, fontSize: 13 });
  caption(s, 0.55, 6.62, 7.7, "Step 1 — one 308 MB LAS selected, ready for the next step");
  s.addNotes("The wizard is a fullscreen modal so a running conversion can't be disturbed. Batch mode enqueues one job per file.");
}

/* ================= 9. CONVERT: QUALITY ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Importing Scans — 2. Quality", "Convert wizard");
  shot(s, "convert_quality.png", 0.55, 1.75, 7.7);
  infoCard(s, 8.7, 1.75, 4.05, 4.8, "One-click presets", [
    { t: "Balanced (recommended) — great detail at a sensible speed & size", bold: true },
    "Draft — fastest, coarser detail; good for a quick first look",
    "High — best close-up detail; slower to build, larger on disk",
    "Custom — unlocks every parameter under Advanced settings",
    { t: "Higher quality sharpens close-up detail but takes longer and makes a bigger file.", c: MUT },
  ], { gap: 12 });
  caption(s, 0.55, 6.62, 7.7, "Step 2 — choose a quality preset (or open Advanced settings)");
  s.addNotes("Editing any Advanced value switches the preset to Custom. High on a >5 GB source shows a size-aware warning.");
}

/* ================= 10. CONVERT: ADVANCED ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Importing Scans — 3. Advanced", "Convert wizard");
  shot(s, "convert_advanced.png", 0.55, 1.75, 7.7);
  infoCard(s, 8.7, 1.75, 4.05, 4.8, "Fine-grained control", [
    "Sampling — Spacing (0 = auto), Leaf size (max pts/node), Max depth",
    "Resources — Chunk grid depth, Flush budget (Mpts), Indexer threads",
    "Output — Compress nodes (zstd) and Keep chunk files (debug)",
    "Every field has a (?) tooltip explaining what it does",
    { t: "The same knobs as the pfconvert CLI — presets map to these values.", c: MUT },
  ], { gap: 11 });
  caption(s, 0.55, 6.62, 7.7, "Advanced settings — sampling, resources and output options");
  s.addNotes("Indexer threads: Phase C runs on a parallel chunk worker pool (2.9x measured on a real scan, byte-identical output).");
}

/* ================= 11. CONVERT: DESTINATION & JOBS ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Importing Scans — 4. Destination & Jobs", "Convert wizard");
  const jobsShot = OPT("jobs_console.png");
  if (jobsShot) {
    shot(s, jobsShot, 0.55, 1.75, 7.7);
    caption(s, 0.55, 6.62, 7.7, "Jobs & Console panels — background conversions with live progress");
    stepCard(s, 8.7, 1.75, 4.05, 4.8, "Finish & monitor", [
      "Destination: single file → pick the .vxpc path (+ “Open when finished”); batch → pick a folder",
      "Review the summary, then Start — conversion runs as a background job",
      "Watch progress + ETA in the wizard, the Jobs panel, or the status-bar pill",
      "Cancel works in every phase (scanning, chunking, indexing)",
      "Done → Open in Viewer · Convert Another · Close",
    ], { gap: 9, fontSize: 12.5 });
  } else {
    stepCard(s, 0.55, 1.75, 5.95, 4.8, "Finish the wizard", [
      "Destination: single file → pick the .vxpc path (+ “Open when finished”); batch → pick an output folder (one .vxpc per scan)",
      "Review the summary — source, quality, destination",
      "Start — conversion runs on background threads; the fullscreen wizard shows overall + per-file progress and a live ETA",
      "Cancel stops safely in every phase (scanning, chunking, indexing)",
      "Done → Open in Viewer · Convert Another · Close",
    ], { gap: 10, fontSize: 13 });
    infoCard(s, 6.8, 1.75, 5.95, 4.8, "Background jobs, console & toasts", [
      "Jobs panel (Window ▸ Jobs) — every conversion/export job with state tag, progress bar + message, and Cancel / Open / Reveal / Console buttons",
      "Status-bar pill shows the busiest job at all times; a toast pops on completion with Open / Reveal",
      "Console panel mirrors the full log — Debug/Info/Warn/Error filters, auto-scroll, Clear, Copy",
      "Unseen warnings/errors badge the menu: “Console (N!)”",
      { t: "Optional screenshot: drop img/jobs_console.png to show the panels here (see CAPTURE_LIST.md).", c: MUT },
    ], { gap: 9, fontSize: 12.5 });
  }
  s.addNotes("Jobs never block the viewport — you can keep navigating while converting. Orphaned temp chunk dirs from a crashed run are purged automatically on the next conversion.");
}

/* ================= 12. NAVIGATION & CAMERA ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Navigation & Camera", "Viewing");
  stepCard(s, 0.55, 1.75, 5.95, 4.8, "Moving around", [
    "Orbit — hold LMB and drag; double-click a point to re-centre the pivot on it",
    "Free-look — hold RMB and drag (FPS-style)",
    "Zoom — mouse wheel, always toward the cursor",
    "Fly — WASD + Q/E while holding RMB or in free space; Shift = 5× speed",
    "Frame — F fits everything in view; View ▸ Reset View restarts the camera",
  ], { gap: 10, fontSize: 13 });
  infoCard(s, 6.8, 1.75, 5.95, 2.9, "View presets & projection", [
    "1 Front · 3 Side · 7 Top — instant axis-aligned views",
    "5 toggles Orthographic (with an Ortho Size control) — ideal for plans & sections",
    "Fly Speed slider in Properties ▸ Camera",
  ], { gap: 8, fontSize: 13 });
  infoCard(s, 6.8, 4.85, 5.95, 1.7, "Camera bookmarks", [
    "Properties ▸ Camera ▸ Bookmarks — save named poses per cloud, jump back with Go",
    { t: "Persisted between sessions and embeddable into the .vxpc.", c: MUT },
  ], { gap: 7, fontSize: 13 });
  footNote(s, "Tip", "Movement keys are ignored while a text field has focus — type freely.", 6.85);
  s.addNotes("Orbit pivot is projected dynamically onto the forward axis after free-look/fly, so orbiting never snaps back to a stale pivot.");
}

/* ================= 13. COLOR MODES I ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Color Modes — True Color & Elevation", "Rendering");
  const iw = 6.0, iy = 1.9;
  shot(s, "hero.png", 0.55, iy, iw);
  shot(s, "color_elev.png", 6.95, iy, iw);
  caption(s, 0.55, iy + iw / AR + 0.14, iw, "True Color — the scan's captured RGB");
  caption(s, 6.95, iy + iw / AR + 0.14, iw, "Elevation — Turbo colormap by height, with a Z(m) legend");
  footNote(s, "How", "Toolbar color dropdown, Properties ▸ Display ▸ Color Mode, or View ▸ Color Mode.");
  s.addNotes("Five modes total: True Color, Elevation, Solid Color, Intensity, Classification.");
}

/* ================= 14. COLOR MODES II ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Color Modes — Intensity & Classification", "Rendering");
  const iw = 6.0, iy = 1.9;
  shot(s, "color_int.png", 0.55, iy, iw);
  shot(s, "color_class.png", 6.95, iy, iw);
  caption(s, 0.55, iy + iw / AR + 0.14, iw, "Intensity — LiDAR return strength, Turbo legend (auto-scaled)");
  caption(s, 6.95, iy + iw / AR + 0.14, iw, "Classification — per-point class (e.g. ground vs. vegetation)");
  footNote(s, "Also", "Solid Color paints every point one picked colour — Properties ▸ Display has the picker.");
  s.addNotes("Elevation & Intensity draw a Turbo colour-bar legend in the viewport.");
}

/* ================= 15. DISPLAY & EDL ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Display Tuning & Eye-Dome Lighting", "Rendering");
  const off = OPT("edl_off.png"), on = OPT("edl_on.png");
  if (off && on) {
    const iw = 6.0, iy = 1.9;
    shot(s, off, 0.55, iy, iw);
    shot(s, on, 6.95, iy, iw);
    caption(s, 0.55, iy + iw / AR + 0.14, iw, "EDL off — flat, hard to read depth");
    caption(s, 6.95, iy + iw / AR + 0.14, iw, "EDL on — depth edges shaded, shape pops");
    footNote(s, "How", "Properties ▸ Display ▸ Eye-Dome Lighting (Strength 0.1–5, Radius 0.5–4) or View ▸ Eye-Dome Lighting.");
  } else {
    shot(s, "panel_display.png", 0.7, 1.9, 3.6, 260 / 255);
    caption(s, 0.7, 1.9 + 3.6 * (255 / 260) + 0.12, 3.6, "Properties ▸ Display");
    infoCard(s, 4.9, 1.75, 7.85, 4.85, "Tune how points draw", [
      "Quality — Low / Medium / High / Ultra maps to LOD budget + GPU memory (Low 4 px / 512 MB … Ultra 0.5 px / 4096 MB)",
      "Point size — 1–16 px slider, or Ctrl + mouse-wheel in the viewport",
      "Eye-Dome Lighting (EDL) — screen-space shading that reveals depth & shape on uncoloured clouds; Strength + Radius sliders",
      "Advanced — LOD budget (px SSE), GPU budget (128–8192 MB), Uploads/frame, Round points, Attenuate, Background colour",
      { t: "Cloud Info above shows points, nodes and cube size. Optional: drop img/edl_off.png + img/edl_on.png for a before/after comparison here.", c: MUT },
    ], { gap: 10, fontSize: 13 });
  }
  s.addNotes("EDL is an offscreen-FBO post-process. It makes intensity-only or colourless scans readable.");
}

/* ================= 16. SCENE PANEL / MULTI-CLOUD ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Scene Panel — Multiple Clouds", "Viewing");
  const sm = OPT("scene_multi.png");
  if (sm) {
    shot(s, sm, 0.55, 1.75, 7.7);
    caption(s, 0.55, 6.62, 7.7, "Scene panel — several clouds loaded side by side");
  } else {
    // crop the Scene panel out of the hero shot
    const h = cropShot(s, "hero.png", 0.85, 1.8, 1.75, { px: 0, py: 78, pw: 242, ph: 654 });
    caption(s, 0.55, 1.8 + h + 0.12, 2.35, "Scene panel (from the main window)");
  }
  stepCard(s, sm ? 8.7 : 3.5, 1.75, sm ? 4.05 : 5.6, 4.8, "Work with several clouds at once", [
    "Click + in the Scene panel (left dock) to add another cloud to the scene",
    "Tick / untick the checkbox to show or hide each cloud",
    "The selected cloud is the active one — tools, Properties and bookmarks follow it",
    "Close removes a cloud from the scene",
    "Frame All (F) and rendering include every visible cloud",
  ], { gap: 10, fontSize: 13 });
  if (!sm) infoCard(s, 9.35, 1.75, 3.4, 4.8, "Under the hood", [
    "Each cloud streams from its own octree with the shared GPU budget",
    "Multi-cloud scenes can be saved as one .vxpc (scene.json inside)",
    "Web remote lists clouds and can toggle visibility (cloud_vis)",
    { t: "Optional: drop img/scene_multi.png to show a real two-cloud scene (see CAPTURE_LIST.md).", c: MUT },
  ], { gap: 8, fontSize: 12 });
  s.addNotes("Multi-cloud shipped with newdev.md #6 (SceneCloud vector + Scene panel + remote clouds/cloud_vis).");
}

/* ================= 17. MEASURE ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Measuring Distances", "Tools");
  shot(s, "measure.png", 0.55, 1.75, 7.7);
  stepCard(s, 8.7, 1.75, 4.05, 4.8, "How to measure", [
    "Activate: toolbar Measure, Tools ▸ Measure, or press M",
    "Left-click points on the cloud to build a polyline",
    "Read each segment label and the running Total (m) in Properties ▸ Measure",
    "Undo the last point, Clear all, or Copy (points + total) to the clipboard",
    "Done (or Esc) returns to Navigate mode",
  ], { gap: 11, fontSize: 13 });
  caption(s, 0.55, 6.62, 7.7, "Three points — 95.7 m + 38.2 m = 133.9 m total");
  s.addNotes("Measurements are real 3D geometry — they appear in screenshots and the web stream, persist per cloud, and can be embedded into the .vxpc. Phones can place measure points remotely (tap the video).");
}

/* ================= 18. ANNOTATIONS & BOOKMARKS ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Annotations & Bookmarks", "Tools");
  shot(s, "panel_anno.png", 0.7, 1.95, 3.6, 260 / 192);
  stepCard(s, 4.9, 1.75, 7.85, 4.85, "Pin points & save views", [
    "Activate the Annotate tool (toolbar / Tools ▸ Annotate / A) and click points to drop labelled pins",
    "Rename a pin in Properties ▸ Annotations; Go jumps the camera to it; ✕ removes it (coordinates shown per pin)",
    "Pins can also be placed from a phone via the web remote — tap the video stream",
    "Bookmarks: Properties ▸ Camera ▸ Bookmarks saves named camera poses per cloud",
    "Everything persists between sessions and embeds into the .vxpc with File ▸ Save Project Data to Package",
  ], { gap: 10, fontSize: 13 });
  caption(s, 0.7, 1.95 + 3.6 * (192 / 260) + 0.12, 3.6, "Annotations panel");
  s.addNotes("The hero screenshot on slide 4 shows three pins (Pin 1-3) placed in the viewport. Pins render in the GL stream so remote viewers see them too.");
}

/* ================= 19. CLIP & SLICE EXPORT ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Clipping & Slice Export", "Tools");
  shot(s, "slice.png", 0.55, 1.75, 7.7);
  stepCard(s, 8.7, 1.75, 4.05, 4.8, "Cut & export a slice", [
    "Activate Clip (toolbar / Tools ▸ Clip / C) and tick Enable Clipping",
    "Drag the Min / Max sliders per axis to shape the clip box; Reset Planes clears it",
    "Click Export slice… and pick a format: DXF (CAD), PNG (image) or CSV (points)",
    "Set density (DXF/CSV) or image size (PNG) — an estimated point count warns before huge exports",
    "The export runs as a background job — watch it in the Jobs panel",
  ], { gap: 10, fontSize: 12.5 });
  caption(s, 0.55, 6.62, 7.7, "Slice Export — DXF / PNG / CSV with a live point-count estimate");
  s.addNotes("CSV columns: x,y,z,r,g,b,intensity,classification. DXF is R12 — opens in any CAD package.");
}

/* ================= 20. CAMERA PATHS ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Camera Paths", "Animation");
  shot(s, "campath.png", 0.55, 1.75, 7.7);
  stepCard(s, 8.7, 1.75, 4.05, 4.8, "Author a fly-through", [
    "Open Properties ▸ Camera Path",
    "Frame your first view, then click Add Key at Current View — repeat for each keyframe",
    "Adjust each key's time; Go previews it, Set re-records it, ✕ deletes it",
    "Set the total Duration and Scrub the timeline to check the motion",
    "Preview plays the interpolated path live in the viewport",
  ], { gap: 11, fontSize: 13 });
  caption(s, 0.55, 6.62, 7.7, "Three keyframes over 4 s — ready to preview or export");
  s.addNotes("Keyframes store position, yaw/pitch, ortho + ortho size at a time t; playback interpolates. Paths persist per cloud and embed into the .vxpc.");
}

/* ================= 21. VIDEO EXPORT + DEMO ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Video Export" + (DEMO_VIDEO ? " — Demo" : ""), "Animation");
  if (DEMO_VIDEO) {
    const vw = 8.0, vh = vw * 9 / 16, vx = 0.55, vy = 1.8;
    const p = 0.07;
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: vx - p, y: vy - p, w: vw + 2 * p, h: vh + 2 * p,
      fill: { color: CARD }, line: { color: CARD2, width: 1 }, rectRadius: 0.06, shadow: shadow() });
    s.addMedia({ type: "video", path: DEMO_VIDEO, x: vx, y: vy, w: vw, h: vh });
    caption(s, vx, vy + vh + 0.12, vw, "Fly-through exported straight from the viewer (" + path.basename(DEMO_VIDEO) + ") — click to play");
    infoCard(s, 8.95, 1.8, 3.8, 4.6, "Export MP4…", [
      "Properties ▸ Camera Path ▸ Export MP4…",
      "Resolution up to 4K · 24 / 30 / 60 fps",
      "H.264 bitrate 5–100 Mbps",
      "Hardware-accelerated (NVENC / QuickSync) via Media Foundation",
      { t: "Runs as a background job — keep working while it renders.", c: MUT },
    ], { gap: 9, fontSize: 12.5 });
  } else {
    shot(s, "campath.png", 0.55, 1.75, 7.7);
    caption(s, 0.55, 6.62, 7.7, "Export MP4… lives at the bottom of the Camera Path section");
    stepCard(s, 8.7, 1.75, 4.05, 3.4, "Render the path to MP4", [
      "Author a camera path (previous slide)",
      "Click Export MP4… in Properties ▸ Camera Path",
      "Pick resolution (up to 4K), frame rate (24/30/60) and H.264 bitrate (5–100 Mbps)",
      "Export runs as a background job — hardware-encoded (NVENC / QuickSync)",
    ], { gap: 8, fontSize: 12.5 });
    infoCard(s, 8.7, 5.35, 4.05, 1.2, null, [
      { t: "To embed a live demo video on this slide: export an MP4 from the viewer, save it as docs/ppt/video/demo.mp4, and rerun node build_full.js.", c: MUT, b: false },
    ], { fontSize: 11.5, gap: 4 });
  }
  s.addNotes("VideoExporter.h uses IMFSinkWriter (H.264/NV12 MP4). Windows-only; stubbed elsewhere.");
}

/* ================= 22. SCREENSHOTS & PRESENTATION MODES ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Screenshots, Zen & Fullscreen", "Present");
  shot(s, "stats.png", 0.55, 1.9, 6.0);
  caption(s, 0.55, 1.9 + 6.0 / AR + 0.14, 6.0, "Stats overlay (F3) — live HUD over the viewport");
  infoCard(s, 6.95, 1.75, 5.8, 2.5, "Screenshot (F12 / toolbar Shot)", [
    "Saves a PNG to Pictures\\ViitorXPC\\shot_<timestamp>.png",
    "Also copied to the Windows clipboard — paste straight into chat/docs",
    "A toast confirms with Open / Reveal; remote clients can fetch it at /shot.png (PIN-gated)",
  ], { gap: 8, fontSize: 12.5 });
  infoCard(s, 6.95, 4.45, 5.8, 2.1, "Distraction-free viewing", [
    "F5 (or Shift+Space) — zen mode hides all UI; press again to restore",
    "F11 — fullscreen window",
    "F3 — stats overlay HUD (FPS, points, memory) over the viewport",
  ], { gap: 8, fontSize: 12.5 });
  s.addNotes("Measurements and pins are real geometry, so they appear in screenshots. The watermark is NOT burned into saved screenshots.");
}

/* ================= 23. STEREOSCOPIC ================= */
{
  const s = pres.addSlide(); bg(s, BG2);
  header(s, "Stereoscopic 3D (SBS)", "Present");
  const iw = 9.4, ix = (W - iw) / 2;
  shot(s, "stereo.png", ix, 1.85, iw);
  s.addText([{ text: "F9 ", options: { bold: true, color: ACC, fontSize: 14 } },
    { text: "toggles side-by-side stereo — all UI hides so each eye sees only the cloud. Press F9 or Esc to exit. Set eye separation (IPD) & focal distance in Preferences ▸ Display.", options: { color: TXT, fontSize: 13 } }],
    { x: 1.4, y: 6.75, w: 10.5, h: 0.5, align: "center", margin: 0, fontFace: "Arial" });
  s.addNotes("Designed for SBS-capable displays / 3D TVs / viewers. The brand watermark pops out with negative parallax per eye. Esc exits stereo before it quits the app.");
}

/* ================= 24. PERFORMANCE & MONITORING ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Performance & Monitoring", "Diagnostics");
  shot(s, "perf.png", 0.55, 1.9, 6.0);
  caption(s, 0.55, 1.9 + 6.0 / AR + 0.14, 6.0, "Performance panel — FPS plot, node/point counts, GPU budget");
  infoCard(s, 6.95, 1.75, 5.8, 4.6, "Watch the streamer work", [
    "Window ▸ Performance opens the bottom-dock panel",
    "FPS plot · Visible vs Drawn nodes · Drawn points · Points resident on GPU",
    "Load queue depth shows streaming pressure; the GPU budget bar shows memory head-room",
    "Quality presets trade detail for speed: Low 4 px / 512 MB → Ultra 0.5 px / 4096 MB",
    "Console (Window ▸ Console) mirrors the log with severity filters — unseen problems badge the Window menu",
  ], { gap: 10, fontSize: 13 });
  s.addNotes("LOD budget is screen-space error in pixels; GPU budget uses LRU eviction. Uploads/frame throttles how much data hits the GPU per frame.");
}

/* ================= 25. COMMAND PALETTE ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Command Palette & Help", "Productivity");
  shot(s, "palette.png", 0.55, 1.75, 7.7);
  const overlay = OPT("shortcuts_overlay.png");
  stepCard(s, 8.7, 1.75, 4.05, overlay ? 2.9 : 4.8, "Find any command fast", [
    "Press Ctrl+P to open the Command Palette",
    "Type a few letters — the list fuzzy-filters live",
    "Enter runs the first match; click runs any row",
    "F1 opens the searchable keyboard-shortcut sheet",
  ].concat(overlay ? [] : ["Every menu/toolbar action is in the palette — open, convert, presets, tools, EDL, stereo, panels, prefs…"]), { gap: 10, fontSize: 13 });
  if (overlay) {
    shot(s, overlay, 8.7, 4.85, 4.05);
    caption(s, 8.7, 4.85 + 4.05 / AR + 0.1, 4.05, "F1 — shortcut sheet");
  }
  caption(s, 0.55, 6.62, 7.7, "Command Palette — type to filter, Enter to run");
  s.addNotes("The shortcut catalog is a single source-of-truth table (kKeyBinds) that feeds both the F1 overlay and this guide's shortcut slide.");
}

/* ================= 26. PREFERENCES ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Preferences", "Settings");
  const iw = 6.0, iy = 1.9;
  shot(s, "prefs_general.png", 0.55, iy, iw);
  shot(s, "prefs_input.png", 6.95, iy, iw);
  caption(s, 0.55, iy + iw / AR + 0.14, iw, "General — theme, UI scale, auto-load, .vxpc file association");
  caption(s, 6.95, iy + iw / AR + 0.14, iw, "Input — gamepad, serial controller, web remote");
  footNote(s, "Open", "Edit ▸ Preferences… (Ctrl+,). Tabs: General · Display (stereo IPD/focal) · Input · Advanced (reset all). Settings persist across sessions.");
  s.addNotes("General: light theme, UI scale 0.5-3x, auto-load last cloud, per-user .vxpc file association, clear recents. Settings live in pfview_config.txt under AppData.");
}

/* ================= 27. WEB REMOTE & CONTROLLERS ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Web Remote & Controllers", "Connectivity");
  shot(s, "prefs_web.png", 0.55, 1.75, 7.7);
  stepCard(s, 8.7, 1.75, 4.05, 4.8, "Drive it from a phone", [
    "Preferences ▸ Input ▸ Web remote — tick Enable (default port 8899)",
    "Click Show connect QR and scan it with the phone",
    "Enter the Driver PIN (full control) or Viewer PIN (watch-only)",
    "Use touch joysticks / gestures — or mouse + WASD from a desktop browser",
    "Live viewport streams as JPEG (or WebRTC H.264, hardware-encoded); tap the video to measure or drop pins remotely",
  ], { gap: 9, fontSize: 12.5 });
  caption(s, 0.55, 6.62, 7.7, "Preferences ▸ Input — web remote (serving URL & PINs redacted)");
  s.addNotes("Also here: Xbox gamepad (deadzone/sensitivity/invert, UI-nav mode, live rebind) and the custom ESP32 Bluetooth-SPP serial controller (MAC auto-detect, COM port). PINs regenerate on server start.");
}

/* ================= 28. VXPC PACKAGE ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "The .vxpc Package", "File format");
  // simple structure diagram built from shapes
  const dx = 0.7, dy = 1.9, dw = 5.4;
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: dx, y: dy, w: dw, h: 4.4, fill: { color: CARD }, rectRadius: 0.1, shadow: shadow() });
  s.addText("scan.vxpc", { x: dx + 0.25, y: dy + 0.15, w: dw - 0.5, h: 0.35, margin: 0, fontFace: "Consolas", fontSize: 15, bold: true, color: ACC });
  const rows = [
    ["header + entry directory", "128-byte header; per-entry zstd compression"],
    ["octree  (meta / hierarchy / points)", "the streamable LOD cloud itself"],
    ["scene.json", "optional — several clouds in one package"],
    ["bookmarks · campaths", "saved views & fly-throughs"],
    ["measurements · annotations", "your markups, embedded"],
  ];
  let ry = dy + 0.62;
  rows.forEach((r) => {
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: dx + 0.25, y: ry, w: dw - 0.5, h: 0.62, fill: { color: CARD2 }, rectRadius: 0.06 });
    s.addText([{ text: r[0] + "\n", options: { fontFace: "Consolas", fontSize: 11.5, bold: true, color: TXT } },
      { text: r[1], options: { fontFace: "Arial", fontSize: 9.5, color: MUT } }],
      { x: dx + 0.45, y: ry + 0.04, w: dw - 0.9, h: 0.56, margin: 0, valign: "middle", lineSpacingMultiple: 1.0 });
    ry += 0.73;
  });
  infoCard(s, 6.55, 1.9, 6.2, 4.4, "One file, everything inside", [
    "Convert writes a single portable .vxpc — no loose folders to zip or lose",
    "Opens by double-click: the viewer registers a per-user .vxpc file association (toggle in Preferences ▸ General)",
    "Streams over the network too — http(s):// URLs load via Range requests, no full download",
    "File ▸ Save Project Data to Package embeds your bookmarks, camera paths, measurements and annotations (single-cloud packages)",
    "Legacy octree folders (meta.bin …) still open via File ▸ Open Octree Folder…",
  ], { gap: 10, fontSize: 13 });
  s.addNotes("Random-access ByteSource abstraction backs both local files and HTTP Range streaming. Multi-cloud scenes are stored via scene.json.");
}

/* ================= 29. SHORTCUTS TABLE ================= */
{
  const s = pres.addSlide(); bg(s);
  header(s, "Keyboard Shortcuts", "Reference");
  const th = (t) => ({ text: t, options: { fill: { color: CARD2 }, color: ACC, bold: true, fontSize: 12.5, align: "left" } });
  const rows = [
    ["Ctrl+O", "Open cloud", "F", "Frame all"],
    ["Ctrl+I", "Convert a scan", "1 / 3 / 7", "Front / side / top"],
    ["Ctrl+P", "Command palette", "5", "Orthographic toggle"],
    ["Ctrl+,", "Preferences", "M / A / C", "Measure / annotate / clip"],
    ["F1", "Shortcuts overlay", "Esc", "Exit tool / popup"],
    ["F3", "Stats overlay", "WASD Q E", "Fly (Shift = fast)"],
    ["F5 / Sh+Spc", "Hide UI (zen)", "LMB / RMB", "Orbit / free-look"],
    ["F9", "Stereoscopic SBS", "2× LMB", "Focus pivot"],
    ["F11", "Fullscreen", "Wheel", "Zoom (Ctrl = point size)"],
    ["F12", "Screenshot", "Esc Esc", "Quit"],
  ];
  const body = [[th("Key"), th("Action"), th("Key"), th("Action")]];
  rows.forEach((r, i) => {
    const f = i % 2 ? CARD : BG;
    body.push(r.map((c, j) => ({ text: c, options: {
      fill: { color: f }, color: (j % 2 === 0) ? ACC2 : TXT, bold: j % 2 === 0,
      fontFace: (j % 2 === 0) ? "Consolas" : "Arial", fontSize: 12.5, align: "left", valign: "middle" } })));
  });
  s.addTable(body, { x: 0.55, y: 1.8, w: 12.2, colW: [1.9, 4.2, 1.9, 4.2], rowH: 0.44,
    border: { type: "solid", color: BG2, pt: 1 }, valign: "middle", margin: [2, 6, 2, 6] });
  s.addText("Movement keys are ignored while typing in a field. F1 opens the searchable in-app version of this table. Gamepad & serial-pad mappings live in Preferences ▸ Input.",
    { x: 0.55, y: 6.85, w: 12.2, h: 0.35, margin: 0, fontFace: "Arial", fontSize: 11.5, italic: true, color: MUT });
}

/* ================= 30. CLOSING ================= */
{
  const s = pres.addSlide(); bg(s, BG2);
  s.addText("VIITORX POINTCLOUD VIEWER", { x: 0.7, y: 2.4, w: 12, h: 0.5, margin: 0, fontFace: "Arial", fontSize: 16, bold: true, color: ACC, charSpacing: 5 });
  s.addText("Convert once, explore anything.", { x: 0.66, y: 2.9, w: 12, h: 1.0, margin: 0, fontFace: "Cambria", fontSize: 40, bold: true, color: TXT });
  s.addText("Billion-point clouds, streamed interactively — measure, annotate, clip, slice-export, fly-through video, stereoscopic 3D and a phone web-remote. All in one .vxpc.",
    { x: 0.7, y: 4.0, w: 9.6, h: 0.9, margin: 0, fontFace: "Arial", fontSize: 15, color: MUT });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 0.7, y: 5.1, w: 1.5, h: 0.5, fill: { color: CARD2 }, rectRadius: 0.25 });
  s.addText("v1.0.56", { x: 0.7, y: 5.1, w: 1.5, h: 0.5, align: "center", valign: "middle", margin: 0, fontFace: "Arial", fontSize: 14, bold: true, color: ACC });
}

pres.writeFile({ fileName: path.join(__dirname, "ViitorXPC_Complete_Feature_Guide.pptx") })
  .then((f) => {
    console.log("WROTE", f);
    console.log(DEMO_VIDEO ? `Embedded video: ${DEMO_VIDEO}` : "No video found in docs/ppt/video/ — video slide built in fallback mode.");
    ["jobs_console.png", "scene_multi.png", "edl_off.png", "edl_on.png", "shortcuts_overlay.png"].forEach((n) => {
      if (!OPT(n)) console.log(`Optional screenshot missing (fallback used): img/${n}`);
    });
  })
  .catch((e) => { console.error("BUILD FAILED:", e); process.exit(1); });
