# AGENTS.md

## Documentation

Maintain:

- docs/project-overview.md
- docs/architecture.md
- docs/roadmap.md
- docs/tasks.md
- docs/decisions.md
- docs/ai_handoff.md

## Before Coding

- Read architecture.md
- Read roadmap.md
- Read ai_handoff.md
- Summarize understanding before implementation

## During Coding

- Follow existing patterns
- Update tasks.md
- Update decisions.md for major decisions

## After Coding

- Update ai_handoff.md
- Record modified files
- Record next recommended task

## Architecture

- Prefer consistency over novelty
- Document architectural changes before introducing them

## Handoff

Assume another AI agent may continue the project tomorrow.
Optimize for agent portability.

<claude-mem-context>
# Memory Context

# [PointForge] recent context, 2026-07-07 1:07pm GMT+5:30

Legend: 🎯session 🔴bugfix 🟣feature 🔄refactor ✅change 🔵discovery ⚖️decision 🚨security_alert 🔐security_note
Format: ID TIME TYPE TITLE
Fetch details: get_observations([IDs]) | Search: mem-search skill

Stats: 50 obs (20,456t read) | 315,043t work | 94% savings

### Jun 12, 2026
S411 User asked about "Leaf size" — explained the --leaf parameter controlling max points per leaf node in octree point cloud LOD structures (Jun 12, 7:12 PM)
S412 User asked about "Max Depth" — explained the --max-depth parameter as a hard cap on octree subdivision levels in point cloud conversion (Jun 12, 7:13 PM)
S453 /init — Initialize a new CLAUDE.md file with codebase documentation (Jun 12, 7:15 PM)
### Jun 17, 2026
S454 Implement Eye-Dome Lighting (EDL) post-process material and viewer panel controls for the PointForge point cloud viewer in Unreal Engine 5.5.4 (Jun 17, 4:00 PM)
S455 Execute the build_edl_material.py script in Unreal Engine to generate the M_EDL post-process material asset for EDL feature testing (Jun 17, 4:11 PM)
S456 Fix EDL post-process material build script failing with missing Unreal Python API nodes, then compile PointForgeViewer plugin via Live Coding (Jun 17, 4:15 PM)
S457 Point cloud renderer debugging in Unreal Engine — color modes, elevation fix, clipping plane usage, EDL visibility, and next feature priorities (Jun 17, 4:27 PM)
S483 PointForgeViewer UE Plugin: Complete 7 outstanding tasks one by one — file picker, elevation fit, cache UI, intensity fix, standalone build fixes, point count slider, commit/push (Jun 17, 4:54 PM)
1893 5:10p 🔵 UPFConvertPanel Widget Architecture in PointForgeViewer Plugin
1894 5:11p 🔄 UPFConvertPanel Migrated from BindWidget to Fully Procedural RebuildWidget Pattern
1895 5:13p 🔵 PFConvertPanel.cpp Implementation State — Missing RebuildWidget, BrowseBtn, and CompressCheck Handlers
1896 5:14p 🔵 PointForgeViewer Build.cs — Dual-Mode pfcore Architecture with Zstd and Optional In-Process Linking
1897 " 🔵 UPFConvertSettings — bCompress Field Confirmed, Full Parameter Schema Documented
1898 " 🔵 UPFViewerPanel RebuildWidget() Pattern — Reference Implementation for PFConvertPanel
1899 " ✅ PFConvertPanel.cpp — Editor-Only Desktop Platform Includes Added for File Picker
1900 5:15p 🟣 PFConvertPanel RebuildWidget() Fully Implemented — Self-Contained Procedural UI
1901 " 🟣 OnBrowseClicked and OnCompressChanged Implemented in PFConvertPanel
1902 " ✅ DesktopPlatform Module Added to Build.cs Under Target.bBuildEditor Guard
1903 " 🔵 Live Coding Compile Triggered — Previous Build Succeeded with Component Attachment Errors
1904 5:16p 🟣 Live Coding patch_2 Compiled Successfully — PFConvertPanel RebuildWidget and Browse Button Changes Live
1905 " 🔵 PFViewerPanel Full Widget Inventory — Extended Controls and EDL Section
1906 " 🟣 FitElevationBtn Added to PFViewerPanel — Auto-Fit Elevation Range Button
1907 " ✅ Button.h Include Added to PFViewerPanel.cpp for FitElevationBtn
1908 " 🟣 FitElevationBtn Constructed and Wired in PFViewerPanel RebuildWidget
1909 5:17p 🟣 OnFitElevationClicked Implemented and Elevation Slider Range Expanded 100x
1910 " 🟣 Live Coding patch_3 Compiled Successfully — FitElevationBtn and Elevation Range Changes Live
1911 " 🔵 FPFConvert Cache Architecture — Convert-Once-Then-Stream with Hash-Based Cache Dirs
1912 5:18p 🔵 PFPointCloudComponent Elevation Auto-Range and KeyString Cache Invalidation Logic
### Jun 18, 2026
1998 2:29p 🔵 PointForgeViewer UE Plugin Build Configuration
1999 " 🔵 PFConvert Cache Key Strategy and Directory Layout
2000 2:30p ✅ Upgraded RuntimeDependencies Staging to Two-Arg Form with Multi-Source Fallback
2001 " 🔵 PFConvert Cache Management and pfconvert.exe Location Logic
2002 2:31p 🔴 LocatePfConvert Gains Portable Fallback via FPlatformProcess::BaseDir()
2003 " 🔵 Point Cloud Scene Proxy Rendering: Quad Billboard Geometry and Point Count Budget
2004 " 🔵 PointCountLimit Data Flow: UMG Slider → Component → Render Thread
2005 " 🔴 Intensity Extraction Heuristic for 8-bit, 12-bit, and 16-bit LAS Fields
2006 " 🔄 Billboard Material Script: Direct A-Pin and HLSL Custom Node Replace If-Chain
2007 2:32p ✅ Committed feat: standalone packaging, intensity auto-scale, classification HLSL (ca44ac9)
2008 " ✅ Pushed ca44ac9 to GitHub Remote (vinayak-vc/UEPointForge)
S484 Commit and push changes in the PointForgeViewer Unreal Engine plugin repository (Jun 18, 2:32 PM)
2009 2:50p 🔵 PointForgeViewer Plugin Working Tree Already Clean
2010 4:10p 🔵 Unreal Editor Crash: EXCEPTION_ACCESS_VIOLATION in RenderCore/Engine
2011 " 🔵 PointForge OctreeStore: Async Point Cloud Loader Architecture
2012 " 🔵 Crash Log File Located for StereoScopicProject Access Violation
2013 4:11p 🔵 StereoScopicProject Crash Log Analysis: PLY Point Cloud Load Preceded Crash
2014 " 🔵 CrashContext.runtime-xml: Render Thread Null Dereference Confirmed, Not GameThread
2015 4:12p 🔵 PointForgeViewer Plugin Structure Mapped: Scene Proxy Holds Raw UMaterialInterface* — Likely Crash Source
2016 4:14p 🔵 PFPointCloudComponent: GetUsedMaterials Roots Material, but TickComponent Has Unsafe SceneProxy Capture
S485 Unreal Editor crash (EXCEPTION_ACCESS_VIOLATION 0x0000000000000101) in StereoScopicProject PointForge point cloud viewer plugin — diagnosed and fixed (Jun 18, 4:14 PM)
2017 4:17p 🔵 Unreal Editor Crash: EXCEPTION_ACCESS_VIOLATION in RenderCore/Engine
2018 4:18p 🔵 Crash Log Trace: PointForge Point Cloud Load Precedes EXCEPTION_ACCESS_VIOLATION
2019 " 🔵 Exact PCallStack Extracted: Null Function Pointer Call in UnrealEditor-Engine via RenderCore
2020 4:19p 🔵 Crash Thread Confirmed as RenderThread; ONNX Runtime Threads Active at Crash Time
2021 4:20p 🔵 FPFOctreeStore Architecture: Background Streaming Thread Feeds Render Thread via Lock-Free Queues
2022 " 🔵 CreateSceneProxy() Guards on Store Validity at Creation But Not During Proxy Lifetime
2023 4:21p 🔵 PFOctreeStore.cpp Full Pipeline: mmap → Quad Expansion → MPSC ResultQueue; Pending Results Survive StopWorker()
2024 4:23p 🔵 UPFPointCloudComponent: Store is TSharedPtr (Game Thread), Stats Shared with Proxy via ThreadSafe TSharedPtr
2025 4:46p 🔵 Unreal Editor Crash: EXCEPTION_ACCESS_VIOLATION in RenderCore/Engine
2026 4:47p 🔵 UE5.5: PrimitiveUniformBufferResource Defined in MeshBatch.h as TUniformBuffer Pointer
### Jun 19, 2026
2027 10:31a 🔵 UE5 StereroScopicProject Crash: Null Pointer in RenderCore

Access 315k tokens of past work via get_observations([IDs]) or mem-search skill.
</claude-mem-context>