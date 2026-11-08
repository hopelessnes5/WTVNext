#ifdef _DEBUG
#pragma comment(lib, "DirectXTex_d.lib")
#else
#pragma comment(lib, "DirectXTex.lib")
#endif
#include <windows.h>
#include <thumbcache.h>
#include <wrl/client.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <DirectXTex.h>

#pragma comment(lib, "shlwapi.lib")

// Уникальный GUID для нашего обработчика эскизов: {4A2D4055-6E02-4D3B-A179-880F86F6B2EA}
static const CLSID CLSID_DDSThumbnailHandler =
{ 0x4a2d4055, 0x6e02, 0x4d3b, { 0xa1, 0x79, 0x88, 0x0f, 0x86, 0xf6, 0xb2, 0xea } };

static const wchar_t* CLSID_STRING = L"{4A2D4055-6E02-4D3B-A179-880F86F6B2EA}";

long g_dllRefCount = 0;
HINSTANCE g_hInst = nullptr;

// ============================================================================
// Реализация провайдера миниатюр
// ============================================================================
class DDSThumbnailProvider : public IInitializeWithStream, public IThumbnailProvider {
    long m_refCount = 1;
    Microsoft::WRL::ComPtr<IStream> m_stream;

public:
    DDSThumbnailProvider() { InterlockedIncrement(&g_dllRefCount); }
    virtual ~DDSThumbnailProvider() { InterlockedDecrement(&g_dllRefCount); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        static const QITAB qit[] = {
            QITABENT(DDSThumbnailProvider, IInitializeWithStream),
            QITABENT(DDSThumbnailProvider, IThumbnailProvider),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }

    IFACEMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&m_refCount);
    }

    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) delete this;
        return count;
    }

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* pStream, DWORD) override {
        m_stream = pStream;
        return S_OK;
    }

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override {
        if (!m_stream || !phbmp || !pdwAlpha) return E_POINTER;

        // 1. Считываем входящий поток файла в память
        STATSTG stat;
        HRESULT hr = m_stream->Stat(&stat, STATFLAG_NONAME);
        if (FAILED(hr)) return hr;

        size_t size = static_cast<size_t>(stat.cbSize.QuadPart);
        std::vector<uint8_t> buffer(size);
        ULONG bytesRead = 0;
        hr = m_stream->Read(buffer.data(), (ULONG)size, &bytesRead);
        if (FAILED(hr) || bytesRead == 0) return E_FAIL;

        // 2. Декодируем DDS через DirectXTex
        DirectX::TexMetadata meta;
        DirectX::ScratchImage originalImage;
        hr = DirectX::LoadFromDDSMemory(buffer.data(), buffer.size(), DirectX::DDS_FLAGS_NONE, &meta, originalImage);
        if (FAILED(hr)) return hr;

        // 3. Получаем нулевой MIP-уровень
        const DirectX::Image* img = originalImage.GetImage(0, 0, 0);
        if (!img) return E_FAIL;

        DirectX::ScratchImage convertedImage;
        // Если текстура пожата (BC1-BC7), декодируем в B8G8R8A8
        if (DirectX::IsCompressed(meta.format)) {
            hr = DirectX::Decompress(*img, DXGI_FORMAT_B8G8R8A8_UNORM, convertedImage);
            if (FAILED(hr)) return hr;
            img = convertedImage.GetImage(0, 0, 0);
        }
        else if (meta.format != DXGI_FORMAT_B8G8R8A8_UNORM) {
            hr = DirectX::Convert(*img, DXGI_FORMAT_B8G8R8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, convertedImage);
            if (FAILED(hr)) return hr;
            img = convertedImage.GetImage(0, 0, 0);
        }

        // 4. Масштабируем до требуемого Проводником размера
        DirectX::ScratchImage resizedImage;
        float aspect = (float)img->width / (float)img->height;
        UINT targetW = (aspect >= 1.0f) ? cx : (UINT)(cx * aspect);
        UINT targetH = (aspect >= 1.0f) ? (UINT)(cx / aspect) : cx;
        if (targetW == 0) targetW = 1;
        if (targetH == 0) targetH = 1;

        hr = DirectX::Resize(*img, targetW, targetH, DirectX::TEX_FILTER_LINEAR, resizedImage);
        if (FAILED(hr)) return hr;

        // 5. Создаем Windows DIBSection
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = targetW;
        bmi.bmiHeader.biHeight = -(LONG)targetH; // Отрицательная высота = top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HDC hdc = GetDC(nullptr);
        HBITMAP hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, hdc);

        if (!hBitmap) return E_OUTOFMEMORY;
        memcpy(bits, resizedImage.GetPixels(), targetW * targetH * 4);

        *phbmp = hBitmap;
        *pdwAlpha = WTSAT_ARGB; // Включаем поддержку альфа-канала для эскиза
        return S_OK;
    }
};

// ============================================================================
// COM Class Factory
// ============================================================================
class ClassFactory : public IClassFactory {
    long m_refCount = 1;
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        static const QITAB qit[] = {
            QITABENT(ClassFactory, IClassFactory),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refCount); }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) delete this;
        return count;
    }
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        auto* provider = new (std::nothrow) DDSThumbnailProvider();
        if (!provider) return E_OUTOFMEMORY;
        HRESULT hr = provider->QueryInterface(riid, ppv);
        provider->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) InterlockedIncrement(&g_dllRefCount);
        else InterlockedDecrement(&g_dllRefCount);
        return S_OK;
    }
};

// ============================================================================
// Экспортируемые функции DLL
// ============================================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (IsEqualCLSID(rclsid, CLSID_DDSThumbnailHandler)) {
        auto* factory = new (std::nothrow) ClassFactory();
        if (!factory) return E_OUTOFMEMORY;
        HRESULT hr = factory->QueryInterface(riid, ppv);
        factory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT __stdcall DllCanUnloadNow() {
    return (g_dllRefCount == 0) ? S_OK : S_FALSE;
}

// Регистрация расширения в реестре
extern "C" HRESULT __stdcall DllRegisterServer() {
    wchar_t szModule[MAX_PATH];
    if (GetModuleFileNameW(g_hInst, szModule, ARRAYSIZE(szModule)) == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    std::wstring clsidKey = L"CLSID\\" + std::wstring(CLSID_STRING);
    std::wstring inprocKey = clsidKey + L"\\InprocServer32";

    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, clsidKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)L"DDS Thumbnail Handler", (DWORD)(sizeof(L"DDS Thumbnail Handler")));
        RegCloseKey(hKey);
    }

    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, inprocKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)szModule, (DWORD)((wcslen(szModule) + 1) * sizeof(wchar_t)));
        const wchar_t* model = L"Apartment";
        RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ, (const BYTE*)model, (DWORD)((wcslen(model) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    // Привязываем к расширению .dds
    std::wstring extKey = L".dds\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}";
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, extKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)CLSID_STRING, (DWORD)((wcslen(CLSID_STRING) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

// Удаление расширения из реестра
extern "C" HRESULT __stdcall DllUnregisterServer() {
    std::wstring clsidKey = L"CLSID\\" + std::wstring(CLSID_STRING);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, clsidKey.c_str());

    std::wstring extKey = L".dds\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}";
    RegDeleteTreeW(HKEY_CLASSES_ROOT, extKey.c_str());

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}