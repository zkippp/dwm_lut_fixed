# DwmLut Documentation

## Architecture Overview

DwmLut is composed of a C++ core (`lutdwm`) and a C# management interface (`DwmLutGUI`).

### 1. The Core Engine (`lutdwm`)
The core engine is a dynamic link library (`lutdwm.dll`) designed for injection into `dwm.exe` (Desktop Window Manager).

#### Key Responsibilities:
- **Direct3D Hooking**: Intercepts DWM rendering calls using vtable pinning and AOB scanning.
- **LUT Application**: Uses a custom pixel shader to apply 3D LUT data via tetrahedral interpolation.
- **Dithering**: Implements blue-noise dithering for SDR display modes to maintain bit-depth integrity.
- **Windows Version Compatibility**: Contains specific logic/offsets for Windows 10, Windows 11 (22H2/23H2), and Windows 11 25H2.

#### Core Files:
- `lutdwm/dllmain.cpp`: Main entry point, hooking logic, and shader management.
- `lutdwm/noise.h`: Blue noise texture data for dithering.
- `lutdwm/pch.h`: Precompiled headers.

### 2. The GUI Manager (`DwmLutGUI`)
A WPF application used to configure and monitor the LUT application status.

#### Key Features:
- **Per-Monitor Calibration**: Detects all connected monitors and allows assigning different `.cube` files to each.
- **UAC Bypass Autostart**: Uses Windows Task Scheduler to launch with highest privileges on system logon.
- **DLL Injection**: Automates the `CreateRemoteThread` injection process into `dwm.exe`.
- **Minimized Operation**: Runs in the system tray to keep LUTs active without cluttering the taskbar.

#### Project Layout:
- `DwmLutGUI/MainWindow.xaml`: Main user interface.
- `DwmLutGUI/Injector.cs`: Handles process discovery and DLL injection.
- `DwmLutGUI/MainViewModel.cs`: Core application logic and state management.

---

## Technical Deep Dive: Windows 11 25H2 Support

Windows 11 Build 26200 (25H2) significantly refactored DWM's internal structures. This project addresses these changes as follows:

- **IDXGISwapChain Discovery**: DWM no longer uses `IDXGISwapChain` in a detectable way via standard patterns. Instead, the engine looks for `IOverlaySwapChain` and calls `vt[24]` to retrieve the underlying swap chain interface safely.
- **DeviceClipBox Offsets**: Coordinate offsets for monitor clipping were moved from `0x120` to `0x466C` (and recently `0x53E8` in Canary builds).
- **OverlayTestMode**: To ensure the LUT is applied even during "DirectFlip" or MPO (Multi-Plane Overlays), the engine patches `g_pOverlayTestMode` to `5`.

## Compilation & Development

### Prerequisites:
- Visual Studio 2022
- C++ Desktop Development Workload
- .NET Desktop Development Workload
- [vcpkg](https://vcpkg.io/)

### Build Configuration:
1.  **Release x64**: The project MUST be built as x64 to match the DWM process architecture.
2.  **Dependencies**: Run `vcpkg integrate install` before opening the solution.

---

*Last Updated: March 2026*
