
#include "pch.h"

#include <io.h>
#include <map>
#include <mutex>
#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#pragma comment (lib, "d3d11.lib")
#pragma comment (lib, "d3dcompiler.lib")
#pragma comment (lib, "dxgi.lib")
#pragma comment (lib, "uuid.lib")
#pragma comment (lib, "dxguid.lib")


#pragma intrinsic(_ReturnAddress)

#define DITHER_GAMMA 2.2
#define LUT_FOLDER "%SYSTEMROOT%\\Temp\\luts"

#define RELEASE_IF_NOT_NULL(x) { if (x != NULL) { x->Release(); } }
#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)
#define RESIZE(x, y) realloc(x, (y) * sizeof(*x));
#define LOG_FILE_PATH R"(C:\DWMLOG\dwm.log)"
#define MAX_LOG_FILE_SIZE 20 * 1024 * 1024
#ifdef _DEBUG
#define DEBUG_MODE true
#else
#define DEBUG_MODE false
#endif

#if DEBUG_MODE == true
#define __LOG_ONLY_ONCE(x, y) if (static bool first_log_##y = true) { log_to_file(x); first_log_##y = false; }
#define _LOG_ONLY_ONCE(x, y) __LOG_ONLY_ONCE(x, y)
#define LOG_ONLY_ONCE(x) _LOG_ONLY_ONCE(x, __COUNTER__)
#define MESSAGE_BOX_DBG(x, y) MessageBoxA(NULL, x, "DEBUG HOOK DWM", y);

#define EXECUTE_WITH_LOG(winapi_func_hr) \
	do { \
		HRESULT hr = (winapi_func_hr); \
		if (FAILED(hr)) \
		{ \
			std::stringstream ss; \
			ss << "ERROR AT LINE: " << __LINE__ << " HR: " << hr << " - DETAILS: "; \
			LPSTR error_message = nullptr; \
			FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, \
				NULL, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&error_message, 0, NULL); \
			ss << error_message; \
			log_to_file(ss.str().c_str()); \
			LocalFree(error_message); \
			throw std::exception(ss.str().c_str()); \
		} \
	} while (false);

#define EXECUTE_D3DCOMPILE_WITH_LOG(winapi_func_hr, error_interface) \
	do { \
		HRESULT hr = (winapi_func_hr); \
		if (FAILED(hr)) \
		{ \
			std::stringstream ss; \
			ss << "ERROR AT LINE: " << __LINE__ << " HR: " << hr << " - DETAILS: "; \
			LPSTR error_message = nullptr; \
			FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, \
				NULL, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&error_message, 0, NULL); \
			ss << error_message << " - DX COMPILE ERROR: " << (char*)error_interface->GetBufferPointer(); \
			error_interface->Release(); \
			log_to_file(ss.str().c_str()); \
			LocalFree(error_message); \
			throw std::exception(ss.str().c_str()); \
		} \
	} while (false);

#define LOG_ADDRESS(prefix_message, address) \
	{ \
		std::stringstream ss; \
		ss << prefix_message << " 0x" << std::setw(sizeof(address) * 2) << std::setfill('0') << std::hex << (UINT_PTR)address; \
		log_to_file(ss.str().c_str()); \
	}

#else
#define LOG_ONLY_ONCE(x)
#define MESSAGE_BOX_DBG(x, y)
#define EXECUTE_WITH_LOG(winapi_func_hr) winapi_func_hr;
#define EXECUTE_D3DCOMPILE_WITH_LOG(winapi_func_hr, error_interface) winapi_func_hr;
#define LOG_ADDRESS(prefix_message, address)
#endif


#if DEBUG_MODE == true
void log_to_file(const char* log_buf)
{
	FILE* pFile = fopen(LOG_FILE_PATH, "a");
	if (pFile == NULL)
	{
		return;
	}
	fseek(pFile, 0, SEEK_END);
	long size = ftell(pFile);
	if (size > MAX_LOG_FILE_SIZE)
	{
		if (_chsize(_fileno(pFile), 0) == -1)
		{
			fclose(pFile);
			return;
		}
	}
	fseek(pFile, 0, SEEK_END);
	fprintf(pFile, "%s\n", log_buf);
	fclose(pFile);
}
#endif

static int g_DynamicSwapChainOffset = -1;
struct RtvCacheEntry {
	ID3D11Texture2D* texture;
	ID3D11RenderTargetView* rtv;
};

struct DeviceContext {
	ID3D11Device* device = NULL;
	ID3D11DeviceContext* deviceContext = NULL;
	ID3D11VertexShader* vertexShader = NULL;
	ID3D11PixelShader* pixelShader = NULL;
	ID3D11InputLayout* inputLayout = NULL;
	ID3D11Buffer* vertexBuffer = NULL;
	UINT numVerts = 4;
	UINT stride = 4 * sizeof(float);
	UINT offset = 0;
	D3D11_TEXTURE2D_DESC backBufferDesc = {};
	D3D11_TEXTURE2D_DESC textureDesc[2] = {};
	ID3D11SamplerState* samplerState = NULL;
	ID3D11Texture2D* texture[2] = {NULL, NULL};
	ID3D11ShaderResourceView* textureView[2] = {NULL, NULL};
	ID3D11SamplerState* noiseSamplerState = NULL;
	ID3D11ShaderResourceView* noiseTextureView = NULL;
	ID3D11Buffer* constantBuffer = NULL;

	RtvCacheEntry rtvCache[16] = {};
	int rtvCacheCount = 0;

	int lastLutSize[2] = {-1, -1};
	bool lastIsHdr[2] = {false, false};

	ID3D11ShaderResourceView* lutTextureViews[64] = {};
};

std::map<ID3D11Device*, DeviceContext*> g_deviceContexts;
std::mutex g_deviceMutex;
#if DEBUG_MODE == true
void print_error(const char* prefix_message)
{
	DWORD errorCode = GetLastError();
	LPSTR errorMessage = nullptr;
	FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	               nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&errorMessage, 0, nullptr);

	char message_buf[100];
	sprintf(message_buf, "%s: %s - error code: %u", prefix_message, errorMessage, errorCode);
	log_to_file(message_buf);
	return;
}
#endif


unsigned int lut_index(const unsigned int b, const unsigned int g, const unsigned int r, const unsigned int c,
                       const unsigned int lut_size)
{
	return lut_size * lut_size * 4 * b + lut_size * 4 * g + 4 * r + c;
}

#define LUT_ACCESS_INDEX(lut, b, g, r, c, lut_size) (*((float*)(lut) + lut_index(b, g, r, c, lut_size)))


void* get_relative_address(void* instruction_address, int offset, int instruction_size)
{
	int relative_offset = *(int*)((unsigned char*)instruction_address + offset);

	return (unsigned char*)instruction_address + instruction_size + relative_offset;
}


const unsigned char COverlayContext_Present_bytes[] = {
	0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b, 0xb1, 0x20,
	0x2c, 0x00, 0x00, 0x45, 0x8b, 0xd0, 0x48, 0x8b, 0xfa, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xf6, 0x0f, 0x85
};
const int IOverlaySwapChain_IDXGISwapChain_offset = -0x118;

const unsigned char COverlayContext_IsCandidateDirectFlipCompatible_bytes[] = {
	0x48, 0x89, 0x7c, 0x24, 0x20, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8b, 0xec, 0x48, 0x83,
	0xec, 0x40
};
const unsigned char COverlayContext_OverlaysEnabled_bytes[] = {
	0x75, 0x04, 0x32, 0xc0, 0xc3, 0xcc, 0x83, 0x79, 0x30, 0x01, 0x0f, 0x97, 0xc0, 0xc3
};

const int COverlayContext_DeviceClipBox_offset = -0x120;

const int IOverlaySwapChain_HardwareProtected_offset = -0xbc;


const unsigned char COverlayContext_Present_bytes_w11[] = {
	0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05,
	'?', '?', '?', '?', 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x78, 0x48
};
const int IOverlaySwapChain_IDXGISwapChain_offset_w11 = 0xE0;


const unsigned char COverlayContext_IsCandidateDirectFlipCompatible_bytes_w11[] = {
	0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC,
	0x68, 0x48,
};


const unsigned char COverlayContext_OverlaysEnabled_bytes_w11[] = {
	0x83, 0x3D, '?', '?', '?', '?', '?', 0x75, 0x04
};

int COverlayContext_DeviceClipBox_offset_w11 = 0x4EE8;

const int IOverlaySwapChain_HardwareProtected_offset_w11 = -0x144;



const unsigned char COverlayContext_Present_bytes_w11_24h2[] = {
	0x4C, 0x8B, 0xDC, 0x56, 0x41, 0x56
};

const int IOverlaySwapChain_IDXGISwapChain_offset_w11_24h2 = 0x108;

const unsigned char COverlayContext_IsCandidateDirectFlipCompatible_bytes_w11_24h2[] = {
	0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, '?', 0x48, 0x89, 0x68, '?', 0x48, 0x89, 0x70, '?', 0x48, 0x89, 0x78, '?', 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xDB
};

const unsigned char COverlayContext_OverlaysEnabled_bytes_relative_w11_24h2[] = {
	0xE8, '?', '?', '?', '?', 0x84, 0xC0, 0xB8, 0x04, 0x00, 0x00, 0x00
};

int COverlayContext_DeviceClipBox_offset_w11_24h2 = 0x53E8;

const int IOverlaySwapChain_HardwareProtected_offset_w11_24h2 = 0x64;



const unsigned char COverlayContext_Present_bytes_w11_25h2[] = {
	0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C,
	0x24, 0xF9, 0x48, 0x81, 0xEC, 0xF8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05,
	'?', '?', '?', '?', 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45, 0xEF, 0x4C, 0x8B, 0x65, '?', 0x48, 0x8B, 0xD9
};


const unsigned char COverlayContext_IsCandidateDirectFlipCompatible_bytes_w11_25h2[] = {
	0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48,
	0x89, 0x78, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xDB
};


const unsigned char COverlayContext_OverlaysEnabled_bytes_w11_25h2[] = {
	0x83, 0x3D, '?', '?', '?', '?', 0x05, 0x74, 0x09, 0x83, 0x79, 0x28, 0x01, 0x0F, 0x97, 0xC0, 0xC3
};


const unsigned char CWindowContext_IsCandidateDirectFlipCompatible_bytes_w11_25h2[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xD9, 0x48, 0x8B, 0xF2, 0x4C, 0x8B, 0x01, 0x48, 0x8B, 0xF9
};


const unsigned char CCompSwapChain_IsCandidateDirectFlipCompatible_bytes_w11_25h2[] = {
	0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xDB, 0x41, 0x8B, 0xF0
};


const unsigned char CCompVisual_IsCandidateForPromotion_bytes_w11_25h2[] = {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x01, 0x41, 0x8B, 0xD1, 0x48, 0x8B, 0xF1
};


int COverlayContext_DeviceClipBox_offset_w11_25h2 = 0x7698;

const int IOverlaySwapChain_HardwareProtected_offset_w11_25h2 = 0x4C;


const int IOverlaySwapChain_GetSwapChain_vtable_offset_w11_25h2 = 0x108;


bool isWindows11 = false;
bool isWindows11_23h2 = false;
bool isWindows11_24h2 = false;
bool isWindows11_25h2 = false;


static int* g_pOverlayTestMode = NULL;

bool aob_match_inverse(const void* buf1, const void* mask, const int buf_len)
{
	for (int i = 0; i < buf_len; ++i)
	{
		if (((unsigned char*)buf1)[i] != ((unsigned char*)mask)[i] && ((unsigned char*)mask)[i] != '?')
		{
			return true;
		}
	}
	return false;
}

char shaders[] = R"(
    struct VS_INPUT {
	float2 pos : POSITION;
	float2 tex : TEXCOORD;
};

struct VS_OUTPUT {
	float4 pos : SV_POSITION;
	float2 tex : TEXCOORD;
};

Texture2D backBufferTex : register(t0);
Texture3D lutTex : register(t1);
SamplerState smp : register(s0);

Texture2D noiseTex : register(t2);
SamplerState noiseSmp : register(s1);

cbuffer ConstantBuffer : register(b0) {
    int lutSize;
    bool hdr;
};

static float3x3 scrgb_to_bt2100 = {
2939026994.L / 585553224375.L, 9255011753.L / 3513319346250.L,   173911579.L / 501902763750.L,
  76515593.L / 138420033750.L, 6109575001.L / 830520202500.L,    75493061.L / 830520202500.L,
  12225392.L / 93230009375.L, 1772384008.L / 2517210253125.L, 18035212433.L / 2517210253125.L,
};

static float3x3 bt2100_to_scrgb = {
 348196442125.L / 1677558947.L, -123225331250.L / 1677558947.L,  -15276242500.L / 1677558947.L,
-579752563250.L / 37238079773.L, 5273377093000.L / 37238079773.L,  -38864558125.L / 37238079773.L,
 -12183628000.L / 5369968309.L, -472592308000.L / 37589778163.L, 5256599974375.L / 37589778163.L,
};

static float m1 = 1305 / 8192.;
static float m2 = 2523 / 32.;
static float c1 = 107 / 128.;
static float c2 = 2413 / 128.;
static float c3 = 2392 / 128.;

float3 SampleLut(float3 index) {
	float3 tex = (index + 0.5) / lutSize;
	return lutTex.Sample(smp, tex).rgb;
}


void barycentricWeight(float3 r, out float4 bary, out int3 vert2, out int3 vert3) {
	vert2 = int3(0, 0, 0); vert3 = int3(1, 1, 1);
	int3 c = r.xyz >= r.yzx;
	bool c_xy = c.x; bool c_yz = c.y; bool c_zx = c.z;
	bool c_yx = !c.x; bool c_zy = !c.y; bool c_xz = !c.z;
	bool cond;  float3 s = float3(0, 0, 0);
#define ORDER(X, Y, Z)                   \
            cond = c_ ## X ## Y && c_ ## Y ## Z; \
            s = cond ? r.X ## Y ## Z : s;        \
            vert2.X = cond ? 1 : vert2.X;        \
            vert3.Z = cond ? 0 : vert3.Z;
	ORDER(x, y, z)   ORDER(x, z, y)   ORDER(z, x, y)
		ORDER(z, y, x)   ORDER(y, z, x)   ORDER(y, x, z)
		bary = float4(1 - s.x, s.z, s.x - s.y, s.y - s.z);
}

float3 LutTransformTetrahedral(float3 rgb) {
	float3 lutIndex = rgb * (lutSize - 1);
	float4 bary; int3 vert2; int3 vert3;
	barycentricWeight(frac(lutIndex), bary, vert2, vert3);

	float3 base = floor(lutIndex);
	return bary.x * SampleLut(base) +
		bary.y * SampleLut(base + 1) +
		bary.z * SampleLut(base + vert2) +
		bary.w * SampleLut(base + vert3);
}

float3 pq_eotf(float3 e) {
	return pow(max((pow(e, 1 / m2) - c1), 0) / (c2 - c3 * pow(e, 1 / m2)), 1 / m1);
}

float3 pq_inv_eotf(float3 y) {
	return pow((c1 + c2 * pow(y, m1)) / (1 + c3 * pow(y, m1)), m2);
}

float3 OrderedDither(float3 rgb, float2 pos) {
	float3 low = floor(rgb * 255) / 255;
	float3 high = low + 1.0 / 255;

	float3 rgb_linear = pow(rgb,)" STRINGIFY(DITHER_GAMMA) R"();
	float3 low_linear = pow(low,)" STRINGIFY(DITHER_GAMMA) R"();
	float3 high_linear = pow(high,)" STRINGIFY(DITHER_GAMMA) R"();

	float noise = noiseTex.Sample(noiseSmp, pos / )" STRINGIFY(NOISE_SIZE) R"().x;
	float3 threshold = lerp(low_linear, high_linear, noise);

	return lerp(low, high, rgb_linear > threshold);
}

VS_OUTPUT VS(VS_INPUT input) {
	VS_OUTPUT output;
	output.pos = float4(input.pos, 0, 1);
	output.tex = input.tex;
	return output;
}

float4 PS(VS_OUTPUT input) : SV_TARGET{
	float3 sample = backBufferTex.Sample(smp, input.tex).rgb;

	if (hdr) {
		float3 hdr10_sample = pq_inv_eotf(saturate(mul(scrgb_to_bt2100, sample)));

		float3 hdr10_res = LutTransformTetrahedral(hdr10_sample);

		float3 scrgb_res = mul(bt2100_to_scrgb, pq_eotf(hdr10_res));

		return float4(scrgb_res, 1);
	}
	else {
		float3 res = LutTransformTetrahedral(sample);

		res = OrderedDither(res, input.pos.xy);

		return float4(res, 1);
	}
}
)";



struct lutData
{
	int left;
	int top;
	int size;
	bool isHdr;
	
	float* rawLut;
};

void DrawRectangle(struct tagRECT* rect, int index, DeviceContext* ctx)
{
	float width = ctx->backBufferDesc.Width;
	float height = ctx->backBufferDesc.Height;

	float screenLeft = rect->left / width;
	float screenTop = rect->top / height;
	float screenRight = rect->right / width;
	float screenBottom = rect->bottom / height;

	float left = screenLeft * 2 - 1;
	float top = screenTop * -2 + 1;
	float right = screenRight * 2 - 1;
	float bottom = screenBottom * -2 + 1;

	width = ctx->textureDesc[index].Width;
	height = ctx->textureDesc[index].Height;
	float texLeft = rect->left / width;
	float texTop = rect->top / height;
	float texRight = rect->right / width;
	float texBottom = rect->bottom / height;

	float vertexData[] = {
		left, bottom, texLeft, texBottom,
		left, top, texLeft, texTop,
		right, bottom, texRight, texBottom,
		right, top, texRight, texTop
	};

	D3D11_MAPPED_SUBRESOURCE resource;
	EXECUTE_WITH_LOG(ctx->deviceContext->Map(ctx->vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource))
	memcpy(resource.pData, vertexData, ctx->stride * ctx->numVerts);
	ctx->deviceContext->Unmap(ctx->vertexBuffer, 0);

	ctx->deviceContext->IASetVertexBuffers(0, 1, &ctx->vertexBuffer, &ctx->stride, &ctx->offset);

	ctx->deviceContext->Draw(ctx->numVerts, 0);
}

int numLuts;

lutData* luts;

bool ParseLUT(lutData* lut, char* filename)
{
	FILE* file = fopen(filename, "r");
	if (file == NULL) return false;

	char line[256];
	unsigned int lutSize;

	while (1)
	{
		if (!fgets(line, sizeof(line), file))
		{
			fclose(file);
			return false;
		}
		if (sscanf(line, "LUT_3D_SIZE%d", &lutSize) == 1)
		{
			break;
		}
	}

	float* rawLut = (float*)malloc(lutSize * lutSize * lutSize * 4 * sizeof(float));


	for (int b = 0; b < lutSize; b++)
	{
		for (int g = 0; g < lutSize; g++)
		{
			for (int r = 0; r < lutSize; r++)
			{
				while (1)
				{
					if (!fgets(line, sizeof(line), file))
					{
						fclose(file);

						return false;
					}
					
					if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
						continue;
					
					if ((line[0] >= '0' && line[0] <= '9') || line[0] == '-' || line[0] == '.')
					{
						float red, green, blue;

						if (sscanf(line, "%f %f %f", &red, &green, &blue) != 3)
						{
							fclose(file);

							return false;
						}
						LUT_ACCESS_INDEX(rawLut, b, g, r, 0, lutSize) = red;
						LUT_ACCESS_INDEX(rawLut, b, g, r, 1, lutSize) = green;
						LUT_ACCESS_INDEX(rawLut, b, g, r, 2, lutSize) = blue;
						LUT_ACCESS_INDEX(rawLut, b, g, r, 3, lutSize) = 1;

						break;
					}
				}
			}
		}
	}
	fclose(file);
	lut->size = lutSize;
	lut->rawLut = rawLut;
	return true;
}

bool AddLUTs(char* folder)
{
	WIN32_FIND_DATAA findData;

	char path[MAX_PATH];
	strcpy(path, folder);
	strcat(path, "\\*");
	HANDLE hFind = FindFirstFileA(path, &findData);
	if (hFind == INVALID_HANDLE_VALUE) return false;
	do
	{
		if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			char filePath[MAX_PATH];
			char* fileName = findData.cFileName;

			strcpy(filePath, folder);
			strcat(filePath, "\\");
			strcat(filePath, fileName);

			luts = (lutData*)RESIZE(luts, numLuts + 1)
			lutData* lut = &luts[numLuts];
			if (sscanf(findData.cFileName, "%d_%d", &lut->left, &lut->top) == 2)
			{
				lut->isHdr = strstr(fileName, "hdr") != NULL;
				if (!ParseLUT(lut, filePath))
				{
					LOG_ONLY_ONCE("LUT could not be parsed")
					FindClose(hFind);
					return false;
				}
				numLuts++;
			}
		}
	}
	while (FindNextFileA(hFind, &findData) != 0);
	FindClose(hFind);
	return true;
}

int numLutTargets;
void** lutTargets;



static void* g_primaryHdrContext = NULL;

bool IsLUTActive(void* target)
{
	for (int i = 0; i < numLutTargets; i++)
	{
		if (lutTargets[i] == target)
		{
			return true;
		}
	}
	return false;
}

void SetLUTActive(void* target)
{
	if (!IsLUTActive(target))
	{
		lutTargets = (void**)RESIZE(lutTargets, numLutTargets + 1)
		lutTargets[numLutTargets++] = target;
	}
}

void UnsetLUTActive(void* target)
{
	for (int i = 0; i < numLutTargets; i++)
	{
		if (lutTargets[i] == target)
		{
			lutTargets[i] = lutTargets[--numLutTargets];
			lutTargets = (void**)RESIZE(lutTargets, numLutTargets)
			return;
		}
	}
}

lutData* GetLUTDataFromCOverlayContext(void* context, bool hdr, int* out_index)
{
	if (!context) return NULL;

	
	static void* last_context = NULL;
	static lutData* last_lut = NULL;
	static bool last_hdr = false;
	static int last_index = -1;

	if (context == last_context && last_lut != NULL && last_hdr == hdr)
	{
		if (out_index) *out_index = last_index;
		return last_lut;
	}

	
	if (g_pOverlayTestMode != NULL)
	{
		*g_pOverlayTestMode = 5;
	}

	
	int left = 0, top = 0;
	bool gotCoords = false;

	__try
	{
		if (isWindows11_25h2)
		{
			void* realObj = *(void**)context;
			if (realObj)
			{
				int* rect = (int*)((unsigned char*)realObj + 0x4D0);
				left = rect[0];
				top = rect[1];
				gotCoords = true;
			}
		}
		else if (isWindows11_24h2)
		{
			unsigned char* base = (unsigned char*)*(void**)context;
			if (base)
			{
				float* rect = (float*)(base + COverlayContext_DeviceClipBox_offset_w11_24h2);
				left = (int)rect[0];
				top = (int)rect[1];

				if (left == 0 && top == 0 && (rect[2] != 0 || rect[3] != 0))
				{
					left = (int)rect[2];
					top = (int)rect[3];
				}
				gotCoords = true;
			}
		}
		else if (isWindows11)
		{
			unsigned char* base = (unsigned char*)*(void**)context;
			if (base)
			{
				float* rect = (float*)(base + COverlayContext_DeviceClipBox_offset_w11);
				left = (int)rect[0];
				top = (int)rect[1];
				
				gotCoords = true;
			}
		}
		else
		{
			unsigned char* base = (unsigned char*)*(void**)context;
			if (base)
			{
				float* rect = (float*)(base + COverlayContext_DeviceClipBox_offset);
				left = (int)rect[0];
				top = (int)rect[1];
				gotCoords = true;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		gotCoords = false;
	}

	char message_buf[128];
	sprintf(message_buf, "GetLUT: left=%d, top=%d, hdr=%d, gotCoords=%d", left, top, (int)hdr, (int)gotCoords);
	LOG_ONLY_ONCE(message_buf)

	
	for (int i = 0; i < numLuts; i++)
	{
		if (luts[i].left == left && luts[i].top == top && luts[i].isHdr == hdr)
		{
			last_context = context;
			last_lut = &luts[i];
			last_hdr = hdr;
			last_index = i;
			if (out_index) *out_index = i;
			return &luts[i];
		}
	}

	
	if ((isWindows11_25h2 || isWindows11_24h2 || isWindows11) && g_primaryHdrContext == context)
	{
		for (int i = 0; i < numLuts; i++)
		{
			if (luts[i].left == left && luts[i].top == top && luts[i].isHdr != hdr)
			{
				last_context = context;
				last_lut = &luts[i];
				last_hdr = hdr;
				last_index = i;
				if (out_index) *out_index = i;
				return &luts[i];
			}
		}
	}

	
	if (numLuts == 1 && luts[0].isHdr == hdr)
	{
		last_context = context;
		last_lut = &luts[0];
		last_hdr = hdr;
		last_index = 0;
		if (out_index) *out_index = 0;
		return &luts[0];
	}

	
	last_context = context;
	last_lut = NULL;
	last_hdr = hdr;
	last_index = -1;
	return NULL;
}

void InitializeDeviceContext(ID3D11Device* inputDevice, DeviceContext* ctx)
{
	try
	{
		ctx->device = inputDevice;
		ctx->device->AddRef();
		LOG_ONLY_ONCE("Device successfully gathered")
		LOG_ADDRESS("The device address is: ", ctx->device)

		ctx->device->GetImmediateContext(&ctx->deviceContext);
		LOG_ONLY_ONCE("Got context after device")
		LOG_ADDRESS("The Device context is located at address: ", ctx->deviceContext)
		{
			ID3DBlob* vsBlob;
			ID3DBlob* compile_error_interface;
			LOG_ONLY_ONCE(("Trying to compile vshader with this code:\n" + std::string(shaders)).c_str())
			EXECUTE_D3DCOMPILE_WITH_LOG(
				D3DCompile(shaders, sizeof shaders, NULL, NULL, NULL, "VS", "vs_5_0", 0, 0, &vsBlob, &
					compile_error_interface), compile_error_interface)


			LOG_ONLY_ONCE("Vertex shader compiled successfully")
			EXECUTE_WITH_LOG(ctx->device->CreateVertexShader(vsBlob->GetBufferPointer(),
				vsBlob->GetBufferSize(), NULL, &ctx->vertexShader))


			LOG_ONLY_ONCE("Vertex shader created successfully")
			D3D11_INPUT_ELEMENT_DESC inputElementDesc[] =
			{
				{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{
					"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT,
					D3D11_INPUT_PER_VERTEX_DATA, 0
				}
			};
			EXECUTE_WITH_LOG(ctx->device->CreateInputLayout(inputElementDesc, ARRAYSIZE(inputElementDesc),
				vsBlob->GetBufferPointer(),
				vsBlob->GetBufferSize(), &ctx->inputLayout))

			vsBlob->Release();
		}
		{
			ID3DBlob* psBlob;
			ID3DBlob* compile_error_interface;
			EXECUTE_D3DCOMPILE_WITH_LOG(
				D3DCompile(shaders, sizeof shaders, NULL, NULL, NULL, "PS", "ps_5_0", 0, 0, &psBlob, &
					compile_error_interface), compile_error_interface)

			LOG_ONLY_ONCE("Pixel shader compiled successfully")
			ctx->device->CreatePixelShader(psBlob->GetBufferPointer(),
			                          psBlob->GetBufferSize(), NULL, &ctx->pixelShader);
			psBlob->Release();
		}
		{
			ctx->stride = 4 * sizeof(float);
			ctx->numVerts = 4;
			ctx->offset = 0;

			D3D11_BUFFER_DESC vertexBufferDesc = {};
			vertexBufferDesc.ByteWidth = ctx->stride * ctx->numVerts;
			vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
			vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			EXECUTE_WITH_LOG(ctx->device->CreateBuffer(&vertexBufferDesc, NULL, &ctx->vertexBuffer))
		}
		{
			D3D11_SAMPLER_DESC samplerDesc = {};
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

			EXECUTE_WITH_LOG(ctx->device->CreateSamplerState(&samplerDesc, &ctx->samplerState))
		}
		for (int i = 0; i < numLuts; i++)
		{
			lutData* lut = &luts[i];
            
            if (!lut->rawLut) continue; 

			D3D11_TEXTURE3D_DESC desc = {};
			desc.Width = lut->size;
			desc.Height = lut->size;
			desc.Depth = lut->size;
			desc.MipLevels = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

			D3D11_SUBRESOURCE_DATA initData;
			initData.pSysMem = lut->rawLut;
			initData.SysMemPitch = lut->size * 4 * sizeof(float);
			initData.SysMemSlicePitch = lut->size * lut->size * 4 * sizeof(float);

			ID3D11Texture3D* tex;
			EXECUTE_WITH_LOG(ctx->device->CreateTexture3D(&desc, &initData, &tex))
			EXECUTE_WITH_LOG(ctx->device->CreateShaderResourceView((ID3D11Resource*)tex, NULL, &ctx->lutTextureViews[i]))
			tex->Release();
			
		}
		{
			D3D11_SAMPLER_DESC samplerDesc = {};
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
			samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

			EXECUTE_WITH_LOG(ctx->device->CreateSamplerState(&samplerDesc, &ctx->noiseSamplerState))
		}
		{
			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = NOISE_SIZE;
			desc.Height = NOISE_SIZE;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R32_FLOAT;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

			float noise[NOISE_SIZE][NOISE_SIZE];

			for (int i = 0; i < NOISE_SIZE; i++)
			{
				for (int j = 0; j < NOISE_SIZE; j++)
				{
					noise[i][j] = (noiseBytes[i][j] + 0.5) / 256;
				}
			}

			D3D11_SUBRESOURCE_DATA initData;
			initData.pSysMem = noise;
			initData.SysMemPitch = sizeof(noise[0]);

			ID3D11Texture2D* tex;
			EXECUTE_WITH_LOG(ctx->device->CreateTexture2D(&desc, &initData, &tex))
			EXECUTE_WITH_LOG(ctx->device->CreateShaderResourceView((ID3D11Resource*)tex, NULL, &ctx->noiseTextureView))
			tex->Release();
		}
		{
			D3D11_BUFFER_DESC constantBufferDesc = {};
			constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			constantBufferDesc.ByteWidth = 16;
			constantBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
			constantBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			EXECUTE_WITH_LOG(ctx->device->CreateBuffer(&constantBufferDesc, NULL, &ctx->constantBuffer))
			LOG_ONLY_ONCE("Final buffer created in InitializeStuff")
		}
	}
	catch (std::exception& ex)
	{
		std::stringstream ex_message;
		ex_message << "Exception caught at line " << __LINE__ << ": " << ex.what() << std::endl;
		LOG_ONLY_ONCE(ex_message.str().c_str())
		throw;
	}
	catch (...)
	{
		std::stringstream ex_message;
		ex_message << "Exception caught at line " << __LINE__ << ": " << std::endl;
		LOG_ONLY_ONCE(ex_message.str().c_str())
		throw;
	}
}

DeviceContext* GetOrCreateDeviceContext(ID3D11Device* dev)
{
    std::lock_guard<std::mutex> lock(g_deviceMutex);
    auto it = g_deviceContexts.find(dev);
    if (it != g_deviceContexts.end()) return it->second;
    DeviceContext* ctx = new DeviceContext();
    InitializeDeviceContext(dev, ctx);
    g_deviceContexts[dev] = ctx;
    return ctx;
}

void UninitializeStuff()
{
	std::lock_guard<std::mutex> lock(g_deviceMutex);
	for (auto& pair : g_deviceContexts)
	{
		DeviceContext* ctx = pair.second;
		RELEASE_IF_NOT_NULL(ctx->device)
		RELEASE_IF_NOT_NULL(ctx->deviceContext)
		RELEASE_IF_NOT_NULL(ctx->vertexShader)
		RELEASE_IF_NOT_NULL(ctx->pixelShader)
		RELEASE_IF_NOT_NULL(ctx->inputLayout)
		RELEASE_IF_NOT_NULL(ctx->vertexBuffer)
		RELEASE_IF_NOT_NULL(ctx->samplerState)
		for (int i = 0; i < 2; i++)
		{
			RELEASE_IF_NOT_NULL(ctx->texture[i])
			RELEASE_IF_NOT_NULL(ctx->textureView[i])
		}
		for (int i = 0; i < ctx->rtvCacheCount; i++)
		{
			RELEASE_IF_NOT_NULL(ctx->rtvCache[i].rtv)
		}
		ctx->rtvCacheCount = 0;
		RELEASE_IF_NOT_NULL(ctx->noiseSamplerState)
		RELEASE_IF_NOT_NULL(ctx->noiseTextureView)
		RELEASE_IF_NOT_NULL(ctx->constantBuffer)
		for (int i = 0; i < numLuts; i++)
		{
			RELEASE_IF_NOT_NULL(ctx->lutTextureViews[i])
		}
		delete ctx;
	}
	g_deviceContexts.clear();

	for (int i = 0; i < numLuts; i++)
	{
		if (luts[i].rawLut) {
			free(luts[i].rawLut);
			luts[i].rawLut = NULL;
		}
	}
	free(luts);
	free(lutTargets);
}


bool RenderLUT(void* cOverlayContext, ID3D11Texture2D* backBuffer, struct tagRECT* rects, int numRects, DeviceContext* ctx)
{
	ID3D11RenderTargetView* renderTargetView;

	D3D11_TEXTURE2D_DESC newBackBufferDesc;
	backBuffer->GetDesc(&newBackBufferDesc);

	int index = -1;
	if (newBackBufferDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
	    newBackBufferDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
	    newBackBufferDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
	    newBackBufferDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
	    newBackBufferDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM)
	{
		index = 0;
	}
	else if (newBackBufferDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || newBackBufferDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
	{
		index = 1;

		if (isWindows11_25h2 && g_primaryHdrContext == NULL)
		{
			g_primaryHdrContext = cOverlayContext;
		}
	}

	int lut_index = 0;
	lutData* lut;
	if (index == -1 || !(lut = GetLUTDataFromCOverlayContext(cOverlayContext, index == 1, &lut_index)))
	{
		char message[256];
		sprintf(message, "LUT not found for index %d", index);
		LOG_ONLY_ONCE(message)
		return false;
	}

	D3D11_TEXTURE2D_DESC oldTextureDesc = ctx->textureDesc[index];
	if (newBackBufferDesc.Width > oldTextureDesc.Width || newBackBufferDesc.Height > oldTextureDesc.Height)
	{
		if (ctx->texture[index] != NULL)
		{
			ctx->texture[index]->Release();
			ctx->textureView[index]->Release();
		}

		UINT newWidth = max(newBackBufferDesc.Width, oldTextureDesc.Width);
		UINT newHeight = max(newBackBufferDesc.Height, oldTextureDesc.Height);

		D3D11_TEXTURE2D_DESC newTextureDesc;

		newTextureDesc = newBackBufferDesc;
		newTextureDesc.Width = newWidth;
		newTextureDesc.Height = newHeight;
		newTextureDesc.Usage = D3D11_USAGE_DEFAULT;
		newTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		newTextureDesc.CPUAccessFlags = 0;
		newTextureDesc.MiscFlags = 0;

		ctx->textureDesc[index] = newTextureDesc;

		EXECUTE_WITH_LOG(ctx->device->CreateTexture2D(&ctx->textureDesc[index], NULL, &ctx->texture[index]))
		EXECUTE_WITH_LOG(
			ctx->device->CreateShaderResourceView((ID3D11Resource*)ctx->texture[index], NULL, &ctx->textureView[index]))
	}

	ctx->backBufferDesc = newBackBufferDesc;
	renderTargetView = NULL;
	for (int i = 0; i < ctx->rtvCacheCount; i++)
	{
		if (ctx->rtvCache[i].texture == backBuffer)
		{
			renderTargetView = ctx->rtvCache[i].rtv;
			renderTargetView->AddRef();
			break;
		}
	}

	if (renderTargetView == NULL)
	{
		if (ctx->rtvCacheCount >= 16)
		{
			RELEASE_IF_NOT_NULL(ctx->rtvCache[0].rtv)
			for (int i = 0; i < 15; i++) ctx->rtvCache[i] = ctx->rtvCache[i+1];
			ctx->rtvCacheCount--;
		}
		EXECUTE_WITH_LOG(ctx->device->CreateRenderTargetView((ID3D11Resource*)backBuffer, NULL, &ctx->rtvCache[ctx->rtvCacheCount].rtv))
		ctx->rtvCache[ctx->rtvCacheCount].texture = backBuffer;
		renderTargetView = ctx->rtvCache[ctx->rtvCacheCount].rtv;
		renderTargetView->AddRef();
		ctx->rtvCacheCount++;
	}

	const D3D11_VIEWPORT d3d11_viewport(0, 0, (float)ctx->backBufferDesc.Width, (float)ctx->backBufferDesc.Height, 0.0f, 1.0f);
	ctx->deviceContext->RSSetViewports(1, &d3d11_viewport);

	ctx->deviceContext->OMSetRenderTargets(1, &renderTargetView, NULL);
	renderTargetView->Release();

	ctx->deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	ctx->deviceContext->IASetInputLayout(ctx->inputLayout);
	ctx->deviceContext->VSSetShader(ctx->vertexShader, NULL, 0);
	ctx->deviceContext->PSSetShader(ctx->pixelShader, NULL, 0);
	ctx->deviceContext->PSSetSamplers(0, 1, &ctx->samplerState);
	ctx->deviceContext->PSSetSamplers(1, 1, &ctx->noiseSamplerState);

	ctx->deviceContext->PSSetShaderResources(0, 1, &ctx->textureView[index]);
	ctx->deviceContext->PSSetShaderResources(1, 1, &ctx->lutTextureViews[lut_index]);
	ctx->deviceContext->PSSetShaderResources(2, 1, &ctx->noiseTextureView);

	static int lastLutSize[2] = {-1, -1};
	static bool lastIsHdr[2] = {false, false};

	if (lastLutSize[index] != lut->size || lastIsHdr[index] != (index == 1))
	{
		int constantData[4] = {lut->size, index == 1};
		D3D11_MAPPED_SUBRESOURCE resource;
		EXECUTE_WITH_LOG(ctx->deviceContext->Map((ID3D11Resource*)ctx->constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource))
		memcpy(resource.pData, constantData, sizeof(constantData));
		ctx->deviceContext->Unmap((ID3D11Resource*)ctx->constantBuffer, 0);
		
		lastLutSize[index] = lut->size;
		lastIsHdr[index] = (index == 1);
	}

	ctx->deviceContext->PSSetConstantBuffers(0, 1, &ctx->constantBuffer);

	
	
	D3D11_BOX sourceBox;
	sourceBox.left = 0;
	sourceBox.top = 0;
	sourceBox.front = 0;
	sourceBox.right = ctx->backBufferDesc.Width;
	sourceBox.bottom = ctx->backBufferDesc.Height;
	sourceBox.back = 1;
	ctx->deviceContext->CopySubresourceRegion((ID3D11Resource*)ctx->texture[index], 0, 0, 0, 0, (ID3D11Resource*)backBuffer, 0, &sourceBox);

	for (int i = 0; i < numRects; i++)
	{
		DrawRectangle(&rects[i], index, ctx);
	}

	return true;
}

bool ApplyLUT(void* cOverlayContext, IDXGISwapChain* swapChain, struct tagRECT* rects, int numRects)
{
	try
	{
		ID3D11Device* dev;
		EXECUTE_WITH_LOG(swapChain->GetDevice(IID_ID3D11Device, (void**)&dev))
		DeviceContext* ctx = GetOrCreateDeviceContext(dev);

		LOG_ONLY_ONCE("Init done, continuing with LUT application")

		ID3D11Texture2D* backBuffer;
		EXECUTE_WITH_LOG(swapChain->GetBuffer(0, IID_ID3D11Texture2D, (void**)&backBuffer))

		bool result = RenderLUT(cOverlayContext, backBuffer, rects, numRects, ctx);
		backBuffer->Release();
		dev->Release();
		return result;
	}
	catch (std::exception& ex)
	{
		std::stringstream ex_message;
		ex_message << "Exception caught at line " << __LINE__ << ": " << ex.what() << std::endl;
		LOG_ONLY_ONCE(ex_message.str().c_str())
		return false;
	}
	catch (...)
	{
		std::stringstream ex_message;
		ex_message << "Exception caught at line " << __LINE__ << std::endl;
		LOG_ONLY_ONCE(ex_message.str().c_str())
		return false;
	}
}


bool ApplyLUTDirect(void* cOverlayContext, ID3D11Texture2D* backBuffer, struct tagRECT* rects, int numRects)
{
	try
	{
		ID3D11Device* dev;
		backBuffer->GetDevice(&dev);
		DeviceContext* ctx = GetOrCreateDeviceContext(dev);
		bool result = RenderLUT(cOverlayContext, backBuffer, rects, numRects, ctx);
		dev->Release();
		return result;
	}
	catch (std::exception& ex)
	{
		std::stringstream ex_message;
		ex_message << "Exception caught at line " << __LINE__ << ": " << ex.what() << std::endl;
		LOG_ONLY_ONCE(ex_message.str().c_str())
		return false;
	}
	catch (...)
	{
		std::stringstream ex_message;
		ex_message << "Exception caught at line " << __LINE__ << std::endl;
		LOG_ONLY_ONCE(ex_message.str().c_str())
		return false;
	}
}

typedef struct rectVec
{
	struct tagRECT* start;
	struct tagRECT* end;
	struct tagRECT* cap;
} rectVec;

typedef long (COverlayContext_Present_t)(void*, void*, unsigned int, rectVec*, unsigned int, bool);
typedef long long (COverlayContext_Present_24h2_t)(void*, void*, unsigned int, rectVec*, int, void*, bool);



static ID3D11Texture2D* GetBackBuffer_25H2(void* overlaySwapChain)
{
	__try
	{
		if (!overlaySwapChain) return NULL;

		void** vt = *(void***)overlaySwapChain;
		if (!vt) return NULL;

		typedef void* (__fastcall *VirtFunc)(void*);

		VirtFunc func1 = (VirtFunc)vt[24];
		if (!func1) return NULL;

		void* r1 = func1(overlaySwapChain);
		if (!r1) return NULL;

		void** vt2 = *(void***)r1;
		if (!vt2) return NULL;

		VirtFunc func2 = (VirtFunc)vt2[19];
		if (!func2) return NULL;

		void* r2 = func2(r1);
		if (!r2) return NULL;

		ID3D11Texture2D* tex = NULL;
		HRESULT hr = ((IUnknown*)r2)->QueryInterface(IID_ID3D11Texture2D, (void**)&tex);
		if (FAILED(hr) || !tex) return NULL;

		LOG_ONLY_ONCE("25H2: Got texture via overlaySwapChain->vt[24]()->vt2[19]()->QI")
		return tex;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return NULL;
	}
}

COverlayContext_Present_t* COverlayContext_Present_orig = NULL;
COverlayContext_Present_t* COverlayContext_Present_real_orig = NULL;

COverlayContext_Present_24h2_t* COverlayContext_Present_orig_24h2 = NULL;
COverlayContext_Present_24h2_t* COverlayContext_Present_real_orig_24h2 = NULL;

long long COverlayContext_Present_hook_24h2(void* self, void* overlaySwapChain, unsigned int a3, rectVec* rectVec,
	int a5, void* a6, bool a7)
{
	if (_ReturnAddress() < (void*)COverlayContext_Present_real_orig_24h2 || isWindows11_24h2 || isWindows11_25h2)
	{
			LOG_ONLY_ONCE("I am inside COverlayContext::Present hook inside the main if condition")
			std::stringstream overlay_swapchain_message;
			overlay_swapchain_message << "OverlaySwapChain address: 0x" << std::hex << overlaySwapChain
				<< " -- windows 11 25h2: " << isWindows11_25h2
				<< " -- windows 11 24h2: " << isWindows11_24h2
				<< " -- " << "windows 11: " << isWindows11;
			LOG_ONLY_ONCE(overlay_swapchain_message.str().c_str())

			if (isWindows11_25h2)
			{
				bool success = false;

				ID3D11Texture2D* backBuffer = GetBackBuffer_25H2(overlaySwapChain);
				if (backBuffer)
				{
					if (ApplyLUTDirect(self, backBuffer, rectVec->start, rectVec->end - rectVec->start))
					{
						SetLUTActive(self);
						success = true;
					}
					backBuffer->Release();
				}

				if (!success)
				{
					if (g_DynamicSwapChainOffset != -1)
					{
						IDXGISwapChain* swapChain = *(IDXGISwapChain**)((unsigned char*)overlaySwapChain + g_DynamicSwapChainOffset);
						if (swapChain != NULL && !IsBadReadPtr(swapChain, 8))
						{
							if (ApplyLUT(self, swapChain, rectVec->start, rectVec->end - rectVec->start))
							{
								SetLUTActive(self);
								success = true;
							}
						}
					}

					if (!success)
					{
						for (int off = 0x80; off < 0x240; off += 8)
						{
							void* ptr = *(void**)((unsigned char*)overlaySwapChain + off);
							if (ptr != NULL && !IsBadReadPtr(ptr, 8))
							{
								void* vtable = *(void**)ptr;
								if (vtable != NULL)
								{
									IDXGISwapChain* swapChain = (IDXGISwapChain*)ptr;

									if (ApplyLUT(self, swapChain, rectVec->start, rectVec->end - rectVec->start))
									{
										g_DynamicSwapChainOffset = off;
										SetLUTActive(self);
										success = true;
										break;
									}
								}
							}
						}
					}
				}

				if (!success)
				{
					UnsetLUTActive(self);
				}
			}
			else
			{

			bool hwProtected = false;
			if (isWindows11_24h2)
				hwProtected = *((bool*)overlaySwapChain + IOverlaySwapChain_HardwareProtected_offset_w11_24h2);
			else if (isWindows11)
				hwProtected = *((bool*)overlaySwapChain + IOverlaySwapChain_HardwareProtected_offset_w11);
			else
				hwProtected = *((bool*)overlaySwapChain + IOverlaySwapChain_HardwareProtected_offset);

			if (hwProtected)
			{
				LOG_ONLY_ONCE("Hardware protected - unsetting LUT active")
				UnsetLUTActive(self);
			}
			else
			{
				IDXGISwapChain* swapChain = NULL;

				if (isWindows11_24h2)
				{
					LOG_ONLY_ONCE("Gathering IDXGISwapChain pointer")
					swapChain = *(IDXGISwapChain**)((unsigned char*)overlaySwapChain +
						IOverlaySwapChain_IDXGISwapChain_offset_w11_24h2);
				}
				else if (isWindows11)
				{
					LOG_ONLY_ONCE("Gathering IDXGISwapChain pointer")
					int sub_from_legacy_swapchain = *(int*)((unsigned char*)overlaySwapChain - 4);
					void* real_overlay_swap_chain = (unsigned char*)overlaySwapChain - sub_from_legacy_swapchain -
						0x1b0;
					swapChain = *(IDXGISwapChain**)((unsigned char*)real_overlay_swap_chain +
						IOverlaySwapChain_IDXGISwapChain_offset_w11);
				}
				else
				{
					swapChain = *(IDXGISwapChain**)((unsigned char*)overlaySwapChain +
						IOverlaySwapChain_IDXGISwapChain_offset);
				}

				if (swapChain != NULL && ApplyLUT(self, swapChain, rectVec->start, rectVec->end - rectVec->start))
				{
					LOG_ONLY_ONCE("Setting LUTactive")
					SetLUTActive(self);
				}
				else
				{
					LOG_ONLY_ONCE("Un-setting LUTactive")
					UnsetLUTActive(self);
				}
			}
			}
	}

	return COverlayContext_Present_orig_24h2(self, overlaySwapChain, a3, rectVec, a5, a6, a7);
}


long COverlayContext_Present_hook(void* self, void* overlaySwapChain, unsigned int a3, rectVec* rectVec,
                                  unsigned int a5, bool a6)
{
	if (_ReturnAddress() < (void*)COverlayContext_Present_real_orig)
	{
		LOG_ONLY_ONCE("I am inside COverlayContext::Present hook inside the main if condition")

		bool hwProtected = false;
		if (isWindows11)
			hwProtected = *((bool*)overlaySwapChain + IOverlaySwapChain_HardwareProtected_offset_w11);
		else
			hwProtected = *((bool*)overlaySwapChain + IOverlaySwapChain_HardwareProtected_offset);

		if (hwProtected)
		{
			LOG_ONLY_ONCE("Hardware protected - unsetting LUT active")
			UnsetLUTActive(self);
		}
		else
		{
			IDXGISwapChain* swapChain;

			if (isWindows11)
			{
				LOG_ONLY_ONCE("Gathering IDXGISwapChain pointer")
				int sub_from_legacy_swapchain = *(int*)((unsigned char*)overlaySwapChain - 4);
				void* real_overlay_swap_chain = (unsigned char*)overlaySwapChain - sub_from_legacy_swapchain -
					0x1b0;
				swapChain = *(IDXGISwapChain**)((unsigned char*)real_overlay_swap_chain +
					IOverlaySwapChain_IDXGISwapChain_offset_w11);
			}
			else
			{
				swapChain = *(IDXGISwapChain**)((unsigned char*)overlaySwapChain +
					IOverlaySwapChain_IDXGISwapChain_offset);
			}

			if (ApplyLUT(self, swapChain, rectVec->start, rectVec->end - rectVec->start))
			{
				LOG_ONLY_ONCE("Setting LUTactive")
				SetLUTActive(self);
			}
			else
			{
				LOG_ONLY_ONCE("Un-setting LUTactive")
				UnsetLUTActive(self);
			}
		}
	}

	return COverlayContext_Present_orig(self, overlaySwapChain, a3, rectVec, a5, a6);
}

typedef bool (CWindowContext_IsCandidateDirectFlipCompatible_t)(void*, void*, bool);
CWindowContext_IsCandidateDirectFlipCompatible_t* CWindowContext_IsCandidateDirectFlipCompatible_orig = NULL;

bool CWindowContext_IsCandidateDirectFlipCompatible_hook(void* self, void* a2, bool a3)
{
	if (numLuts > 0)
	{
		return false;
	}
	return CWindowContext_IsCandidateDirectFlipCompatible_orig(self, a2, a3);
}

typedef bool (CCompSwapChain_IsCandidateDirectFlipCompatible_t)(void*, void*, bool);
CCompSwapChain_IsCandidateDirectFlipCompatible_t* CCompSwapChain_IsCandidateDirectFlipCompatible_orig = NULL;

bool CCompSwapChain_IsCandidateDirectFlipCompatible_hook(void* self, void* a2, bool a3)
{
	if (numLuts > 0)
	{
		return false;
	}
	return CCompSwapChain_IsCandidateDirectFlipCompatible_orig(self, a2, a3);
}

typedef bool (CCompVisual_IsCandidateForPromotion_t)(void*, void*, void*);
CCompVisual_IsCandidateForPromotion_t* CCompVisual_IsCandidateForPromotion_orig = NULL;

bool CCompVisual_IsCandidateForPromotion_hook(void* self, void* a2, void* a3)
{
	if (numLuts > 0)
	{
		return false;
	}
	return CCompVisual_IsCandidateForPromotion_orig(self, a2, a3);
}

typedef bool (CCompSwapChain_IsCandidateIndependentFlipCompatible_t)(void*);
CCompSwapChain_IsCandidateIndependentFlipCompatible_t* CCompSwapChain_IsCandidateIndependentFlipCompatible_orig = NULL;

bool CCompSwapChain_IsCandidateIndependentFlipCompatible_hook(void* self)
{
	if (numLuts > 0)
	{
		return false;
	}
	return CCompSwapChain_IsCandidateIndependentFlipCompatible_orig(self);
}

typedef bool (COverlayContext_IsCandidateDirectFlipCompatible_t)(void*, void*, void*, void*, int, unsigned int, bool, bool);

typedef bool (COverlayContext_IsCandidateDirectFlipCompatible_24h2_t)(void*, void*, void*, void*, unsigned int, bool);

COverlayContext_IsCandidateDirectFlipCompatible_t* COverlayContext_IsCandidateDirectFlipCompatible_orig;
COverlayContext_IsCandidateDirectFlipCompatible_24h2_t* COverlayContext_IsCandidateDirectFlipCompatible_orig_24h2;

bool COverlayContext_IsCandidateDirectFlipCompatible_hook_24h2(void* self, void* a2, void* a3, void* a4, unsigned int a5,
	bool a6)
{
	if (IsLUTActive(self))
	{
		return false;
	}
	return COverlayContext_IsCandidateDirectFlipCompatible_orig_24h2(self, a2, a3, a4, a5, a6);
}

bool COverlayContext_IsCandidateDirectFlipCompatible_hook(void* self, void* a2, void* a3, void* a4, int a5,
                                                          unsigned int a6, bool a7, bool a8)
{
	if (IsLUTActive(self))
	{
		return false;
	}
	return COverlayContext_IsCandidateDirectFlipCompatible_orig(self, a2, a3, a4, a5, a6, a7, a8);
}

typedef bool (COverlayContext_OverlaysEnabled_t)(void*);

COverlayContext_OverlaysEnabled_t* COverlayContext_OverlaysEnabled_orig  = NULL;

bool COverlayContext_OverlaysEnabled_hook(void* self)
{
	if (IsLUTActive(self))
	{
		return false;
	}
	return COverlayContext_OverlaysEnabled_orig(self);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved)
{
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		{
			HMODULE dwmcore = GetModuleHandle(L"dwmcore.dll");
			MODULEINFO moduleInfo;
			GetModuleInformation(GetCurrentProcess(), dwmcore, &moduleInfo, sizeof moduleInfo);

			OSVERSIONINFOEX versionInfo;
			ZeroMemory(&versionInfo, sizeof OSVERSIONINFOEX);
			versionInfo.dwOSVersionInfoSize = sizeof OSVERSIONINFOEX;
			versionInfo.dwBuildNumber = 22000;


			OSVERSIONINFOEX versionInfo24h2;
			ZeroMemory(&versionInfo24h2, sizeof OSVERSIONINFOEX);
			versionInfo24h2.dwOSVersionInfoSize = sizeof OSVERSIONINFOEX;
			versionInfo24h2.dwBuildNumber = 26100;


			OSVERSIONINFOEX versionInfo25h2;
			ZeroMemory(&versionInfo25h2, sizeof OSVERSIONINFOEX);
			versionInfo25h2.dwOSVersionInfoSize = sizeof OSVERSIONINFOEX;
			versionInfo25h2.dwBuildNumber = 26200;


			ULONGLONG dwlConditionMask = 0;
			VER_SET_CONDITION(dwlConditionMask, VER_BUILDNUMBER, VER_GREATER_EQUAL);

			OSVERSIONINFOEX versionInfo23h2;
			ZeroMemory(&versionInfo23h2, sizeof OSVERSIONINFOEX);
			versionInfo23h2.dwOSVersionInfoSize = sizeof OSVERSIONINFOEX;
			versionInfo23h2.dwBuildNumber = 22621;

			if (VerifyVersionInfo(&versionInfo25h2, VER_BUILDNUMBER, dwlConditionMask))
			{
				isWindows11_25h2 = true;
			}
			else if (VerifyVersionInfo(&versionInfo24h2, VER_BUILDNUMBER, dwlConditionMask))
			{
				isWindows11_24h2 = true;
			}
			else if (VerifyVersionInfo(&versionInfo23h2, VER_BUILDNUMBER, dwlConditionMask))
			{
				isWindows11_23h2 = true;
				isWindows11 = true;
			}
			else if (VerifyVersionInfo(&versionInfo, VER_BUILDNUMBER, dwlConditionMask))
			{
				isWindows11 = true;
			}
			else
			{
				isWindows11 = false;
			}

			if (isWindows11_25h2)
			{
				for (size_t i = 0; i <= moduleInfo.SizeOfImage - sizeof COverlayContext_OverlaysEnabled_bytes_w11_25h2; i++)
				{
					unsigned char* address = (unsigned char*)dwmcore + i;
					if (!COverlayContext_Present_orig_24h2 && sizeof COverlayContext_Present_bytes_w11_25h2 <= moduleInfo.
						SizeOfImage - i && !aob_match_inverse(address, COverlayContext_Present_bytes_w11_25h2,
							sizeof COverlayContext_Present_bytes_w11_25h2))
					{
						COverlayContext_Present_orig_24h2 = (COverlayContext_Present_24h2_t*)address;
						COverlayContext_Present_real_orig_24h2 = COverlayContext_Present_orig_24h2;
					}
					else if (!COverlayContext_Present_orig_24h2 && sizeof COverlayContext_Present_bytes_w11_25h2 <= moduleInfo.SizeOfImage - i && !aob_match_inverse(address, COverlayContext_Present_bytes_w11_25h2, sizeof COverlayContext_Present_bytes_w11_25h2))
					{
						COverlayContext_Present_orig_24h2 = (COverlayContext_Present_24h2_t*)address;
						COverlayContext_Present_real_orig_24h2 = COverlayContext_Present_orig_24h2;
					}
					else if (!COverlayContext_IsCandidateDirectFlipCompatible_orig_24h2 && sizeof COverlayContext_IsCandidateDirectFlipCompatible_bytes_w11_25h2 <= moduleInfo.SizeOfImage - i && !aob_match_inverse(address, COverlayContext_IsCandidateDirectFlipCompatible_bytes_w11_25h2, sizeof COverlayContext_IsCandidateDirectFlipCompatible_bytes_w11_25h2))
					{
						COverlayContext_IsCandidateDirectFlipCompatible_orig_24h2 = (COverlayContext_IsCandidateDirectFlipCompatible_24h2_t*)address;
					}
					else if (!CWindowContext_IsCandidateDirectFlipCompatible_orig && sizeof CWindowContext_IsCandidateDirectFlipCompatible_bytes_w11_25h2
						<= moduleInfo.SizeOfImage - i && !aob_match_inverse(
							address, CWindowContext_IsCandidateDirectFlipCompatible_bytes_w11_25h2,
							sizeof CWindowContext_IsCandidateDirectFlipCompatible_bytes_w11_25h2))
					{
						CWindowContext_IsCandidateDirectFlipCompatible_orig = (CWindowContext_IsCandidateDirectFlipCompatible_t*)address;
					}
					else if (!CCompSwapChain_IsCandidateDirectFlipCompatible_orig && sizeof CCompSwapChain_IsCandidateDirectFlipCompatible_bytes_w11_25h2
						<= moduleInfo.SizeOfImage - i && !aob_match_inverse(
							address, CCompSwapChain_IsCandidateDirectFlipCompatible_bytes_w11_25h2,
							sizeof CCompSwapChain_IsCandidateDirectFlipCompatible_bytes_w11_25h2))
					{
						CCompSwapChain_IsCandidateDirectFlipCompatible_orig = (CCompSwapChain_IsCandidateDirectFlipCompatible_t*)address;
					}
					else if (!CCompVisual_IsCandidateForPromotion_orig && sizeof CCompVisual_IsCandidateForPromotion_bytes_w11_25h2
						<= moduleInfo.SizeOfImage - i && !aob_match_inverse(
							address, CCompVisual_IsCandidateForPromotion_bytes_w11_25h2,
							sizeof CCompVisual_IsCandidateForPromotion_bytes_w11_25h2))
					{
						CCompVisual_IsCandidateForPromotion_orig = (CCompVisual_IsCandidateForPromotion_t*)address;
					}
					else if (!COverlayContext_OverlaysEnabled_orig && sizeof COverlayContext_OverlaysEnabled_bytes_w11_25h2
						<= moduleInfo.SizeOfImage - i && !aob_match_inverse(
							address, COverlayContext_OverlaysEnabled_bytes_w11_25h2,
							sizeof COverlayContext_OverlaysEnabled_bytes_w11_25h2))
					{
						COverlayContext_OverlaysEnabled_orig = (COverlayContext_OverlaysEnabled_t*)address;




						int rip_offset = *(int*)(address + 2);
						g_pOverlayTestMode = (int*)(address + 7 + rip_offset);



						const unsigned char flipMatch[] = { 0x48, 0x8D, 0x05 };
						for (int j = 0; j < 500; j++) {
							unsigned char* fAddr = address + j;
							if (!memcmp(fAddr, flipMatch, 3)) {
								CCompSwapChain_IsCandidateIndependentFlipCompatible_orig = (CCompSwapChain_IsCandidateIndependentFlipCompatible_t*)fAddr;
								break;
							}
						}
					}
					if (COverlayContext_Present_orig_24h2 && COverlayContext_IsCandidateDirectFlipCompatible_orig_24h2 &&
						COverlayContext_OverlaysEnabled_orig)
					{
						break;
					}
				}
			}
			else if (isWindows11_24h2)
			{
				for (size_t i = 0; i <= moduleInfo.SizeOfImage - sizeof COverlayContext_OverlaysEnabled_bytes_relative_w11_24h2; i++)
				{
					unsigned char* address = (unsigned char*)dwmcore + i;
					if (!COverlayContext_Present_orig && sizeof COverlayContext_Present_bytes_w11_24h2 <= moduleInfo.
						SizeOfImage - i && !aob_match_inverse(address, COverlayContext_Present_bytes_w11_24h2,
							sizeof COverlayContext_Present_bytes_w11_24h2))
					{
						COverlayContext_Present_orig_24h2 = (COverlayContext_Present_24h2_t*)address;
						COverlayContext_Present_real_orig_24h2 = COverlayContext_Present_orig_24h2;
					}
					else if (!COverlayContext_IsCandidateDirectFlipCompatible_orig && sizeof
						COverlayContext_IsCandidateDirectFlipCompatible_bytes_w11_24h2 <= moduleInfo.SizeOfImage - i && !
						aob_match_inverse(
							address, COverlayContext_IsCandidateDirectFlipCompatible_bytes_w11_24h2,
							sizeof COverlayContext_IsCandidateDirectFlipCompatible_bytes_w11_24h2))
					{
						COverlayContext_IsCandidateDirectFlipCompatible_orig_24h2 = (
							COverlayContext_IsCandidateDirectFlipCompatible_24h2_t*)address;
					}
					else if (!COverlayContext_OverlaysEnabled_orig && sizeof COverlayContext_OverlaysEnabled_bytes_relative_w11_24h2
						<= moduleInfo.SizeOfImage - i && !aob_match_inverse(
							address, COverlayContext_OverlaysEnabled_bytes_relative_w11_24h2,
							sizeof COverlayContext_OverlaysEnabled_bytes_relative_w11_24h2))
					{


						COverlayContext_OverlaysEnabled_orig = (COverlayContext_OverlaysEnabled_t*)get_relative_address(address, 1, 5);
					}
					if (COverlayContext_Present_orig && COverlayContext_IsCandidateDirectFlipCompatible_orig &&
						COverlayContext_OverlaysEnabled_orig)
					{
						break;
					}
				}
			}
			else if (isWindows11)
			{
				for (size_t i = 0; i <= moduleInfo.SizeOfImage - sizeof COverlayContext_OverlaysEnabled_bytes_w11; i++)
				{
					unsigned char* address = (unsigned char*)dwmcore + i;
					if (!COverlayContext_Present_orig && sizeof COverlayContext_Present_bytes_w11 <= moduleInfo.
						SizeOfImage - i && !aob_match_inverse(address, COverlayContext_Present_bytes_w11,
						                                      sizeof COverlayContext_Present_bytes_w11))
					{
						COverlayContext_Present_orig = (COverlayContext_Present_t*)address;
						COverlayContext_Present_real_orig = COverlayContext_Present_orig;
					}
					else if (!COverlayContext_IsCandidateDirectFlipCompatible_orig && sizeof
						COverlayContext_IsCandidateDirectFlipCompatible_bytes_w11 <= moduleInfo.SizeOfImage - i && !
						aob_match_inverse(
							address, COverlayContext_IsCandidateDirectFlipCompatible_bytes_w11,
							sizeof COverlayContext_IsCandidateDirectFlipCompatible_bytes_w11))
					{
						COverlayContext_IsCandidateDirectFlipCompatible_orig = (
							COverlayContext_IsCandidateDirectFlipCompatible_t*)address;
					}
					else if (!COverlayContext_OverlaysEnabled_orig && sizeof COverlayContext_OverlaysEnabled_bytes_w11
						<= moduleInfo.SizeOfImage - i && !aob_match_inverse(
							address, COverlayContext_OverlaysEnabled_bytes_w11,
							sizeof COverlayContext_OverlaysEnabled_bytes_w11))
					{
						COverlayContext_OverlaysEnabled_orig = (COverlayContext_OverlaysEnabled_t*)address;

						
						int rip_offset = *(int*)(address + 2);
						g_pOverlayTestMode = (int*)(address + 7 + rip_offset);
					}
					if (COverlayContext_Present_orig && COverlayContext_IsCandidateDirectFlipCompatible_orig &&
						COverlayContext_OverlaysEnabled_orig)
					{
						break;
					}
				}

				DWORD rev;
				DWORD revSize = sizeof(rev);
				RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "UBR", RRF_RT_DWORD,
				             NULL, &rev, &revSize);
			}
			else
			{
				for (size_t i = 0; i <= moduleInfo.SizeOfImage - sizeof(COverlayContext_Present_bytes); i++)
				{
					unsigned char* address = (unsigned char*)dwmcore + i;
					if (!COverlayContext_Present_orig && !memcmp(address, COverlayContext_Present_bytes,
					                                             sizeof(COverlayContext_Present_bytes)))
					{
						COverlayContext_Present_orig = (COverlayContext_Present_t*)address;
						COverlayContext_Present_real_orig = COverlayContext_Present_orig;
					}
					else if (!COverlayContext_IsCandidateDirectFlipCompatible_orig && !memcmp(
						address, COverlayContext_IsCandidateDirectFlipCompatible_bytes,
						sizeof(COverlayContext_IsCandidateDirectFlipCompatible_bytes)))
					{
						static int found = 0;
						found++;
						if (found == 2)
						{
							COverlayContext_IsCandidateDirectFlipCompatible_orig = (
								COverlayContext_IsCandidateDirectFlipCompatible_t*)(address - 0xa);
						}
					}
					else if (!COverlayContext_OverlaysEnabled_orig && !memcmp(
						address, COverlayContext_OverlaysEnabled_bytes, sizeof(COverlayContext_OverlaysEnabled_bytes)))
					{
						COverlayContext_OverlaysEnabled_orig = (COverlayContext_OverlaysEnabled_t*)(address - 0x7);
						
						
						int rip_offset = *(int*)(address - 5);
						g_pOverlayTestMode = (int*)(address + rip_offset);
					}
					if (COverlayContext_Present_orig && COverlayContext_IsCandidateDirectFlipCompatible_orig &&
						COverlayContext_OverlaysEnabled_orig)
					{
						break;
					}
				}
			}

			char lutFolderPath[MAX_PATH];
			ExpandEnvironmentStringsA(LUT_FOLDER, lutFolderPath, sizeof(lutFolderPath));
			if (!AddLUTs(lutFolderPath))
			{
				return FALSE;
			}
			if ((COverlayContext_Present_orig && COverlayContext_IsCandidateDirectFlipCompatible_orig &&
				COverlayContext_OverlaysEnabled_orig) ||
				(COverlayContext_Present_orig_24h2 && COverlayContext_IsCandidateDirectFlipCompatible_orig_24h2 && COverlayContext_OverlaysEnabled_orig) && numLuts != 0)

			{
				MH_Initialize();
				if (!isWindows11_24h2 && !isWindows11_25h2)
					MH_CreateHook((PVOID)COverlayContext_Present_orig, (PVOID)COverlayContext_Present_hook,
								  (PVOID*)&COverlayContext_Present_orig);
				else
					MH_CreateHook((PVOID)COverlayContext_Present_orig_24h2, (PVOID)COverlayContext_Present_hook_24h2,
						(PVOID*)&COverlayContext_Present_orig_24h2);

				if (!isWindows11_24h2 && !isWindows11_25h2)
					MH_CreateHook((PVOID)COverlayContext_IsCandidateDirectFlipCompatible_orig,
								  (PVOID)COverlayContext_IsCandidateDirectFlipCompatible_hook,
								  (PVOID*)&COverlayContext_IsCandidateDirectFlipCompatible_orig);
				else
					MH_CreateHook((PVOID)COverlayContext_IsCandidateDirectFlipCompatible_orig_24h2,
						(PVOID)COverlayContext_IsCandidateDirectFlipCompatible_hook_24h2,
						(PVOID*)&COverlayContext_IsCandidateDirectFlipCompatible_orig_24h2);

				if (CWindowContext_IsCandidateDirectFlipCompatible_orig)
				{
					MH_CreateHook((PVOID)CWindowContext_IsCandidateDirectFlipCompatible_orig,
						(PVOID)CWindowContext_IsCandidateDirectFlipCompatible_hook,
						(PVOID*)&CWindowContext_IsCandidateDirectFlipCompatible_orig);
					LOG_ONLY_ONCE("Hooked CWindowContext::IsCandidateDirectFlipCompatible")
				}
				else {
					LOG_ONLY_ONCE("FAILED to find CWindowContext::IsCandidateDirectFlipCompatible")
				}

				if (CCompSwapChain_IsCandidateIndependentFlipCompatible_orig)
				{
					MH_CreateHook((PVOID)CCompSwapChain_IsCandidateIndependentFlipCompatible_orig,
						(PVOID)CCompSwapChain_IsCandidateIndependentFlipCompatible_hook,
						(PVOID*)&CCompSwapChain_IsCandidateIndependentFlipCompatible_orig);
					LOG_ONLY_ONCE("Hooked CCompSwapChain::IsCandidateIndependentFlipCompatible")
				}
				else {
					LOG_ONLY_ONCE("FAILED to find CCompSwapChain::IsCandidateIndependentFlipCompatible")
				}

				if (CCompSwapChain_IsCandidateDirectFlipCompatible_orig)
				{
					MH_CreateHook((PVOID)CCompSwapChain_IsCandidateDirectFlipCompatible_orig,
						(PVOID)CCompSwapChain_IsCandidateDirectFlipCompatible_hook,
						(PVOID*)&CCompSwapChain_IsCandidateDirectFlipCompatible_orig);
					LOG_ONLY_ONCE("Hooked CCompSwapChain::IsCandidateDirectFlipCompatible")
				}
				else {
					LOG_ONLY_ONCE("FAILED to find CCompSwapChain::IsCandidateDirectFlipCompatible")
				}

				if (CCompVisual_IsCandidateForPromotion_orig)
				{
					MH_CreateHook((PVOID)CCompVisual_IsCandidateForPromotion_orig,
						(PVOID)CCompVisual_IsCandidateForPromotion_hook,
						(PVOID*)&CCompVisual_IsCandidateForPromotion_orig);
					LOG_ONLY_ONCE("Hooked CCompVisual::IsCandidateForPromotion")
				}
				else {
					LOG_ONLY_ONCE("FAILED to find CCompVisual::IsCandidateForPromotion")
				}

				if (g_pOverlayTestMode != NULL)
				{
					*g_pOverlayTestMode = 5;
					LOG_ONLY_ONCE("SUCCESS: Forced OverlayTestMode to 5")
				}
				else {
					LOG_ONLY_ONCE("FAILED to find g_pOverlayTestMode")
				}

				MH_CreateHook((PVOID)COverlayContext_OverlaysEnabled_orig, (PVOID)COverlayContext_OverlaysEnabled_hook,
				              (PVOID*)&COverlayContext_OverlaysEnabled_orig);
				MH_EnableHook(MH_ALL_HOOKS);
				LOG_ONLY_ONCE("DWM HOOK DLL INITIALIZATION. START LOGGING")


				if (g_pOverlayTestMode != NULL)
				{
					*g_pOverlayTestMode = 5;
					LOG_ONLY_ONCE("Set OverlayTestMode global to 5 in DWM memory")
				}

				break;
			}
			return FALSE;
		}
	case DLL_PROCESS_DETACH:

		if (g_pOverlayTestMode != NULL)
		{
			*g_pOverlayTestMode = 0;
		}
		MH_Uninitialize();
		Sleep(100);
		UninitializeStuff();
		break;
	default:
		break;
	}
	return TRUE;
}
