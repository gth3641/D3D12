#include "RenderingObject.h"
#include "Manager/DirectXManager.h"
#include "Manager/ImageManager.h"

RenderingObject::RenderingObject()
{

}

RenderingObject::~RenderingObject()
{
	m_Delegate.Broadcast();
	m_UploadBuffer.Release();
	m_VertexBuffer.Release();
}

bool RenderingObject::Init(const std::filesystem::path& imagePath, UINT64 index)
{
	AddTexture(imagePath);
	UploadTextureBuffer();
	CreateSRV();

	return true;
}

void RenderingObject::UploadGPUResource(ID3D12GraphicsCommandList7* cmdList)
{
	if (!cmdList || !m_Image) return;

	// ===== (A) Vertex Buffer =====
	if (m_VBSize > 0 && mVbDirty) {
		if (mVBState != D3D12_RESOURCE_STATE_COPY_DEST) {
			auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
				m_VertexBuffer.Get(), mVBState, D3D12_RESOURCE_STATE_COPY_DEST);
			cmdList->ResourceBarrier(1, &toCopy);
		}

		cmdList->CopyBufferRegion(
			m_VertexBuffer, 0,
			m_UploadBuffer, m_GeomOffsetInUpload,
			m_VBSize);

		auto toVB = CD3DX12_RESOURCE_BARRIER::Transition(
			m_VertexBuffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		cmdList->ResourceBarrier(1, &toVB);
		mVBState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		SetVbDirty(false);
	}

	// ===== (B) Texture (mip-chain) =====
	if (mTexDirty) {
		ID3D12Resource* tex = m_Image->GetTexture();

		if (mTexState != D3D12_RESOURCE_STATE_COPY_DEST) {
			auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
				tex, mTexState, D3D12_RESOURCE_STATE_COPY_DEST);
			cmdList->ResourceBarrier(1, &toCopy);
		}

		D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
		src.pResource = m_UploadBuffer;
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		dst.pResource = tex;
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

		const UINT dstMips = tex->GetDesc().MipLevels;
		const UINT copyMips = std::min<UINT>(dstMips, m_MipCount);
		for (UINT level = 0; level < copyMips; ++level) {
			src.PlacedFootprint = m_MipFootprints[level];
			dst.SubresourceIndex = level;
			cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		}

		auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(
			tex, D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cmdList->ResourceBarrier(1, &toSRV);

		mTexState = (D3D12_RESOURCE_STATES)
			(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		SetTexDirty(false);
	}
}

Triangle* RenderingObject::GetTriagleByIndex(size_t index)
{
	if (m_Triangle.size() > index)
	{
		return &m_Triangle[index];
	}

	return nullptr;
}

size_t RenderingObject::GetVertexCount()
{
	size_t size = m_Triangle.size();
	if (size <= 0)
	{
		return 0;
	}

	return size * _countof(m_Triangle[0].m_Verticies);
}

void RenderingObject::AddTriangle(const Vertex* vertex, size_t size)
{
	Triangle triangle;
	for (size_t i = 0; i < size; ++i)
	{
		triangle.m_Verticies[i] = vertex[i];
	}

	m_Triangle.push_back(triangle);
}


void RenderingObject::AddTexture(const std::filesystem::path& imagePath)
{
	m_Image = DX_IMAGE.GetImage(imagePath);
}

void RenderingObject::UploadTextureBuffer()
{
	if (m_Image == nullptr) return;

	auto dev = DX_CONTEXT.GetDevice();
	const auto& td = m_Image->GetTextureData();

	const bool isRGBA8 =
		(td.giPixelFormat == DXGI_FORMAT_R8G8B8A8_UNORM || td.giPixelFormat == DXGI_FORMAT_B8G8R8A8_UNORM);


	auto CalcMipCount = [](UINT w, UINT h) {
		UINT m = 1;
		while (w > 1 || h > 1) { w = (w > 1) ? (w >> 1) : 1; h = (h > 1) ? (h >> 1) : 1; ++m; }
		return m;
		};

	m_MipCount = isRGBA8 ? CalcMipCount((UINT)td.width, (UINT)td.height) : 1;

	// (B) 텍스처 desc 
	D3D12_RESOURCE_DESC texDesc{};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = td.width;
	texDesc.Height = td.height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = (UINT16)m_MipCount;          
	texDesc.Format = td.giPixelFormat;         
	texDesc.SampleDesc = { 1, 0 };
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	// (C) 모든 서브리소스 Footprint 계산
	m_MipFootprints.resize(m_MipCount);
	m_MipNumRowsV.resize(m_MipCount);
	m_MipRowSizeInBytesV.resize(m_MipCount);

	UINT64 uploadSize = 0;
	dev->GetCopyableFootprints(
		&texDesc, 0, m_MipCount, 0,
		m_MipFootprints.data(),
		m_MipNumRowsV.data(),
		m_MipRowSizeInBytesV.data(),
		&uploadSize);

	if (isRGBA8 && m_MipNumRowsV[0] != td.height) {
		char buf[256];
		sprintf_s(buf, "[UploadTextureBuffer] NumRows[0]=%u, height=%u (mismatch)\n",
			m_MipNumRowsV[0], (UINT)td.height);
		OutputDebugStringA(buf);
	}

	// (D) VB 사이즈 계산
	m_VBSize = 0;
	for (size_t i = 0, n = GetTriangleIndex(); i < n; ++i)
		m_VBSize += GetTriagleByIndex(i)->GetVerticiesSize();

	// (E) 업로드 버퍼 레이아웃 = [모든 mip 데이터][512 정렬][VB]
	auto Align = [](UINT64 v, UINT64 a) { return (v + (a - 1)) & ~(a - 1); };
	m_GeomOffsetInUpload = Align(uploadSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
	const UINT64 totalUpload = m_GeomOffsetInUpload + (m_VBSize ? m_VBSize : 1);

	// (F) 리소스 생성
	D3D12_HEAP_PROPERTIES hpUpload = DirectXManager::GetHeapUploadProperties();
	D3D12_HEAP_PROPERTIES hpDefault = DirectXManager::GetDefaultUploadProperties();

	auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(totalUpload);
	DX_CONTEXT.GetDevice()->CreateCommittedResource(
		&hpUpload, D3D12_HEAP_FLAG_NONE, &uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_UploadBuffer));

	auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(m_VBSize ? m_VBSize : 1);
	DX_CONTEXT.GetDevice()->CreateCommittedResource(
		&hpDefault, D3D12_HEAP_FLAG_NONE, &vbDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_VertexBuffer));

	// (G) 업로드 버퍼에 mip-chain 채우기 (간단 2x2 box filter, RGBA8/BGRA8 전용)
	auto avg4 = [](const uint8_t* a, const uint8_t* b, const uint8_t* c, const uint8_t* d, uint8_t* o) {
		o[0] = uint8_t((uint32_t(a[0]) + b[0] + c[0] + d[0]) >> 2);
		o[1] = uint8_t((uint32_t(a[1]) + b[1] + c[1] + d[1]) >> 2);
		o[2] = uint8_t((uint32_t(a[2]) + b[2] + c[2] + d[2]) >> 2);
		o[3] = uint8_t((uint32_t(a[3]) + b[3] + c[3] + d[3]) >> 2);
		};

	const UINT bytesPerPixel = 4; 

	BYTE* dst = nullptr;
	if (FAILED(m_UploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&dst))) || !dst)
		return;

	{
		const BYTE* src = reinterpret_cast<const BYTE*>(td.data.data());
		const UINT srcRowBytes = (UINT)(td.width * bytesPerPixel);
		BYTE* texDstBase = dst + m_MipFootprints[0].Offset;
		const UINT dstRowPitch = m_MipFootprints[0].Footprint.RowPitch;
		for (UINT r = 0; r < m_MipNumRowsV[0]; ++r) {
			std::memcpy(texDstBase + r * dstRowPitch, src + r * srcRowBytes, srcRowBytes);
		}
	}

	if (isRGBA8) {
		UINT prevW = (UINT)td.width, prevH = (UINT)td.height;
		std::vector<uint8_t> prev;             
		prev.assign(td.data.begin(), td.data.end());

		for (UINT level = 1; level < m_MipCount; ++level) {
			const UINT curW = (1u > prevW >> 1 ? 1u : prevW >> 1);
			const UINT curH = (1u > prevH >> 1 ? 1u : prevH >> 1);
			std::vector<uint8_t> cur(curW * curH * bytesPerPixel);

			for (UINT y = 0; y < curH; ++y) {
				for (UINT x = 0; x < curW; ++x) {
					const UINT sx = x << 1, sy = y << 1;
					const uint8_t* A = &prev[(sy * prevW + sx) * bytesPerPixel];
					const uint8_t* B = &prev[(sy * prevW + (prevW - 1 < sx + 1 ? prevW - 1 : sx + 1)) * bytesPerPixel];
					const uint8_t* C = &prev[((prevH - 1 < sy + 1 ? prevH - 1 : sy + 1) * prevW + sx) * bytesPerPixel];
					const uint8_t* D = &prev[((prevH - 1 < sy + 1 ? prevH - 1 : sy + 1) * prevW + (prevW - 1 < sx + 1 ? prevW - 1 : sx + 1)) * bytesPerPixel];
					avg4(A, B, C, D, &cur[(y * curW + x) * bytesPerPixel]);
				}
			}

			// 업로드 버퍼에 이 레벨 쓰기 (RowPitch 패딩 고려)
			BYTE* texDstBase = dst + m_MipFootprints[level].Offset;
			const UINT dstRowPitch = m_MipFootprints[level].Footprint.RowPitch;
			const UINT srcRowBytes = curW * bytesPerPixel;
			for (UINT r = 0; r < curH; ++r) {
				std::memcpy(texDstBase + r * dstRowPitch, &cur[r * srcRowBytes], srcRowBytes);
			}

			prev.swap(cur);
			prevW = curW; prevH = curH;
		}
	}
	UpdateVertexBuffer(dst);

	m_UploadBuffer->Unmap(0, nullptr);
	
	m_Image->UploadTextureBuffer();
	mTexState = D3D12_RESOURCE_STATE_COPY_DEST;
	
	SetTexDirty(true);
	SetVbDirty(true);
}

void RenderingObject::CreateSRV()
{
	if (m_Image == nullptr)
	{
		return;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = m_Image->GetTextureData().giPixelFormat;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = UINT(-1);
	srv.Texture2D.MostDetailedMip = 0;
	srv.Texture2D.PlaneSlice = 0;
	srv.Texture2D.ResourceMinLODClamp = 0.f;

	DX_CONTEXT.GetDevice()->CreateShaderResourceView(m_Image->GetTexture(), &srv, DX_IMAGE.GetCPUDescriptorHandle(m_Image->GetIndex()));

}

void RenderingObject::UpateTexture(BYTE* dst)
{
	const auto& td = m_Image->GetTextureData();
	const BYTE* src = reinterpret_cast<const BYTE*>(td.data.data());

	const auto& fp0 = !m_MipFootprints.empty() ? m_MipFootprints[0] : m_TexFootprint;

	BYTE* texDstBase = dst + fp0.Offset;
	const UINT dstRowPitch = fp0.Footprint.RowPitch;

	const UINT bytesPerPixel = 4; // RGBA/BGRA 전제
	const UINT srcRowBytes = (UINT)(td.width * bytesPerPixel);

	for (UINT r = 0; r < fp0.Footprint.Height; ++r) {
		std::memcpy(texDstBase + r * dstRowPitch, src + r * srcRowBytes, srcRowBytes);
	}

	SetTexDirty(true);
}

void RenderingObject::UpdateVertexBuffer(BYTE* dst)
{
	size_t cur = 0;
	for (size_t i = 0, n = GetTriangleIndex(); i < n; ++i)
	{
		Triangle* tri = GetTriagleByIndex(i);
		const size_t sz = tri->GetVerticiesSize();
		std::memcpy(dst + m_GeomOffsetInUpload + cur, tri->m_Verticies, sz);
		cur += sz;
	}

	SetVbDirty(true);
}

void RenderingObject::UploadCPUResource(bool textureUpdate, bool vertexUpdate)
{
	if (m_Image == nullptr || m_UploadBuffer == nullptr) return;

	BYTE* dst = nullptr;
	if (FAILED(m_UploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&dst))) || !dst)
		return;

	if (textureUpdate == true)
	{
		UpateTexture(dst);
	}

	if (vertexUpdate == true)
	{
		UpdateVertexBuffer(dst);
	}

	m_UploadBuffer->Unmap(0, nullptr);

	
}
