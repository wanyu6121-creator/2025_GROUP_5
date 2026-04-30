EEEE2076 Group CAD Viewer

Overview

The EEEE2076 Group CAD Viewer is a desktop application developed using Qt (GUI) and VTK (rendering engine) with optional OpenVR integration.

It allows users to load and visualise 3D CAD models interactively, with support for both standard desktop viewing and immersive VR environments.

---

Key Features

- Load and render CAD models (e.g. STL)
- Interactive GUI built with Qt
- Real-time 3D rendering via VTK
- Model structure management (ModelPart, ModelPartList)
- Custom settings dialog
- VR support using OpenVR (SteamVR compatible)

---

System Architecture


UI Layer (Qt)
    ↓
Application Logic
    ↓
Model Management (ModelPart / ModelPartList)
    ↓
Rendering Engine (VTK)
    ↓
(Optional) VR Layer (OpenVR)


---

Project Structure


include/        → Header files
src/            → Source files
vrbindings/     → OpenVR integration
icons/          → UI resources
docs/           → Documentation & images


---

Requirements

Minimum:

- Windows 10/11 (64-bit)
- OpenGL compatible GPU
- Microsoft Visual C++ Redistributable

For VR Mode:

- VR Headset (e.g. HTC Vive / Oculus)
- SteamVR installed

---

Installation

1. Run the installer:


Setup.exe


2. Follow installation steps

3. Launch the application from:

C:\Program Files\CADViewer\bin\worksheet6.exe

---

Usage

Desktop Mode

1. Open the application
2. Load a CAD file (STL)
3. Rotate / zoom / interact using mouse

VR Mode

1. Start SteamVR
2. Launch the application
3. Wear headset to view in VR

---

Documentation

Doxygen documentation is available online:

https://floatycmd.github.io/2025_20799326/

Includes:

- Class hierarchy
- File documentation
- Function reference

---

Example Classes

| Class           | Description                |
| --------------- | -------------------------- |
| MainWindow    | Main UI controller         |
| ModelPart     | Represents a CAD component |
| ModelPartList | Manages model hierarchy    |
| OptionDialog  | User settings panel        |

---

Authors

- Karn Chotamungsa
- Adrian Okae
- Wanyu Yin

---

Notes

- All required dependencies (Qt, VTK, OpenVR) are bundled in the installer
- If the application fails to start, check DLLs in `/bin`
- VR mode requires SteamVR to be running before launch

---

Conclusion

- This project demonstrates integration of **GUI (Qt)**, **3D rendering (VTK)**, and **VR technologies (OpenVR)** into a single deployable engineering application.

---
