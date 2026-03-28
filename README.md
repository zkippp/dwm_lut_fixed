# dwm_lut_fixed (v1.0.8)

[![GitHub stars](https://img.shields.io/github/stars/zkippp/dwm_lut_fixed?style=for-the-badge&color=gold)](https://github.com/zkippp/dwm_lut_fixed/stargazers)
[![GitHub releases](https://img.shields.io/github/v/release/zkippp/dwm_lut_fixed?include_prereleases&style=for-the-badge&color=blue)](https://github.com/zkippp/dwm_lut_fixed/releases)
[![License](https://img.shields.io/github/license/zkippp/dwm_lut_fixed?style=for-the-badge&color=green)](https://github.com/zkippp/dwm_lut_fixed/blob/master/LICENSE)

Maintenance fork of the original `dwm_lut` project, updated and maintained for Windows 11 builds (24H2 and 25H2). This tool enables 3D LUT application for desktop color calibration across multiple monitors.

## Key Features

- **Windows 11 Compatible**: Full support for **25H2 (Insider)**, **24H2 (Direct composition)**, and **23H2 (Build 22631)**.
- **Multi-Monitor & Multi-GPU Fix**: Reliable LUT application across multiple displays and GPUs on all modern Windows versions.
- **HDR Compatibility**: Improved logic for HDR/SDR switching and primary context matching.
- **MPO/DirectFlip Management**: Automated memory patching for `OverlayTestMode`, ensuring persistent LUT application during full-screen scenarios.
- **Enhanced .cube Parser**: Support for DisplayCAL generated LUTs, including negative values and floating-point data.

## Useful Links

- **Discord**: [Join Server](https://discord.gg/Y9Zcf8jGAN)
- **Releases**: [Download Latest](https://github.com/zkippp/dwm_lut_fixed/releases)
- **Technical Info**: [Documentation](https://github.com/zkippp/dwm_lut_fixed/wiki)

## Support the project

[![Ko-fi](https://img.shields.io/badge/Ko--fi-F16061?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/zkippp)

## Dependencies

- **Visual C++ Runtime**: [AIO Redistributable](https://www.techpowerup.com/download/visual-c-redistributable-runtime-package-all-in-one/)

## Build Instructions

1. Install **Visual Studio 2022** with C++ desktop development.
2. Clone repository.
3. Integrate vcpkg: `.\vcpkg\vcpkg.exe integrate install`.
4. Build in **x64 Release** configuration.

## Credits

- **Original Author**: [ledoge](https://github.com/ledoge/dwm_lut)
- **Maintenance**: [zkippp](https://github.com/zkippp/dwm_lut_fixed)
