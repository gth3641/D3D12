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

    std::shared_ptr<Image> GetImage(const std::filesystem::path& imagePath);

    //===========Getter=================//
    ComPointer<ID3D12DescriptorHeap>& GetSrvheap() { return m_Srvheap; }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(const UINT64 index);
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(const UINT64 index);
    //==================================//

private: // Functions
    void CreateDescriptorHipForTexture();

private: // Variables

    ComPointer<ID3D12DescriptorHeap> m_Srvheap;

    std::unordered_map<std::string, std::weak_ptr<Image>> m_ImageMap;


};

