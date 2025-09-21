#include "ImageManager.h"

bool ImageManager::Init()
{
	CreateDescriptorHipForTexture();


	return true;
}


void ImageManager::Shutdown()
{
	m_Srvheap.Release();
}

std::shared_ptr<Image> ImageManager::GetImage(const std::filesystem::path& imagePath)
{
	std::string imageString = imagePath.string();
	
	auto getImage = m_ImageMap.find(imageString);
	if (getImage == m_ImageMap.end())
	{
		std::shared_ptr<Image> newImage = std::make_shared<Image>();
		newImage->ImageLoad(imagePath);

		m_ImageMap.emplace(imageString, newImage);
		return newImage;
	}

	std::weak_ptr<Image> image = getImage->second;
	if (image.expired() == 0)
	{
		return image.lock();
	}

	std::shared_ptr<Image> newImage = std::make_shared<Image>();
	newImage->ImageLoad(imagePath);

	m_ImageMap.emplace(imageString, newImage);
	return newImage;
}


void ImageManager::CreateDescriptorHipForTexture()
{
	D3D12_DESCRIPTOR_HEAP_DESC dhd{};
	dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	dhd.NumDescriptors = 4096;
	dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	dhd.NodeMask = 0;

	DX_CONTEXT.GetDevice()->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&m_Srvheap));
}

D3D12_CPU_DESCRIPTOR_HANDLE ImageManager::GetCPUDescriptorHandle(const UINT64 index)
{
	if (m_Srvheap != nullptr)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE srvHeapHandle = m_Srvheap->GetCPUDescriptorHandleForHeapStart();
		srvHeapHandle.ptr += index * DX_CONTEXT.GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		return srvHeapHandle;
	}

	return D3D12_CPU_DESCRIPTOR_HANDLE();
}

D3D12_GPU_DESCRIPTOR_HANDLE ImageManager::GetGPUDescriptorHandle(const UINT64 index)
{
	if (m_Srvheap != nullptr)
	{
		D3D12_GPU_DESCRIPTOR_HANDLE srvHeapHandle = m_Srvheap->GetGPUDescriptorHandleForHeapStart();
		srvHeapHandle.ptr += index * DX_CONTEXT.GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		return srvHeapHandle;
	}

	return D3D12_GPU_DESCRIPTOR_HANDLE();
}