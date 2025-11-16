#include "ImageManager.h"
#include "DirectXManager.h"

bool ImageManager::Init()
{
	CreateDescriptorHipForTexture();
	CreateDefaultTextures();

	return true;
}


void ImageManager::Shutdown()
{
	m_Srvheap.Release();
}

UINT64 ImageManager::Load(const std::filesystem::path& imagePath)
{
	const std::string key = imagePath.string();

	auto it = m_PathToSrvIndex.find(key);
	if (it != m_PathToSrvIndex.end())
		return it->second;

	auto imgPtr = GetImage(imagePath); 
	if (!imgPtr) 
	{ 
		return m_WhiteIndex;
	}
		
	ID3D12Resource* tex = imgPtr->GetTexture(); 
	if (!tex)
	{
		return m_WhiteIndex;
	}

	UINT64 idx = CreateSRVForTexture(tex);
	m_PathToSrvIndex.emplace(key, idx);
	return idx;
}

UINT64 ImageManager::GetTextureIndex()
{
	UINT64 rtValue = 0;
	if (m_MinHeap.size() > 0)
	{
		rtValue = m_MinHeap.top();
		m_MinHeap.pop();

		return rtValue;
	}
	rtValue = m_MaxIndex;
	m_MaxIndex++;

	return rtValue;
}

void ImageManager::ReturnTextureIndex(Image* image)
{
	if (image == nullptr)
	{
		return;
	}

	m_MinHeap.push(image->GetIndex());

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

	m_NextSrvIndex = 0;
	m_PathToSrvIndex.clear();
	m_WhiteIndex = UINT64(-1);
}

void ImageManager::CreateDefaultTextures()
{
	// 1x1 RGBA8 화이트 텍스처 만들기
	auto dev = DX_CONTEXT.GetDevice();

	D3D12_RESOURCE_DESC td{};
	td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	td.Width = 1;
	td.Height = 1;
	td.DepthOrArraySize = 1;
	td.MipLevels = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc = { 1,0 };
	td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	td.Flags = D3D12_RESOURCE_FLAG_NONE;

	CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
	ComPointer<ID3D12Resource> whiteTex;

	D3D12_CLEAR_VALUE* noClear = nullptr;
	dev->CreateCommittedResource(
		&heapDefault, D3D12_HEAP_FLAG_NONE, &td,
		D3D12_RESOURCE_STATE_COPY_DEST, noClear,
		IID_PPV_ARGS(&whiteTex));

	// 업로드로 0xFF 채우기
	ComPointer<ID3D12Resource> upload;
	const UINT64 uploadSize = GetRequiredIntermediateSize(whiteTex.Get(), 0, 1);
	CD3DX12_HEAP_PROPERTIES heapUp(D3D12_HEAP_TYPE_UPLOAD);
	auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
	dev->CreateCommittedResource(&heapUp, D3D12_HEAP_FLAG_NONE, &bufDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));

	// 서브리소스 작성
	D3D12_SUBRESOURCE_DATA srd{};
	UINT32 pixel = 0xFFFFFFFFu; // RGBA = (255,255,255,255)
	srd.pData = &pixel;
	srd.RowPitch = 4;
	srd.SlicePitch = 4;

	// 커맨드로 복사/전이
	ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();
	UpdateSubresources(cmd, whiteTex.Get(), upload.Get(), 0, 0, 1, &srd);
	auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(
		whiteTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmd->ResourceBarrier(1, &toSRV);
	DX_CONTEXT.ExecuteCommandList();

	// SRV 생성 후 인덱스 저장
	m_WhiteIndex = CreateSRVForTexture(whiteTex.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);

	// 리소스를 Image로 감싸서 캐시에 넣고 싶으면 필요 시 추가.
	// (필수 아님. m_WhiteIndex만 써도 됨)
}

UINT64 ImageManager::CreateSRVForTexture(ID3D12Resource* tex, DXGI_FORMAT overrideFormat)
{
	if (!tex || !m_Srvheap) return UINT64(-1);

	auto dev = DX_CONTEXT.GetDevice();
	const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	UINT64 idx = m_NextSrvIndex++;
	D3D12_CPU_DESCRIPTOR_HANDLE dst = m_Srvheap->GetCPUDescriptorHandleForHeapStart();
	dst.ptr += idx * inc;

	auto desc = tex->GetDesc();
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MipLevels = desc.MipLevels ? desc.MipLevels : 1;
	srv.Format = (overrideFormat == DXGI_FORMAT_UNKNOWN) ? desc.Format : overrideFormat;

	dev->CreateShaderResourceView(tex, &srv, dst);
	return idx;
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