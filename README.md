# EEEE2076 — Software Engineering & VR Project

A Qt/VTK desktop application for loading, visualising, and interactively editing 3D STL models, with optional HTC Vive VR rendering via OpenVR.

---

## Features

### 3D model viewer
- Load individual STL files or entire directories (folder hierarchy maps to the tree view)
- Interactive tree view listing every loaded part with visibility toggle
- Edit part name, colour (RGB spin boxes or colour picker), and visibility via the Options dialog
- Delete individual parts from the tree and renderer

### VTK filter pipeline
Each part supports five independently toggleable filters, applied in a chained pipeline:

| Filter | Effect |
|--------|--------|
| **Clip** | Cuts geometry at the model's X-centre along the −X normal |
| **Shrink** | Pulls each face toward its centroid (factor 0.6), exposing gaps |
| **Smooth** | Laplacian smoothing — 20 iterations, softens sharp edges |
| **Decimate** | Reduces polygon count by 90% using vtkDecimatePro |
| **Elevation** | Maps Z-height to a blue→red rainbow colour table |

### Lighting
- Dual-light rig (key + fill) controllable via a slider (0–200% intensity)
- Lighting syncs between the GUI renderer and the VR thread in real time

### VR rendering (`VRRenderThread`)
- Automatically detects an HTC Vive headset at startup
  - **VR mode** — renders via `vtkOpenVRRenderWindow`
  - **Desktop fallback** — renders in a plain window when no headset is connected
- Skybox background (deep-space environment)
- Auto-rotation animation (start/stop)
- Four named view presets: Front, Top, Right Side, Isometric
- Reset view — snaps all parts back to their original positions
- Controller trigger drag — grab and reposition individual parts in VR
- In-VR part selection: highlight, toggle visibility, cycle colour, toggle clip/shrink filters

---

## Dependencies

| Dependency | Minimum version | Notes |
|------------|----------------|-------|
| CMake | 3.16 | Build system |
| Qt | 5.15 or 6.x | Widgets, OpenGLWidgets |
| VTK | 9.x (with OpenVR) | Built with `-DVTK_ENABLE_VR_SUPPORT=ON` |
| OpenVR SDK | 1.x | Required for VR headset support |
| C++ compiler | C++17 | MSVC 2019+, GCC 10+, or Clang 12+ |

> **Note:** VTK must be compiled with `VTK_ENABLE_VR_SUPPORT=ON` and the OpenVR SDK must be installed and discoverable by CMake. If VTK was installed via a package manager it may not include OpenVR support — see the VTK build instructions below.

---

## Building

### 1. Clone the repository

```bash
git clone https://github.com/<your-org>/<your-repo>.git
cd <your-repo>
```

### 2. Configure with CMake

```bash
cmake -S src -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DVTK_DIR=/path/to/vtk/install/lib/cmake/vtk \
  -DQt6_DIR=/path/to/qt/install/lib/cmake/Qt6
```

On Windows with Visual Studio, open the `build/` folder in Visual Studio after configuration, or use the CMake GUI.

### 3. Build

```bash
cmake --build build --config Release
```

### 4. Run

```bash
./build/worksheet6        # Linux / macOS
build\Release\worksheet6  # Windows
```

The `vrbindings/` directory is copied automatically into the build folder by CMake. If you are running in VR, ensure SteamVR is running before launching the application.

---

## Repository structure

```
.
├── README.md               ← you are here
├── .gitignore
├── src/                    ← all C++ source and CMakeLists.txt
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── mainwindow.h / .cpp / .ui
│   ├── ModelPart.h / .cpp
│   ├── ModelPartList.h / .cpp
│   ├── VRRenderThread.h / .cpp
│   ├── optiondialog.h / .cpp / .ui
│   ├── icons.qrc
│   ├── Icon/
│   └── README.md
├── docs/                   ← Doxygen configuration and landing page
│   ├── Doxyfile
│   ├── mainpage.md
│   └── README.md
└── .github/
    └── workflows/
        └── docs.yml        ← auto-generates and deploys documentation
```

---

## Documentation

API documentation is published automatically to GitHub Pages on every push to `main`:

**https://\<your-org\>.github.io/\<your-repo\>/**

To build the documentation locally:

```bash
cd docs
doxygen Doxyfile
# Output written to docs/html/index.html
```

---

## Usage overview

1. **Load a model** — use *File > Open File* for a single STL, or *File > Open Directory* to load a whole folder tree.
2. **Select a part** — click any row in the tree view on the left.
3. **Edit properties** — click *Options* or right-click for the context menu. Adjust name, colour, and visibility.
4. **Apply filters** — tick the filter checkboxes in the toolbar. Filters apply immediately to the selected part.
5. **Start VR** — click *Start VR*. The application will use your Vive headset if SteamVR is running, otherwise a desktop preview window opens.
6. **Adjust lighting** — drag the light intensity slider. Changes propagate to the VR renderer in real time.

---

## Authors

EEEE2076 Group — University of Leeds, 2024/25
