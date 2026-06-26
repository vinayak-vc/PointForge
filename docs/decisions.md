# Architectural Decisions

This document records major design decisions.

## Out-of-Core Processing
- **Decision**: The importer uses a Three-Phase algorithm (Count, Chunk, Index).
- **Reason**: Processing datasets exceeding RAM capacity requires bounded memory streaming. The "Chunk" phase acts as a spatial scatter, meaning Phase C (Index) can operate completely independently in parallel on separate chunks with no locking.

## Viewer Stack
- **Decision**: Uses SDL2 + OpenGL 3.3 Core + Dear ImGui.
- **Reason**: Maximizes cross-platform compatibility without heavy dependencies (like Qt or modern Vulkan which complicates driver support for older enterprise hardware).

## CPU / GPU Separation
- **Decision**: Point nodes stream directly from disk to GPU memory (`glBufferData`) and are then immediately freed from CPU RAM.
- **Reason**: Double buffering points in CPU RAM causes massive overhead. 
- **Consequence**: Implemented CPU-based point-picking requires loading a chunk specifically for intersection, or building a dedicated picking octree, which is a known limitation.

## Application Name & Presentation
- **Decision**: The executable is built as a WIN32 subsystem application (on Windows) rather than a Console App.
- **Reason**: This prevents a persistent and ugly DOS prompt terminal window from opening alongside the ViitorX Viewer application.

## Settings Persistence
- **Decision**: Viewer settings are serialized to a local `pfview_config.txt` file manually rather than relying on a complex JSON parser.
- **Reason**: Avoids heavy dependencies, keeping the repository light and builds fast.
