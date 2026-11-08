#ifdef _DEBUG
#pragma comment(lib, "DirectXTex_d.lib")
#else
#pragma comment(lib, "DirectXTex.lib")
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXTex.h>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

#ifdef _DEBUG
#pragma comment(lib, "DirectXTex_d.lib")
#else
#pragma comment(lib, "DirectXTex.lib")
#endif

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;

// Menu and control identifiers
constexpr int IDM_FILE_OPEN = 1001;
constexpr int IDM_FILE_RELOAD = 1002;
constexpr int IDM_FILE_EXIT = 1004;

constexpr int IDM_OPT_RGBA = 2001;
constexpr int IDM_OPT_RGB = 2002;
constexpr int IDM_OPT_ALPHA = 2003;
constexpr int IDM_OPT_RED = 2004;
constexpr int IDM_OPT_GREEN = 2005;
constexpr int IDM_OPT_BLUE = 2006;
constexpr int IDM_OPT_NORMALMAP = 2007;
constexpr int IDM_OPT_FIT = 2010;
constexpr int IDM_OPT_ZOOM100 = 2011;

constexpr int IDM_HELP_REGISTER = 3001;
constexpr int IDM_HELP_UNREGISTER = 3002;
constexpr int IDM_HELP_KEYS = 3003;
constexpr int IDM_HELP_ABOUT = 3004;

constexpr int IDC_STATUSBAR = 5001;

struct PSConstants {
    int channelMode; // 0 = RGBA, 1 = RGB, 2 = Alpha, 3 = Red, 4 = Green, 5 = Blue, 6 = NormalMap
    float mipLevel;
    float exposure;
    float padding;
};

// Window handles
static HWND g_hWndMain = nullptr;
static HWND g_hWndView = nullptr;
static HWND g_hStatusBar = nullptr;

// DirectX 11
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_pRenderTargetView = nullptr;
static ID3D11VertexShader* g_pVertexShader = nullptr;
static ID3D11PixelShader* g_pPixelShader = nullptr;
static ID3D11InputLayout* g_pInputLayout = nullptr;
static ID3D11Buffer* g_pVertexBuffer = nullptr;
static ID3D11Buffer* g_pConstantBuffer = nullptr;
static ID3D11SamplerState* g_pSamplerLinear = nullptr;
static ID3D11SamplerState* g_pSamplerPoint = nullptr;
static ID3D11RasterizerState* g_pRasterizerState = nullptr;

// Texture data
static ID3D11ShaderResourceView* g_pTextureSRV = nullptr;
static DirectX::TexMetadata g_Metadata = {};
static DirectX::ScratchImage g_RawImage;
static std::wstring g_CurrentFilePath = L"";
static std::vector<std::wstring> g_FolderFiles;
static size_t g_CurrentFileIndex = 0;

// Application state
static int g_CurrentMip = 0;
static int g_ChannelMode = 1; // 1 = RGB (WTV default)
static bool g_UsePointSampler = false;
static bool g_IsFitMode = true;
static float g_Zoom = 1.0f;
static float g_PanX = 0.0f;
static float g_PanY = 0.0f;
static bool g_IsDragging = false;
static POINT g_LastMousePos = { 0, 0 };

static int g_ViewWidth = 800;
static int g_ViewHeight = 600;

static const wchar_t* GetWTVFormatName(DXGI_FORMAT fmt) noexcept {
    switch (fmt) {
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB: return L"BC1 (DXT1)";
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB: return L"BC2 (DXT3)";
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB: return L"BC3 (DXT5)";
    case DXGI_FORMAT_BC4_UNORM:      return L"BC4";
    case DXGI_FORMAT_BC5_UNORM:      return L"BC5 (3Dc)";
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:      return L"BC6H (HDR)";
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB: return L"BC7";
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return L"RGBA8";
    case DXGI_FORMAT_B8G8R8A8_UNORM:      return L"BGRA8";
    case DXGI_FORMAT_R16G16B16A16_FLOAT:  return L"RGBA16F";
    default: return L"DXGI Format";
    }
}

static void UpdateStatusBarAndTitle() {
    if (g_CurrentFilePath.empty() || !g_pTextureSRV) {
        SetWindowTextW(g_hWndMain, L"WTV");
        SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"Format: None"));
        SendMessageW(g_hStatusBar, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(L"Size: 0x0"));
        SendMessageW(g_hStatusBar, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(L""));
        SendMessageW(g_hStatusBar, SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(L"MipMap: 0/0"));
        SendMessageW(g_hStatusBar, SB_SETTEXTW, 4, reinterpret_cast<LPARAM>(L"Mem: 0.0KB/0.0KB"));
        return;
    }

    const std::wstring fileName = g_CurrentFilePath.substr(g_CurrentFilePath.find_last_of(L"\\/") + 1);
    wchar_t titleBuf[512];
    swprintf_s(titleBuf, L"WTV - [%s] Zoom:%.0f%%", fileName.c_str(), static_cast<double>(g_Zoom) * 100.0);
    SetWindowTextW(g_hWndMain, titleBuf);

    size_t curMipBytes = 0;
    size_t totalBytes = 0;
    for (size_t m = 0; m < g_Metadata.mipLevels; ++m) {
        const DirectX::Image* img = g_RawImage.GetImage(m, 0, 0);
        if (img) {
            totalBytes += img->slicePitch;
            if (m == static_cast<size_t>(g_CurrentMip)) curMipBytes = img->slicePitch;
        }
    }

    wchar_t bufFormat[64], bufSize[64], bufMip[64], bufMem[128];
    swprintf_s(bufFormat, L"Format: %s", GetWTVFormatName(g_Metadata.format));
    swprintf_s(bufSize, L"Size: %ux%u", static_cast<UINT>(g_Metadata.width), static_cast<UINT>(g_Metadata.height));
    swprintf_s(bufMip, L"MipMap: %d/%zu", g_CurrentMip + 1, g_Metadata.mipLevels);

    const double curMB = static_cast<double>(curMipBytes) / (1024.0 * 1024.0);
    const double totalMB = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
    if (totalBytes >= 1024 * 1024) {
        swprintf_s(bufMem, L"Mem: %.1fMB/%.1fMB", curMB, totalMB);
    }
    else {
        const double curKB = static_cast<double>(curMipBytes) / 1024.0;
        const double totalKB = static_cast<double>(totalBytes) / 1024.0;
        swprintf_s(bufMem, L"Mem: %.1fKB/%.1fKB", curKB, totalKB);
    }

    const wchar_t* chanStr = L"";
    if (g_ChannelMode == 0) chanStr = L"R G B A";
    else if (g_ChannelMode == 1) chanStr = L"R G B";
    else if (g_ChannelMode == 2) chanStr = L"Alpha";
    else if (g_ChannelMode == 3) chanStr = L"Red";
    else if (g_ChannelMode == 4) chanStr = L"Green";
    else if (g_ChannelMode == 5) chanStr = L"Blue";
    else if (g_ChannelMode == 6) chanStr = L"Normal Z";

    SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(bufFormat));
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(bufSize));
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(chanStr));
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(bufMip));
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 4, reinterpret_cast<LPARAM>(bufMem));
}

static void FitToWindow() {
    if (g_Metadata.width == 0 || g_Metadata.height == 0 || g_ViewWidth <= 0 || g_ViewHeight <= 0) return;
    const float scaleX = static_cast<float>(g_ViewWidth) / static_cast<float>(g_Metadata.width);
    const float scaleY = static_cast<float>(g_ViewHeight) / static_cast<float>(g_Metadata.height);
    g_Zoom = (std::min)(scaleX, scaleY);
    g_PanX = (static_cast<float>(g_ViewWidth) - static_cast<float>(g_Metadata.width) * g_Zoom) * 0.5f;
    g_PanY = (static_cast<float>(g_ViewHeight) - static_cast<float>(g_Metadata.height) * g_Zoom) * 0.5f;
    UpdateStatusBarAndTitle();
}

static void ResetZoom100() {
    if (g_Metadata.width == 0 || g_Metadata.height == 0 || g_ViewWidth <= 0 || g_ViewHeight <= 0) return;
    g_Zoom = 1.0f;
    g_PanX = (static_cast<float>(g_ViewWidth) - static_cast<float>(g_Metadata.width)) * 0.5f;
    g_PanY = (static_cast<float>(g_ViewHeight) - static_cast<float>(g_Metadata.height)) * 0.5f;
    UpdateStatusBarAndTitle();
}

static void AutoAdjustZoom() {
    if (g_Metadata.width == 0 || g_Metadata.height == 0) return;
    if (static_cast<int>(g_Metadata.width) > g_ViewWidth || static_cast<int>(g_Metadata.height) > g_ViewHeight) {
        g_IsFitMode = true;
        FitToWindow();
    }
    else {
        g_IsFitMode = false;
        ResetZoom100();
    }
}

static void ScanFolderOnce(const std::wstring& currentFile) {
    g_FolderFiles.clear();
    try {
        const fs::path p(currentFile);
        const fs::path dir = fs::absolute(p).parent_path();
        std::wstring currentNormalized = fs::absolute(p).wstring();
        std::transform(currentNormalized.begin(), currentNormalized.end(), currentNormalized.begin(), ::towlower);

        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".dds" || ext == ".tga" || ext == ".png" || ext == ".jpg" || ext == ".hdr") {
                    g_FolderFiles.push_back(entry.path().wstring());
                }
            }
        }
        std::sort(g_FolderFiles.begin(), g_FolderFiles.end());

        for (size_t i = 0; i < g_FolderFiles.size(); ++i) {
            std::wstring itemNorm = g_FolderFiles[i];
            std::transform(itemNorm.begin(), itemNorm.end(), itemNorm.begin(), ::towlower);
            if (itemNorm == currentNormalized) {
                g_CurrentFileIndex = i;
                break;
            }
        }
    }
    catch (...) {}
}

static bool LoadTextureInternal(const std::wstring& path) {
    if (g_pTextureSRV) {
        g_pTextureSRV->Release();
        g_pTextureSRV = nullptr;
    }
    g_RawImage.Release();

    HRESULT hr = E_FAIL;
    std::wstring ext = path.substr(path.find_last_of(L".") + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    if (ext == L"dds") hr = DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, &g_Metadata, g_RawImage);
    else if (ext == L"tga") hr = DirectX::LoadFromTGAFile(path.c_str(), &g_Metadata, g_RawImage);
    else if (ext == L"hdr") hr = DirectX::LoadFromHDRFile(path.c_str(), &g_Metadata, g_RawImage);
    else hr = DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_NONE, &g_Metadata, g_RawImage);

    if (FAILED(hr)) return false;

    hr = DirectX::CreateShaderResourceView(g_pd3dDevice, g_RawImage.GetImages(), g_RawImage.GetImageCount(), g_Metadata, &g_pTextureSRV);
    if (FAILED(hr)) return false;

    g_CurrentFilePath = path;
    g_CurrentMip = 0;
    AutoAdjustZoom();
    return true;
}

static bool LoadTexture(const std::wstring& path) {
    if (!LoadTextureInternal(path)) return false;
    ScanFolderOnce(path);
    return true;
}

// Shell Extension helper (Registration / Unregistration directly from the app)
static std::wstring GetExecutableDirectory() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const fs::path p(exePath);
    return p.parent_path().wstring();
}

static void HandleShellExtension(bool unregister) {
    const std::wstring dllPath = GetExecutableDirectory() + L"\\WTVNext.dll";
    if (!fs::exists(dllPath)) {
        MessageBoxW(g_hWndMain, L"WTVNext.dll was not found in the application directory.", L"Error", MB_ICONERROR);
        return;
    }

    const std::wstring params = unregister ? (L"/s /u \"" + dllPath + L"\"") : (L"/s \"" + dllPath + L"\"");

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas"; // Prompts for Administrator elevation
    sei.lpFile = L"regsvr32.exe";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;

    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 10000);
            DWORD exitCode = 0;
            GetExitCodeProcess(sei.hProcess, &exitCode);
            CloseHandle(sei.hProcess);

            if (exitCode == 0) {
                if (!unregister) {
                    const int result = MessageBoxW(g_hWndMain,
                        L"Shell extension registered successfully!\n\nWould you like to restart Windows Explorer now to apply thumbnail previews immediately?",
                        L"Success", MB_YESNO | MB_ICONINFORMATION);
                    if (result == IDYES) {
                        ShellExecuteW(nullptr, nullptr, L"cmd.exe", L"/c taskkill /f /im explorer.exe & start explorer.exe", nullptr, SW_HIDE);
                    }
                }
                else {
                    MessageBoxW(g_hWndMain, L"Shell extension unregistered successfully.", L"Success", MB_OK | MB_ICONINFORMATION);
                }
            }
            else {
                MessageBoxW(g_hWndMain, L"Failed to execute regsvr32. Registration failed.", L"Error", MB_ICONERROR);
            }
        }
    }
}

// HLSL Shaders
static const char* g_ShaderSource = R"(
struct VSInput { float2 pos : POSITION; float2 uv : TEXCOORD; };
struct PSInput { float4 pos : SV_Position; float2 uv : TEXCOORD; };

PSInput VSMain(VSInput input) {
    PSInput output;
    output.pos = float4(input.pos, 0.0f, 1.0f);
    output.uv = input.uv;
    return output;
}

Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

cbuffer PSConstants : register(b0) {
    int g_ChannelMode;
    float g_MipLevel;
    float g_Exposure;
    float g_Padding;
};

float4 PSMain(PSInput input) : SV_Target {
    float4 col = g_Texture.SampleLevel(g_Sampler, input.uv, g_MipLevel);

    if (g_ChannelMode == 1) return float4(col.rgb, 1.0);
    if (g_ChannelMode == 2) return float4(col.aaa, 1.0);
    if (g_ChannelMode == 3) return float4(col.rrr, 1.0);
    if (g_ChannelMode == 4) return float4(col.ggg, 1.0);
    if (g_ChannelMode == 5) return float4(col.bbb, 1.0);

    if (g_ChannelMode == 6) {
        float2 xy = col.rg * 2.0 - 1.0;
        float z = sqrt(saturate(1.0 - dot(xy, xy)));
        return float4(float3(xy, z) * 0.5 + 0.5, 1.0);
    }

    // RGBA with checkerboard background
    float2 checkPos = floor(input.pos.xy / 12.0);
    float check = (fmod(checkPos.x + checkPos.y, 2.0) == 0.0) ? 0.85 : 0.65;
    float3 checkerPattern = float3(check, check, check);
    return float4(lerp(checkerPattern, col.rgb, col.a), 1.0);
}
)";

static bool InitD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL featureLevel;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, featureLevels, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext))) {
        return false;
    }

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    D3DCompile(g_ShaderSource, strlen(g_ShaderSource), nullptr, nullptr, nullptr, "VSMain", "vs_4_0", 0, 0, &vsBlob, nullptr);
    D3DCompile(g_ShaderSource, strlen(g_ShaderSource), nullptr, nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &psBlob, nullptr);

    if (vsBlob) {
        g_pd3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_pVertexShader);
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        g_pd3dDevice->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_pInputLayout);
        vsBlob->Release();
    }
    if (psBlob) {
        g_pd3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_pPixelShader);
        psBlob->Release();
    }

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof(float) * 4 * 6;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_pd3dDevice->CreateBuffer(&bd, nullptr, &g_pVertexBuffer);

    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.ByteWidth = sizeof(PSConstants);
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    g_pd3dDevice->CreateBuffer(&cbd, nullptr, &g_pConstantBuffer);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    g_pd3dDevice->CreateSamplerState(&sampDesc, &g_pSamplerLinear);

    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    g_pd3dDevice->CreateSamplerState(&sampDesc, &g_pSamplerPoint);

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    g_pd3dDevice->CreateRasterizerState(&rd, &g_pRasterizerState);

    return true;
}

static void ResizeD3D(int width, int height) {
    if (!g_pSwapChain || width <= 0 || height <= 0) return;

    if (g_pRenderTargetView) {
        g_pRenderTargetView->Release();
        g_pRenderTargetView = nullptr;
    }

    g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

    ID3D11Texture2D* pBackBuffer = nullptr;
    if (SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))) && pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
        pBackBuffer->Release();
    }

    g_ViewWidth = width;
    g_ViewHeight = height;
}

static void Render() {
    if (!g_pRenderTargetView) return;

    const float clearColor[4] = { 0.753f, 0.753f, 0.753f, 1.0f }; // Classic WTV #C0C0C0
    g_pd3dDeviceContext->ClearRenderTargetView(g_pRenderTargetView, clearColor);

    if (g_pTextureSRV && g_Metadata.width > 0 && g_Metadata.height > 0) {
        const float left = g_PanX;
        const float top = g_PanY;
        const float right = left + static_cast<float>(g_Metadata.width) * g_Zoom;
        const float bottom = top + static_cast<float>(g_Metadata.height) * g_Zoom;

        auto ToNdcX = [](float x, int w) noexcept { return (x / static_cast<float>(w)) * 2.0f - 1.0f; };
        auto ToNdcY = [](float y, int h) noexcept { return 1.0f - (y / static_cast<float>(h)) * 2.0f; };

        const float vertices[] = {
            ToNdcX(left,  g_ViewWidth), ToNdcY(top,    g_ViewHeight), 0.0f, 0.0f,
            ToNdcX(right, g_ViewWidth), ToNdcY(top,    g_ViewHeight), 1.0f, 0.0f,
            ToNdcX(left,  g_ViewWidth), ToNdcY(bottom, g_ViewHeight), 0.0f, 1.0f,

            ToNdcX(right, g_ViewWidth), ToNdcY(top,    g_ViewHeight), 1.0f, 0.0f,
            ToNdcX(right, g_ViewWidth), ToNdcY(bottom, g_ViewHeight), 1.0f, 1.0f,
            ToNdcX(left,  g_ViewWidth), ToNdcY(bottom, g_ViewHeight), 0.0f, 1.0f,
        };

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(g_pd3dDeviceContext->Map(g_pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, vertices, sizeof(vertices));
            g_pd3dDeviceContext->Unmap(g_pVertexBuffer, 0);
        }

        const PSConstants consts = { g_ChannelMode, static_cast<float>(g_CurrentMip), 1.0f, 0.0f };
        g_pd3dDeviceContext->UpdateSubresource(g_pConstantBuffer, 0, nullptr, &consts, 0, 0);

        D3D11_VIEWPORT vp = { 0.0f, 0.0f, static_cast<float>(g_ViewWidth), static_cast<float>(g_ViewHeight), 0.0f, 1.0f };
        g_pd3dDeviceContext->RSSetViewports(1, &vp);
        g_pd3dDeviceContext->RSSetState(g_pRasterizerState);
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);

        const UINT stride = sizeof(float) * 4;
        const UINT offset = 0;
        g_pd3dDeviceContext->IASetVertexBuffers(0, 1, &g_pVertexBuffer, &stride, &offset);
        g_pd3dDeviceContext->IASetInputLayout(g_pInputLayout);
        g_pd3dDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        g_pd3dDeviceContext->VSSetShader(g_pVertexShader, nullptr, 0);
        g_pd3dDeviceContext->PSSetShader(g_pPixelShader, nullptr, 0);
        g_pd3dDeviceContext->PSSetConstantBuffers(0, 1, &g_pConstantBuffer);
        g_pd3dDeviceContext->PSSetShaderResources(0, 1, &g_pTextureSRV);

        ID3D11SamplerState* samplers[] = { g_UsePointSampler ? g_pSamplerPoint : g_pSamplerLinear };
        g_pd3dDeviceContext->PSSetSamplers(0, 1, samplers);

        g_pd3dDeviceContext->Draw(6, 0);
    }

    g_pSwapChain->Present(1, 0);
}

static void NextFile() {
    if (g_FolderFiles.size() > 1) {
        g_CurrentFileIndex = (g_CurrentFileIndex + 1) % g_FolderFiles.size();
        LoadTextureInternal(g_FolderFiles[g_CurrentFileIndex]);
    }
}

static void PrevFile() {
    if (g_FolderFiles.size() > 1) {
        g_CurrentFileIndex = (g_CurrentFileIndex == 0) ? (g_FolderFiles.size() - 1) : (g_CurrentFileIndex - 1);
        LoadTextureInternal(g_FolderFiles[g_CurrentFileIndex]);
    }
}

static LRESULT CALLBACK ViewWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DROPFILES: {
        const HDROP hDrop = reinterpret_cast<HDROP>(wParam);
        wchar_t filePath[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH)) {
            LoadTexture(filePath);
        }
        DragFinish(hDrop);
        return 0;
    }

    case WM_LBUTTONDOWN:
        SetFocus(g_hWndMain);
        g_IsDragging = true;
        g_LastMousePos.x = LOWORD(lParam);
        g_LastMousePos.y = HIWORD(lParam);
        SetCapture(hWnd);
        return 0;

    case WM_LBUTTONUP:
        g_IsDragging = false;
        ReleaseCapture();
        return 0;

    case WM_MOUSEMOVE:
        if (g_IsDragging) {
            g_IsFitMode = false;
            const int mx = LOWORD(lParam);
            const int my = HIWORD(lParam);
            g_PanX += static_cast<float>(mx - g_LastMousePos.x);
            g_PanY += static_cast<float>(my - g_LastMousePos.y);
            g_LastMousePos = { mx, my };
        }
        return 0;

    case WM_MOUSEWHEEL: {
        g_IsFitMode = false;
        const POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        const float oldZoom = g_Zoom;
        const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta > 0) g_Zoom *= 1.25f;
        else g_Zoom /= 1.25f;

        g_Zoom = (std::max)(0.005f, (std::min)(128.0f, g_Zoom));
        g_PanX = static_cast<float>(pt.x) - (static_cast<float>(pt.x) - g_PanX) * (g_Zoom / oldZoom);
        g_PanY = static_cast<float>(pt.y) - (static_cast<float>(pt.y) - g_PanY) * (g_Zoom / oldZoom);
        UpdateStatusBarAndTitle();
        return 0;
    }

    case WM_KEYDOWN:
        SendMessageW(g_hWndMain, WM_KEYDOWN, wParam, lParam);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE: {
        if (wParam != SIZE_MINIMIZED && g_hStatusBar && g_hWndView) {
            SendMessageW(g_hStatusBar, WM_SIZE, 0, 0);
            RECT rcClient, rcStatus;
            GetClientRect(hWnd, &rcClient);
            GetWindowRect(g_hStatusBar, &rcStatus);
            const int statusH = rcStatus.bottom - rcStatus.top;

            const int newW = rcClient.right;
            const int newH = rcClient.bottom - statusH;

            const int parts[] = { 150, 260, 360, 480, -1 };
            SendMessageW(g_hStatusBar, SB_SETPARTS, 5, reinterpret_cast<LPARAM>(parts));

            SetWindowPos(g_hWndView, nullptr, 0, 0, newW, newH, SWP_NOZORDER);

            const int oldW = g_ViewWidth;
            const int oldH = g_ViewHeight;
            ResizeD3D(newW, newH);

            if (g_IsFitMode) {
                FitToWindow();
            }
            else {
                g_PanX += static_cast<float>(newW - oldW) * 0.5f;
                g_PanY += static_cast<float>(newH - oldH) * 0.5f;
                UpdateStatusBarAndTitle();
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == IDM_FILE_OPEN) {
            wchar_t szFile[MAX_PATH] = { 0 };
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFilter = L"Supported Texture Files (*.dds;*.tga;*.png;*.jpg;*.hdr)\0*.dds;*.tga;*.png;*.jpg;*.hdr\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) LoadTexture(szFile);
        }
        else if (id == IDM_FILE_RELOAD) {
            if (!g_CurrentFilePath.empty()) LoadTextureInternal(g_CurrentFilePath);
        }
        else if (id == IDM_FILE_EXIT) PostQuitMessage(0);
        else if (id == IDM_OPT_RGBA) { g_ChannelMode = 0; UpdateStatusBarAndTitle(); }
        else if (id == IDM_OPT_RGB) { g_ChannelMode = 1; UpdateStatusBarAndTitle(); }
        else if (id == IDM_OPT_ALPHA) { g_ChannelMode = 2; UpdateStatusBarAndTitle(); }
        else if (id == IDM_OPT_RED) { g_ChannelMode = 3; UpdateStatusBarAndTitle(); }
        else if (id == IDM_OPT_GREEN) { g_ChannelMode = 4; UpdateStatusBarAndTitle(); }
        else if (id == IDM_OPT_BLUE) { g_ChannelMode = 5; UpdateStatusBarAndTitle(); }
        else if (id == IDM_OPT_NORMALMAP) { g_ChannelMode = (g_ChannelMode == 6) ? 1 : 6; UpdateStatusBarAndTitle(); }
        else if (id == IDM_OPT_FIT) { g_IsFitMode = true; FitToWindow(); }
        else if (id == IDM_OPT_ZOOM100) { g_IsFitMode = false; ResetZoom100(); }
        else if (id == IDM_HELP_REGISTER) {
            HandleShellExtension(false);
        }
        else if (id == IDM_HELP_UNREGISTER) {
            HandleShellExtension(true);
        }
        else if (id == IDM_HELP_KEYS) {
            MessageBoxW(hWnd,
                L"Keyboard Shortcuts:\n\n"
                L"Up / Down Arrows   : Previous / Next MipMap level\n"
                L"Left / Right / Space : Previous / Next texture in folder\n"
                L"1 : RGBA (with transparency checkerboard)\n"
                L"2 : RGB\n"
                L"3 : Alpha (grayscale)\n"
                L"4, 5, 6 : Red, Green, Blue\n"
                L"N : Reconstruct Normal Map Z\n"
                L"F : Fit to Window\n"
                L"Home / 0 : 100% Zoom (1:1)\n"
                L"LMB + Drag : Pan texture\n"
                L"Mouse Wheel : Zoom to cursor",
                L"Keyboard Shortcuts", MB_OK | MB_ICONINFORMATION);
        }
        else if (id == IDM_HELP_ABOUT) {
            MessageBoxW(hWnd, L"Windows Texture Viewer (Next Gen)\nSupports BC1-BC7, HDR (BC6H), cubemaps, and Windows Explorer thumbnail integration.", L"About WTV", MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_UP || wParam == VK_PRIOR) {
            if (g_CurrentMip > 0) {
                g_CurrentMip--;
                UpdateStatusBarAndTitle();
            }
        }
        else if (wParam == VK_DOWN || wParam == VK_NEXT) {
            if (g_Metadata.mipLevels > 0 && g_CurrentMip < static_cast<int>(g_Metadata.mipLevels) - 1) {
                g_CurrentMip++;
                UpdateStatusBarAndTitle();
            }
        }
        else if (wParam == VK_RIGHT || wParam == VK_SPACE) NextFile();
        else if (wParam == VK_LEFT || wParam == VK_BACK) PrevFile();
        else if (wParam >= '1' && wParam <= '6') {
            g_ChannelMode = static_cast<int>(wParam - '1');
            UpdateStatusBarAndTitle();
        }
        else if (wParam == 'A') {
            g_ChannelMode = (g_ChannelMode == 0) ? 1 : 0;
            UpdateStatusBarAndTitle();
        }
        else if (wParam == 'N') {
            g_ChannelMode = (g_ChannelMode == 6) ? 1 : 6;
            UpdateStatusBarAndTitle();
        }
        else if (wParam == 'F') {
            g_IsFitMode = true;
            FitToWindow();
        }
        else if (wParam == VK_HOME || wParam == '0') {
            g_IsFitMode = false;
            ResetZoom100();
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static HMENU CreateWTVMenu() {
    HMENU hMenuBar = CreateMenu();

    // File Menu
    HMENU hFile = CreateMenu();
    AppendMenuW(hFile, MF_STRING, IDM_FILE_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(hFile, MF_STRING, IDM_FILE_RELOAD, L"&Reload\tF5");
    AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hFile, MF_STRING, IDM_FILE_EXIT, L"E&xit");
    AppendMenuW(hMenuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hFile), L"&File");

    // Options Menu
    HMENU hOptions = CreateMenu();
    AppendMenuW(hOptions, MF_STRING, IDM_OPT_RGBA, L"&RGBA\t1");
    AppendMenuW(hOptions, MF_STRING, IDM_OPT_RGB, L"RG&B\t2");
    AppendMenuW(hOptions, MF_STRING, IDM_OPT_ALPHA, L"&Alpha\t3");
    AppendMenuW(hOptions, MF_STRING, IDM_OPT_RED, L"Re&d\t4");
    AppendMenuW(hOptions, MF_STRING, IDM_OPT_GREEN, L"&Green\t5");
    AppendMenuW(hOptions, MF_STRING, IDM_OPT_BLUE, L"B&lue\t6");
    AppendMenuW(hOptions, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hOptions, MF_STRING, IDM_OPT_NORMALMAP, L"&Normal Map Z\tN");
    AppendMenuW(hOptions, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hOptions, MF_STRING, IDM_OPT_FIT, L"&Fit To Window\tF");
    AppendMenuW(hOptions, MF_STRING, IDM_OPT_ZOOM100, L"&100% Zoom\tHome");
    AppendMenuW(hMenuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hOptions), L"&Options");

    // Help Menu
    HMENU hHelp = CreateMenu();
    AppendMenuW(hHelp, MF_STRING, IDM_HELP_REGISTER, L"&Register Shell Extension (Thumbnails)...");
    AppendMenuW(hHelp, MF_STRING, IDM_HELP_UNREGISTER, L"&Unregister Shell Extension...");
    AppendMenuW(hHelp, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hHelp, MF_STRING, IDM_HELP_KEYS, L"&Keyboard Shortcuts...");
    AppendMenuW(hHelp, MF_STRING, IDM_HELP_ABOUT, L"&About WTV...");
    AppendMenuW(hMenuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hHelp), L"&Help");

    return hMenuBar;
}

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ PWSTR pCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(pCmdLine);

    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wcMain = { sizeof(wcMain), CS_CLASSDC, MainWndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr, nullptr, L"WTVMainWnd", nullptr };
    wcMain.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wcMain);

    WNDCLASSEXW wcView = { sizeof(wcView), CS_CLASSDC, ViewWndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr, nullptr, L"WTVViewWnd", nullptr };
    wcView.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wcView);

    HMENU hMenu = CreateWTVMenu();
    g_hWndMain = CreateWindowW(wcMain.lpszClassName, L"WTV", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 100, 100, 950, 750, nullptr, hMenu, hInstance, nullptr);

    g_hStatusBar = CreateWindowExW(0, STATUSCLASSNAME, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, g_hWndMain, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUSBAR)), hInstance, nullptr);

    g_hWndView = CreateWindowExW(0, wcView.lpszClassName, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 900, 680, g_hWndMain, nullptr, hInstance, nullptr);

    if (!InitD3D(g_hWndView)) return 1;

    DragAcceptFiles(g_hWndView, TRUE);
    ShowWindow(g_hWndMain, nCmdShow);
    UpdateWindow(g_hWndMain);

    RECT rc;
    GetClientRect(g_hWndMain, &rc);
    SendMessageW(g_hWndMain, WM_SIZE, SIZE_RESTORED, MAKELPARAM(rc.right, rc.bottom));

    if (__argc > 1) {
        LoadTexture(__wargv[1]);
    }

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        else {
            Render();
        }
    }

    UnregisterClassW(wcView.lpszClassName, hInstance);
    UnregisterClassW(wcMain.lpszClassName, hInstance);
    return 0;
}