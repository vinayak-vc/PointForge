const pptxgen = require("pptxgenjs");
const path = require("path");
const IMG = (n) => path.join(__dirname, "img", n);

const AR = 1296 / 759;            // screenshot aspect ratio
const W = 13.3, H = 7.5;

// palette
const BG    = "0E1524";   // deep navy-black
const BG2   = "0B1120";   // darker (title/close)
const CARD  = "1A2740";   // card surface
const CARD2 = "223256";   // lighter card
const TXT   = "EAF0F8";   // primary text
const MUT   = "93A1B5";   // muted
const ACC   = "35D0C4";   // teal accent (dominant accent)
const ACC2  = "F2A93B";   // amber (secondary accent, sparing)
const RED   = "C0303A";   // brand red (very sparing)

const pres = new pptxgen();
pres.defineLayout({ name: "WIDE", width: W, height: H });
pres.layout = "WIDE";
pres.author = "ViitorX";
pres.title = "ViitorX PointCloud Viewer — Feature Guide";

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

// slide header with number chip + title
function header(slide, num, title, kicker) {
  slide.addShape(pres.shapes.OVAL, { x: 0.55, y: 0.42, w: 0.62, h: 0.62, fill: { color: ACC } });
  slide.addText(String(num), { x: 0.55, y: 0.42, w: 0.62, h: 0.62, align: "center", valign: "middle",
    fontFace: "Cambria", fontSize: 22, bold: true, color: BG });
  if (kicker) slide.addText(kicker.toUpperCase(), { x: 1.35, y: 0.42, w: 10, h: 0.3, margin: 0,
    fontFace: "Arial", fontSize: 11, bold: true, color: ACC, charSpacing: 3 });
  slide.addText(title, { x: 1.33, y: kicker ? 0.66 : 0.5, w: 11.4, h: 0.62, margin: 0,
    fontFace: "Cambria", fontSize: 30, bold: true, color: TXT });
}

// info card with heading + bullet items (items: strings or {t,b?})
function infoCard(slide, x, y, w, h, heading, items, opt = {}) {
  slide.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y, w, h,
    fill: { color: opt.fill || CARD }, line: { type: "none" }, rectRadius: 0.09, shadow: shadow() });
  let cy = y + 0.22;
  if (heading) {
    slide.addText(heading, { x: x + 0.28, y: cy, w: w - 0.5, h: 0.4, margin: 0,
      fontFace: "Arial", fontSize: 15, bold: true, color: opt.headColor || ACC });
    cy += 0.5;
  }
  const runs = items.map((it, i) => {
    const isObj = typeof it === "object";
    const t = isObj ? it.t : it;
    return { text: t, options: {
      bullet: (isObj && it.b === false) ? false : { code: "2022", indent: 14 },
      breakLine: true, fontFace: "Arial", fontSize: opt.fontSize || 13.5,
      color: (isObj && it.c) ? it.c : TXT, paraSpaceAfter: opt.gap != null ? opt.gap : 7, bold: !!(isObj && it.bold) } };
  });
  slide.addText(runs, { x: x + 0.28, y: cy, w: w - 0.5, h: h - (cy - y) - 0.2, valign: "top", margin: 0 });
}

function caption(slide, x, y, w, text) {
  slide.addText(text, { x, y, w, h: 0.3, align: "center", margin: 0,
    fontFace: "Arial", fontSize: 11, italic: true, color: MUT });
}

/* ---------------- SLIDE 1 — TITLE ---------------- */
{
  const s = pres.addSlide(); bg(s, BG2);
  // big framed hero on right
  shot(s, "hero.png", 6.7, 1.7, 6.2);
  // left text block
  s.addText("VIITORX", { x: 0.7, y: 1.55, w: 6, h: 0.5, margin: 0, fontFace: "Arial", fontSize: 16, bold: true, color: ACC, charSpacing: 6 });
  s.addText("PointCloud Viewer", { x: 0.66, y: 1.95, w: 6.1, h: 1.6, margin: 0, fontFace: "Cambria", fontSize: 50, bold: true, color: TXT, lineSpacingMultiple: 0.95 });
  s.addText("Complete Feature Guide", { x: 0.7, y: 3.55, w: 6, h: 0.5, margin: 0, fontFace: "Arial", fontSize: 20, color: MUT });
  // version chip
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 0.7, y: 4.25, w: 1.5, h: 0.5, fill: { color: CARD2 }, rectRadius: 0.25 });
  s.addText("v1.0.56", { x: 0.7, y: 4.25, w: 1.5, h: 0.5, align: "center", valign: "middle", margin: 0, fontFace: "Arial", fontSize: 14, bold: true, color: ACC });
  s.addText("Out-of-core viewer for billion-point clouds", { x: 0.7, y: 5.05, w: 5.7, h: 0.4, margin: 0, fontFace: "Arial", fontSize: 13.5, color: TXT });
  s.addText("LAS · LAZ · E57 · PLY · PTS · XYZ", { x: 0.7, y: 5.42, w: 5.7, h: 0.4, margin: 0, fontFace: "Arial", fontSize: 12.5, bold: true, color: MUT, charSpacing: 1 });
  caption(s, 6.7, 6.55, 6.2, "Tikal-13 — 12.4M points, streamed live in ViitorX");
}

/* ---------------- SLIDE 2 — OVERVIEW ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 1, "What is ViitorX?", "Overview");
  infoCard(s, 0.55, 1.7, 5.5, 4.9, "Built to open clouds bigger than RAM", [
    { t: "Ingests very large scans (LAS/LAZ, E57, PLY, PTS/XYZ) — billions of points, larger than memory.", },
    { t: "Converts each scan into a streamable on-disk octree, then streams nodes on demand.", },
    { t: "Frustum culling + screen-space-error LOD keep only what you can see on the GPU.", },
    { t: "Async loading with an LRU GPU-memory budget — smooth navigation, bounded memory.", },
    { t: "Portable .vxpc package holds the whole cloud plus your bookmarks, measurements & annotations.", },
  ], { fontSize: 13.5, gap: 11 });
  // 2x2 stat tiles
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
}

/* ---------------- SLIDE 3 — WORKSPACE TOUR ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 2, "The Workspace", "Docked shell");
  const ix = 0.55, iy = 1.75, iw = 8.35; const ih = shot(s, "hero.png", ix, iy, iw);
  // numbered markers on image at region centers (fractions of image)
  const marks = [
    [0.03, 0.03, "1"], // menu bar
    [0.03, 0.10, "2"], // toolbar
    [0.09, 0.40, "3"], // scene
    [0.45, 0.55, "4"], // viewport
    [0.90, 0.33, "5"], // properties
    [0.42, 0.975, "6"], // status bar
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
}

/* ---------------- SLIDE 4 — OPENING CLOUDS ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 3, "Opening a Cloud", "Getting started");
  shot(s, "welcome.png", 0.55, 1.75, 7.7);
  infoCard(s, 8.7, 1.75, 4.05, 4.8, "How to open", [
    { t: "File ▸ Open Cloud…  (Ctrl+O) — pick a .vxpc package or an octree's meta.bin", },
    { t: "Drag & drop a .vxpc or octree folder onto the window", },
    { t: "File ▸ Open Octree Folder… for legacy loose-folder octrees", },
    { t: "Recent list on the Welcome screen re-opens past clouds in one click", },
    { t: "Drop a raw LAS/LAZ/E57/PLY/PTS/XYZ file → the Convert wizard opens automatically", },
  ], { gap: 12 });
  caption(s, 0.55, 6.62, 7.7, "Welcome screen — quick actions, recent clouds, drag-and-drop hint");
}

/* ---------------- SLIDE 5 — MENUS ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 4, "Menus at a Glance", "Reference");
  const menus = [
    ["menu_file_crop.png", "File", "Open, Convert, Save to package, Screenshot"],
    ["menu_edit_crop.png", "Edit", "Preferences (Ctrl+,)"],
    ["menu_view_crop.png", "View", "Presets, color mode, EDL, stereo, zen"],
    ["menu_tools_crop.png", "Tools", "Navigate · Measure · Annotate · Clip"],
    ["menu_window_crop.png", "Window", "Toggle panels + Reset Layout"],
    ["menu_help_crop.png", "Help", "Shortcuts (F1), Command Palette, About"],
  ];
  const cols = 3, cw = 4.02, ch = 2.05, gx = 0.2, gy = 0.35, x0 = 0.55, y0 = 1.75;
  const mAR = 585 / 296;
  menus.forEach((m, i) => {
    const cx = x0 + (i % cols) * (cw + gx), cy = y0 + Math.floor(i / cols) * (ch + gy + 0.35);
    shot(s, m[0], cx, cy, cw, mAR);
    const capY = cy + cw / mAR + 0.12;
    s.addText([{ text: m[1] + "  ", options: { bold: true, color: ACC, fontSize: 13 } }, { text: m[2], options: { color: MUT, fontSize: 10.5 } }],
      { x: cx, y: capY, w: cw, h: 0.35, margin: 0, fontFace: "Arial", align: "left" });
  });
}

/* ---------------- SLIDE 6 — CONVERT: SOURCE ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 5, "Importing Scans — 1. Source", "Convert wizard");
  shot(s, "convert_source.png", 0.55, 1.75, 7.7);
  infoCard(s, 8.7, 1.75, 4.05, 4.8, "Pick your scan files", [
    { t: "Open the wizard: File ▸ Convert a Scan…  (Ctrl+I)", },
    { t: "Browse for files… — select one or many, or drag & drop onto the window", },
    { t: "Accepts LAS, LAZ, E57, PLY, PTS and XYZ", },
    { t: "Each file's size is listed; remove any with the ✕ button", },
    { t: "A 4-step breadcrumb tracks progress: Source ▸ Quality ▸ Destination ▸ Convert", },
  ], { gap: 12 });
  caption(s, 0.55, 6.62, 7.7, "Step 1 — one 308 MB LAS selected, ready for the next step");
}

/* ---------------- SLIDE 7 — CONVERT: QUALITY ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 6, "Importing Scans — 2. Quality", "Convert wizard");
  shot(s, "convert_quality.png", 0.55, 1.75, 7.7);
  infoCard(s, 8.7, 1.75, 4.05, 4.8, "One-click presets", [
    { t: "Balanced (recommended) — great detail at a sensible speed & size", bold: true },
    { t: "Draft — fastest, coarser detail; good for a quick first look", },
    { t: "High — best close-up detail; slower to build, larger on disk", },
    { t: "Custom — unlocks every parameter under Advanced settings", },
    { t: "Higher quality sharpens close-up detail but takes longer and makes a bigger file.", c: MUT },
  ], { gap: 12 });
  caption(s, 0.55, 6.62, 7.7, "Step 2 — choose a quality preset (or open Advanced settings)");
}

/* ---------------- SLIDE 8 — CONVERT: ADVANCED ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 7, "Importing Scans — 3. Advanced", "Convert wizard");
  shot(s, "convert_advanced.png", 0.55, 1.75, 7.7);
  infoCard(s, 8.7, 1.75, 4.05, 4.8, "Fine-grained control", [
    { t: "Sampling — Spacing (0 = auto), Leaf size (max pts/node), Max depth", },
    { t: "Resources — Chunk grid depth, Flush budget (Mpts), Indexer threads", },
    { t: "Output — Compress nodes (zstd) and Keep chunk files (debug)", },
    { t: "Editing any value switches the preset to Custom", },
    { t: "Destination step then writes a .vxpc (with optional auto-open) — or a folder for batches", c: MUT },
  ], { gap: 11 });
  caption(s, 0.55, 6.62, 7.7, "Advanced settings — the same knobs as the pfconvert CLI");
}

/* ---------------- SLIDE 9 — CONVERT FROM PHOTOS (PHOTOGRAMMETRY) ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 8, "Importing Photos — Photogrammetry", "Convert wizard");
  const par = 1858 / 1096;                  // this capture is 1858x1096
  shot(s, "convert_photos.png", 0.55, 1.75, 7.7, par);
  infoCard(s, 8.7, 1.55, 4.05, 5.35, "Photos in, point cloud out", [
    { t: "Or start from photos — pick a folder of overlapping drone / camera JPGs, or drag it onto the window", },
    { t: "GPS tags found → ODM — georeferenced, metric-scale LAZ (runs via Docker)", },
    { t: "No GPS or no NVIDIA GPU → COLMAP — densest native reconstruction", },
    { t: "Auto picks the best engine and explains why; override any time", },
    { t: "One consent click installs both engines automatically — no manual setup", },
    { t: "Cancelable background job: reconstruct, then convert to .vxpc like any scan", },
    { t: "Docker blocked? The wizard shows this PC's exact BIOS steps to enable virtualization", c: MUT },
  ], { gap: 8, fontSize: 12.5 });
  caption(s, 0.55, 1.75 + 7.7 / par + 0.14, 7.7,
    "271 drone photos scanned — GPS found, engine status, and board-specific BIOS help");
}

/* ---------------- SLIDE 10 — COLOR MODES I ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 9, "Color Modes — True Color & Elevation", "Rendering");
  const iw = 6.0, iy = 1.9;
  shot(s, "hero.png", 0.55, iy, iw);
  shot(s, "color_elev.png", 6.95, iy, iw);
  caption(s, 0.55, iy + iw / AR + 0.14, iw, "True Color — the scan's captured RGB");
  caption(s, 6.95, iy + iw / AR + 0.14, iw, "Elevation — Turbo colormap by height, with a Z(m) legend");
  s.addText([{ text: "How:  ", options: { bold: true, color: ACC } },
    { text: "Toolbar color dropdown, the Properties ▸ Display ▸ Color Mode combo, or View ▸ Color Mode.", options: { color: TXT } }],
    { x: 0.55, y: 6.55, w: 12.2, h: 0.4, margin: 0, fontFace: "Arial", fontSize: 13 });
}

/* ---------------- SLIDE 11 — COLOR MODES II ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 10, "Color Modes — Intensity & Classification", "Rendering");
  const iw = 6.0, iy = 1.9;
  shot(s, "color_int.png", 0.55, iy, iw);
  shot(s, "color_class.png", 6.95, iy, iw);
  caption(s, 0.55, iy + iw / AR + 0.14, iw, "Intensity — LiDAR return strength, Turbo legend (auto-scaled)");
  caption(s, 6.95, iy + iw / AR + 0.14, iw, "Classification — per-point class (e.g. ground vs. vegetation)");
  s.addText([{ text: "Also:  ", options: { bold: true, color: ACC } },
    { text: "Solid Color mode paints every point one picked colour — handy against the Elevation/Intensity Turbo legends.", options: { color: TXT } }],
    { x: 0.55, y: 6.55, w: 12.2, h: 0.4, margin: 0, fontFace: "Arial", fontSize: 13 });
}

/* ---------------- SLIDE 12 — DISPLAY & EDL ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 11, "Display & Eye-Dome Lighting", "Rendering");
  shot(s, "panel_display.png", 0.7, 1.9, 3.6, 260 / 255);
  infoCard(s, 4.9, 1.75, 7.85, 4.85, "Tune how points draw", [
    { t: "Quality — Low / Medium / High / Ultra maps to the LOD budget + GPU memory budget", },
    { t: "Point size — 1–16 px slider (or Ctrl + mouse-wheel in the viewport)", },
    { t: "Eye-Dome Lighting (EDL) — screen-space shading that reveals depth & shape on uncoloured clouds; Strength and Radius sliders", },
    { t: "Advanced — LOD budget (screen-space error, px), GPU budget (MB), Uploads/frame, Round points, Attenuate, Background colour", },
    { t: "Cloud Info shows point count, node count and cube size for the active cloud", c: MUT },
  ], { gap: 11 });
  caption(s, 0.7, 1.9 + 3.6 * (255 / 260) + 0.12, 3.6, "Properties ▸ Display");
}

/* ---------------- SLIDE 13 — MEASURE ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 12, "Measuring Distances", "Tools");
  shot(s, "measure.png", 0.55, 1.75, 7.7);
  infoCard(s, 8.7, 1.75, 4.05, 4.8, "Measure tool", [
    { t: "Activate: toolbar Measure, Tools ▸ Measure, or press M", },
    { t: "Left-click points in the viewport to build a polyline", },
    { t: "Each segment is labelled; a running Total (m) is shown", },
    { t: "Undo / Clear / Copy (points + total to clipboard) / Done", },
    { t: "Drawn as real 3D geometry — visible in screenshots & the web stream", c: MUT },
  ], { gap: 12 });
  caption(s, 0.55, 6.62, 7.7, "Three points — 95.7 m + 38.2 m = 133.9 m total");
}

/* ---------------- SLIDE 14 — ANNOTATIONS ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 13, "Annotations & Bookmarks", "Tools");
  shot(s, "panel_anno.png", 0.7, 1.95, 3.6, 260 / 192);
  infoCard(s, 4.9, 1.75, 7.85, 4.85, "Pin points & save views", [
    { t: "Annotate tool (toolbar / Tools ▸ Annotate / A) — click points to drop labelled pins", },
    { t: "Rename each pin, jump to it with Go, or remove it; coordinates are shown", },
    { t: "Bookmarks (Properties ▸ Camera) save named camera poses per cloud", },
    { t: "Pins, bookmarks, measurements & camera paths persist — stored beside the octree and embeddable into the .vxpc via File ▸ Save Project Data to Package", },
  ], { gap: 12 });
  caption(s, 0.7, 1.95 + 3.6 * (192 / 260) + 0.12, 3.6, "Annotations panel");
}

/* ---------------- SLIDE 15 — CLIP & SLICE ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 14, "Clipping & Slice Export", "Tools");
  shot(s, "slice.png", 0.55, 1.75, 7.7);
  infoCard(s, 8.7, 1.75, 4.05, 4.8, "Cut & export a slice", [
    { t: "Clip tool (toolbar / Tools ▸ Clip / C) — enable an axis-aligned clip box", },
    { t: "Drag the Min / Max sliders per axis; Reset Planes to clear", },
    { t: "Export slice… writes the clipped region to disk", },
    { t: "Formats: DXF (CAD lines), PNG (rendered image), CSV (x,y,z,rgb,intensity,class)", },
    { t: "An estimated point count warns before very large exports", c: MUT },
  ], { gap: 12 });
  caption(s, 0.55, 6.62, 7.7, "Slice Export — DXF / PNG / CSV with a live point-count estimate");
}

/* ---------------- SLIDE 16 — CAMERA PATH & VIDEO ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 15, "Camera Paths & Video Export", "Animation");
  shot(s, "campath.png", 0.55, 1.75, 7.7);
  infoCard(s, 8.7, 1.75, 4.05, 4.8, "Fly-through animation", [
    { t: "Properties ▸ Camera Path — Add Key at Current View to set keyframes", },
    { t: "Each key has a time; Go / Set / delete per key; Scrub to preview", },
    { t: "Preview plays the interpolated path in the viewport", },
    { t: "Export MP4… — resolution (up to 4K), frame rate, H.264 bitrate", },
    { t: "Hardware-accelerated (NVENC / QuickSync) via Windows Media Foundation", c: MUT },
  ], { gap: 12 });
  caption(s, 0.55, 6.62, 7.7, "Three keyframes over 4 s — ready to preview or export");
}

/* ---------------- SLIDE 17 — STEREOSCOPIC ---------------- */
{
  const s = pres.addSlide(); bg(s, BG2);
  header(s, 16, "Stereoscopic 3D (SBS)", "Immersive");
  const iw = 9.4, ix = (W - iw) / 2;
  shot(s, "stereo.png", ix, 1.85, iw);
  s.addText([{ text: "F9 ", options: { bold: true, color: ACC, fontSize: 14 } },
    { text: "toggles side-by-side stereo — all UI hides so each eye sees only the cloud. Press F9 or Esc to exit. Set eye separation (IPD) & focal distance in Preferences ▸ Display.", options: { color: TXT, fontSize: 13 } }],
    { x: 1.4, y: 6.75, w: 10.5, h: 0.5, align: "center", margin: 0, fontFace: "Arial" });
}

/* ---------------- SLIDE 18 — PERFORMANCE & STATS ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 17, "Performance & Monitoring", "Diagnostics");
  const iw = 6.0, iy = 1.9;
  shot(s, "perf.png", 0.55, iy, iw);
  shot(s, "stats.png", 6.95, iy, iw);
  caption(s, 0.55, iy + iw / AR + 0.14, iw, "Performance panel — FPS plot, node/point counts, GPU budget");
  caption(s, 6.95, iy + iw / AR + 0.14, iw, "Stats overlay (F3) — a live HUD over the viewport");
  s.addText([{ text: "Panels:  ", options: { bold: true, color: ACC } },
    { text: "Window menu toggles Scene, Properties, Jobs, Console and Performance. Conversions run as background Jobs with progress, ETA and a Console log.", options: { color: TXT } }],
    { x: 0.55, y: 6.55, w: 12.2, h: 0.4, margin: 0, fontFace: "Arial", fontSize: 13 });
}

/* ---------------- SLIDE 19 — COMMAND PALETTE & NAV ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 18, "Command Palette & Navigation", "Productivity");
  shot(s, "palette.png", 0.55, 1.75, 7.7);
  infoCard(s, 8.7, 1.75, 4.05, 4.8, "Move & command fast", [
    { t: "Ctrl+P — fuzzy Command Palette for every action", },
    { t: "LMB drag orbit · double-click to focus · RMB free-look", },
    { t: "Wheel zooms to cursor; WASD + Q/E to fly (Shift = 5× fast)", },
    { t: "Presets: 1 front · 3 side · 7 top · 5 orthographic · F frame all", },
    { t: "F1 shows the full keyboard-shortcut overlay", c: MUT },
  ], { gap: 12 });
  caption(s, 0.55, 6.62, 7.7, "Command Palette — type to filter, Enter to run");
}

/* ---------------- SLIDE 20 — PREFERENCES ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 19, "Preferences", "Settings");
  const iw = 6.0, iy = 1.9;
  shot(s, "prefs_general.png", 0.55, iy, iw);
  shot(s, "prefs_input.png", 6.95, iy, iw);
  caption(s, 0.55, iy + iw / AR + 0.14, iw, "General — theme, UI scale, auto-load, .vxpc file association");
  caption(s, 6.95, iy + iw / AR + 0.14, iw, "Input — gamepad sensitivity, deadzone, serial controller");
  s.addText([{ text: "Open:  ", options: { bold: true, color: ACC } },
    { text: "Edit ▸ Preferences… (Ctrl+,). Tabs: General · Display (stereo IPD) · Input · Advanced (reset all). Settings persist between sessions.", options: { color: TXT } }],
    { x: 0.55, y: 6.55, w: 12.2, h: 0.4, margin: 0, fontFace: "Arial", fontSize: 13 });
}

/* ---------------- SLIDE 21 — WEB REMOTE & CONTROLLERS ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 20, "Web Remote & Controllers", "Connectivity");
  shot(s, "prefs_web.png", 0.55, 1.75, 7.7);
  infoCard(s, 8.7, 1.75, 4.05, 4.8, "Drive from a phone or pad", [
    { t: "Web remote — a phone-browser control page on your WiFi (default port 8899)", },
    { t: "Two roles: a Driver PIN (full control) and a Viewer PIN (watch-only)", },
    { t: "Show connect QR to join instantly; live JPEG/WebRTC viewport streaming", },
    { t: "Gamepad — enable & tune deadzone / look / move sensitivity", },
    { t: "Serial (Bluetooth) controller — auto-detect by MAC or set the COM port", c: MUT },
  ], { gap: 11 });
  caption(s, 0.55, 6.62, 7.7, "Preferences ▸ Input — web remote (serving URL & PINs redacted)");
}

/* ---------------- SLIDE 22 — SHORTCUTS TABLE ---------------- */
{
  const s = pres.addSlide(); bg(s);
  header(s, 21, "Keyboard Shortcuts", "Reference");
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
  s.addText("Movement keys are ignored while typing in a field. Gamepad & serial-pad mappings are listed in Preferences ▸ Input.",
    { x: 0.55, y: 6.85, w: 12.2, h: 0.35, margin: 0, fontFace: "Arial", fontSize: 11.5, italic: true, color: MUT });
}

/* ---------------- SLIDE 23 — CLOSING ---------------- */
{
  const s = pres.addSlide(); bg(s, BG2);
  s.addText("VIITORX POINTCLOUD VIEWER", { x: 0.7, y: 2.4, w: 12, h: 0.5, margin: 0, fontFace: "Arial", fontSize: 16, bold: true, color: ACC, charSpacing: 5 });
  s.addText("Convert once, explore anything.", { x: 0.66, y: 2.9, w: 12, h: 1.0, margin: 0, fontFace: "Cambria", fontSize: 40, bold: true, color: TXT });
  s.addText("Billion-point clouds, streamed interactively — with measurement, clipping, slice export, camera-path video, stereoscopic 3D and a phone web-remote.",
    { x: 0.7, y: 4.0, w: 9.6, h: 0.9, margin: 0, fontFace: "Arial", fontSize: 15, color: MUT });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 0.7, y: 5.1, w: 1.5, h: 0.5, fill: { color: CARD2 }, rectRadius: 0.25 });
  s.addText("v1.0.56", { x: 0.7, y: 5.1, w: 1.5, h: 0.5, align: "center", valign: "middle", margin: 0, fontFace: "Arial", fontSize: 14, bold: true, color: ACC });
}

pres.writeFile({ fileName: path.join(__dirname, "ViitorXPC_Feature_Guide.pptx") }).then((f) => console.log("WROTE", f));
