# PointForge Project Overview

PointForge is an out-of-core octree point cloud converter and streaming viewer. It is designed to handle extremely large point cloud datasets (e.g. LiDAR data with billions of points) that cannot fit entirely in memory.

## Architecture Highlights
- **Converter (`pfconvert`)**: Uses a three-phase strategy (Count, Chunk, Index) to build out-of-core spatial hierarchies (Octrees) from input `.las`, `.laz`, and `.e57` files. This ensures that memory consumption is strictly bounded by the chunk grid depth rather than the raw data size.
- **Viewer (`pfview`)**: Also known as **ViitorX PointCloud Viewer**, a fast hardware-accelerated viewer built using SDL2, OpenGL 3.3, and Dear ImGui. It streams the spatial hierarchy out-of-core, keeping a strict limit on GPU budget and CPU memory, to smoothly display massive environments.

## Core Features
- Dynamic Level-of-Detail (LOD) streaming based on pixel-size budget
- Custom visualizations including True Color, Elevation gradients (Turbo map), and Solid Color rendering
- Advanced clipping planes, stereo (SBS) mode, and camera features
- Fast C++ core running efficiently across multiple threads for both rendering and conversion
- Viewport-centric **docked UI shell** (menu bar, toolbar, dockable Properties/Jobs/Console/Performance panels, status bar) — conversion runs as background jobs, monitored via a status-bar pill and Jobs panel rather than blocking the viewer
- Stereoscopic SBS mode hides all UI chrome (no menu/toolbar/HUD reaching either eye) for clean stereoscope viewing
