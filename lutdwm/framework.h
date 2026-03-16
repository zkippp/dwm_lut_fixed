#pragma once


#define WIN32_LEAN_AND_MEAN             

#include <windows.h>
#include <MinHook.h>

#if defined(_M_X64)
#if defined(_DEBUG)
#pragma comment(lib, "vcpkg_installed\\x64-windows-static\\x64-windows-static\\debug\\lib\\minhook.x64d.lib")
#else
#pragma comment(lib, "vcpkg_installed\\x64-windows-static\\x64-windows-static\\lib\\minhook.x64.lib")
#endif
#endif

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <psapi.h>
#include <cstdio>
#include <intrin.h>
#include <vector>
#include "noise.h"