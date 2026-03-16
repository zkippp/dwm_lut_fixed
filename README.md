# A fork of the [original dwm_lut](https://github.com/ledoge/dwm_lut) - Updated for Windows 11 25H2
### Current Version: v1.0.5_25H2
## [Download latest release](https://github.com/zkippp/dwm_lut_fixed/releases/tag/v1.0.5_25H2)

## Credits
- **Original Author**: [ledoge](https://github.com/ledoge/dwm_lut)
- **25H2 Update & Maintenance**: [Eduu](https://github.com/zkippp/dwm_lut_fixed)
- **Our discord**: [Discord Invite](https://discord.gg/Y9Zcf8jGAN)

## Dependencies
- Visual C++ runtime (https://www.techpowerup.com/download/visual-c-redistributable-runtime-package-all-in-one/)

# About
This tool applies 3D LUTs to the Windows desktop by hooking into DWM. It works in both SDR and HDR modes, and uses tetrahedral interpolation on the LUT data. In SDR, blue-noise dithering is applied to the output to reduce banding.

### Windows 11 25H2 (Build 26200+) Support
The latest Windows 11 builds introduced significant internal changes to how DWM handles swap chains and overlays. This fork has been updated with:
- **Direct Texture Retrieval**: On 25H2, `IDXGISwapChain` is no longer directly exposed. We now use a recursive vtable traversal (`overlaySwapChain->vt[24]()->vt[19]()`) to safely acquire the backbuffer.
- **Memory Offset Updates**: Corrected `DeviceClipBox` and internal coordinate structures that shifted in Build 26200+ (positions now stored as integers at new offsets).
- **MPO/DirectFlip Management**: Implemented a memory patch that sets DWM's internal `OverlayTestMode` to `5`, ensuring LUTs remain applied even when games try to use Multi-Plane Overlays.
- **Multi-Monitor Fix**: Resolved issues where LUTs would fail to apply onto secondary monitors on the latest canary/dev builds.

# Usage
Use DisplayCAL or similar to generate .cube LUT files of any size, run `DwmLutGUI.exe`, assign them to monitors and then click Apply. Note that LUTs cannot be applied to monitors that are in "Duplicate" mode.

For ColourSpace users with HT license level, 65^3 eeColor LUT .txt files are also supported.

HDR LUTs must use BT.2020 + SMPTE ST 2084 values as input and output.

# Compiling
Install [vcpkg](https://vcpkg.io/en/getting-started.html) for C++ dependency management:

- `git clone https://github.com/Microsoft/vcpkg.git`
- `.\vcpkg\bootstrap-vcpkg.bat`
- `.\vcpkg\vcpkg.exe integrate install`

Open the projects in Visual Studio 2022 and compile a **x64 Release** build. Ensure you have the C++ Desktop Development workload installed.
