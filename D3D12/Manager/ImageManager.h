#pragma once

#include "D3D/DXContext.h"
#include "Support/ComPointer.h"
#include "Support/ImageLoader.h"
#include "Util/Util.h"

#include "Support/Image.h"

#include <unordered_map>
#include <string>
#include <filesystem>
#include <memory>
#include <queue>
#include <vector>
#include <functional> 

#define DX_IMAGE ImageManager::Get()

class ImageManager
{
public: // Singleton pattern to ensure only one instance exists 
    ImageManager(const ImageManager&) = delete;
    ImageManager& operator=(const ImageManager&) = delete;

    inline static ImageManager& Get()
    {
        static ImageManager instance;
        return instance;
    }

public:
    ImageManager() = default;

public: // Static & Override
  
public: // Functions
    bool Init();
    void Shutdown();

    UINT64 Load(const std::filesystem::path& imagePath);
    UINT64 GetWhiteTextureIndex() const { return m_WhiteIndex; }
    UINT64 GetTextureIndex();
    void ReturnTextureIndex(Image* image);

    std::shared_ptr<Image> GetImage(const std::filesystem::path& imagePath);
    
    //===========Getter=================//
    ComPointer<ID3D12DescriptorHeap>& GetSrvheap() { return m_Srvheap; }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(const UINT64 index);
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(const UINT64 index);
    //==================================//

private: // Functions
    void CreateDescriptorHipForTexture();
    void CreateDefaultTextures();
    UINT64 CreateSRVForTexture(ID3D12Resource* tex, DXGI_FORMAT overrideFormat = DXGI_FORMAT_UNKNOWN);

private: // Variables

    ComPointer<ID3D12DescriptorHeap> m_Srvheap;

    std::unordered_map<std::string, std::weak_ptr<Image>> m_ImageMap;
    std::unordered_map<std::string, UINT64> m_PathToSrvIndex;
    std::priority_queue<UINT64, std::vector<UINT64>, std::greater<UINT64>> m_MinHeap;

    UINT64 m_NextSrvIndex = 0;
    UINT64 m_WhiteIndex = UINT64(-1);
    UINT64 m_MaxIndex = 0;

};

