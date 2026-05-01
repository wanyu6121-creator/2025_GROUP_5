# src/

This directory contains all C++ source code for the EEEE2076 VR viewer application.

---

## How to build

From the **repository root**, run:

```bash
cmake -S src -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DVTK_DIR=/path/to/vtk/lib/cmake/vtk \
  -DQt6_DIR=/path/to/qt/lib/cmake/Qt6

cmake --build build --config Release
```

See the [root README](../README.md) for full dependency and platform notes.

---

## File overview

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | CMake build definition; finds Qt, VTK, and OpenVR |
| `main.cpp` | Application entry point — constructs `QApplication` and `MainWindow` |
| `mainwindow.h / .cpp / .ui` | Main window: tree view, VTK renderer, toolbar, all slot logic |
| `ModelPart.h / .cpp` | Tree node — owns the VTK pipeline for one STL part (5 filters) |
| `ModelPartList.h / .cpp` | `QAbstractItemModel` subclass that backs the tree view |
| `VRRenderThread.h / .cpp` | `QThread` subclass — VR/desktop render loop, command queue |
| `optiondialog.h / .cpp / .ui` | Modal dialog for editing part name, colour, and visibility |
| `icons.qrc` | Qt resource file bundling toolbar icons |
| `Icon/` | Source icon image files referenced by `icons.qrc` |

---

## Class relationships

```
MainWindow
 ├── ModelPartList  (QAbstractItemModel)
 │    └── ModelPart  (tree node, owns vtkActor + filter pipeline)
 ├── VRRenderThread (QThread — runs render loop)
 └── OptionDialog   (modal dialog, launched on demand)
```

### Key design points

- **`ModelPart`** owns a complete VTK pipeline from `vtkSTLReader` through up to five chained filters to a `vtkDataSetMapper`. Calling any `set*()` filter method rewires the pipeline immediately via `updatePipeline()`.
- **`VRRenderThread`** communicates with `MainWindow` exclusively through a thread-safe command queue (`issueCommand()`) and a pending-actor queue (`queueAddActor()`). No VTK objects are shared across the thread boundary.
- **`ModelPartList`** is a standard Qt model. `appendChild()` and `removeItem()` call `beginInsertRows` / `beginRemoveRows` so the tree view updates automatically.

---

## VR bindings

The `vrbindings/` directory at the repository root must be present at build time. CMake copies it into the build output directory automatically. It contains the OpenVR action manifest files required for controller input.

> If the `vrbindings/` folder is missing, the VR thread will start but controller input will not function. Desktop fallback mode is unaffected.
