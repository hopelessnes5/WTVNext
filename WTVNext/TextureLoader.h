#pragma once
#include <DirectXTex.h>
#include <string>
#include <memory>

struct LoadedTexture {
    DirectX::TexMetadata metadata;
    DirectX::ScratchImage image;
    bool isLoaded = false;
};

class TextureLoader {
public:
    static LoadedTexture LoadDDS(const std::wstring& filePath) {
        LoadedTexture result;
        HRESULT hr = DirectX::LoadFromDDSFile(
            filePath.c_str(),
            DirectX::DDS_FLAGS_NONE,
            &result.metadata,
            result.image
        );
        
        result.isLoaded = SUCCEEDED(hr);
        return result;
    }
};