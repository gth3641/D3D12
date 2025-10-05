#include "RenderingObject.h"
#include "Manager/DirectXManager.h"
#include "Manager/ImageManager.h"

RenderingObject::RenderingObject()
{

}

RenderingObject::~RenderingObject()
{
	m_UploadBuffer.Release();
	m_VertexBuffer.Release();
}

bool RenderingObject::Init(const std::filesystem::path& imagePath, UINT64 index)
{
	m_TestIndex = index;
	AddTexture(imagePath);
	UploadTextureBuffer();
	CreateSRV();
	UploadCPUResource();

	return true;
}

void RenderingObject::UploadGPUResource(ID3D12GraphicsCommandList7* cmdList)
{
	if (!cmdList || !m_Image) return;

	// (A) VB 복사
	if (m_VBSize > 0) {
		cmdList->CopyBufferRegion(
			m_VertexBuffer, 0,
			m_UploadBuffer, m_GeomOffsetInUpload,
			m_VBSize);

		// ★ VB 상태: COPY_DEST -> VERTEX_AND_CONSTANT_BUFFER
		auto toVB = CD3DX12_RESOURCE_BARRIER::Transition(
			m_VertexBuffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		cmdList->ResourceBarrier(1, &toVB);
	}

	// (B) 텍스처 복사 (업로드 Footprint → 2D텍스처)
	D3D12_TEXTURE_COPY_LOCATION src{};
	src.pResource = m_UploadBuffer;
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint = m_TexFootprint;

	D3D12_TEXTURE_COPY_LOCATION dst{};
	dst.pResource = m_Image->GetTexture();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = 0;

	cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	// ★ 텍스처 상태: COPY_DEST -> PIXEL_SHADER_RESOURCE
	//   (RenderOffscreen에서 픽셀 셰이더가 바로 읽을 수 있게)
	auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(
		m_Image->GetTexture(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmdList->ResourceBarrier(1, &toSRV);
}

Triangle* RenderingObject::GetTriagleByIndex(size_t index)
{
	if (m_Triangle.size() > index)
	{
		return &m_Triangle[index];
	}

	return nullptr;
}

int RenderingObject::GetVertexCount()
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

	// 1) 텍스처 desc (원본 포맷 그대로)
	const auto& td = m_Image->GetTextureData();
	D3D12_RESOURCE_DESC texDesc{};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = td.width;
	texDesc.Height = td.height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = td.giPixelFormat;  // 예: DXGI_FORMAT_B8G8R8A8_UNORM
	texDesc.SampleDesc = { 1, 0 };
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	// 2) Footprint (RowPitch/UploadSize)
	UINT    numRows = 0;
	UINT64  rowSizeInBytes = 0; // width*BPP (패딩X)
	UINT64  uploadSize = 0;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
	dev->GetCopyableFootprints(&texDesc, 0, 1, 0, &fp, &numRows, &rowSizeInBytes, &uploadSize);

	m_TexFootprint = fp;
	m_TexNumRows = numRows;
	m_TexRowSizeInBytes = rowSizeInBytes;
	m_TexUploadSize = uploadSize;

	// 3) VB 총 바이트 수
	m_VBSize = 0;
	for (size_t i = 0, n = GetTriangleIndex(); i < n; ++i)
		m_VBSize += GetTriagleByIndex(i)->GetVerticiesSize();

	// 4) 업로드 버퍼 레이아웃 = [텍스처 uploadSize][정렬][VB]
	auto Align = [](UINT64 v, UINT64 a) { return (v + (a - 1)) & ~(a - 1); };
	m_GeomOffsetInUpload = Align(m_TexUploadSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT); // 512 정렬
	const UINT64 totalUpload = m_GeomOffsetInUpload + (m_VBSize ? m_VBSize : 1);

	// 5) 리소스 생성
	D3D12_HEAP_PROPERTIES hpUpload = DirectXManager::GetHeapUploadProperties();
	D3D12_HEAP_PROPERTIES hpDefault = DirectXManager::GetDefaultUploadProperties();

	auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(totalUpload);
	// ★ UPLOAD 힙은 GENERIC_READ 상태가 정석
	DX_CONTEXT.GetDevice()->CreateCommittedResource(
		&hpUpload, D3D12_HEAP_FLAG_NONE, &uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_UploadBuffer));

	auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(m_VBSize ? m_VBSize : 1);
	DX_CONTEXT.GetDevice()->CreateCommittedResource(
		&hpDefault, D3D12_HEAP_FLAG_NONE, &vbDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_VertexBuffer));

	// (이미지 내부 준비 루틴 유지)
	m_Image->UploadTextureBuffer();
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
	srv.Texture2D.MipLevels = 1;
	srv.Texture2D.MostDetailedMip = 0;
	srv.Texture2D.PlaneSlice = 0;
	srv.Texture2D.ResourceMinLODClamp = 0.f;

	DX_CONTEXT.GetDevice()->CreateShaderResourceView(m_Image->GetTexture(), &srv, DX_IMAGE.GetCPUDescriptorHandle(m_TestIndex));

}

void RenderingObject::UploadCPUResource()
{
	if (m_Image == nullptr || m_UploadBuffer == nullptr) return;

	const auto& td = m_Image->GetTextureData();
	const BYTE* src = reinterpret_cast<const BYTE*>(td.data.data());

	BYTE* dst = nullptr;
	// ★ 쓰기만 할 거면 read range = nullptr
	if (FAILED(m_UploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&dst))) || !dst)
		return;

	// 1) 텍스처 데이터: 행 단위로 복사 (dst는 RowPitch가 256 정렬)
	BYTE* texDstBase = dst + m_TexFootprint.Offset;     // 보통 0
	const UINT dstRowPitch = m_TexFootprint.Footprint.RowPitch;
	const UINT srcRowBytes = static_cast<UINT>(m_TexRowSizeInBytes);

	for (UINT r = 0; r < m_TexNumRows; ++r) {
		std::memcpy(texDstBase + r * dstRowPitch, src + r * srcRowBytes, srcRowBytes);
		// 남는 패딩은 냅둬도 OK
	}

	// 2) 업로드 버퍼의 VB 영역에 정점 데이터 연속 복사
	size_t cur = 0;
	for (size_t i = 0, n = GetTriangleIndex(); i < n; ++i) {
		Triangle* tri = GetTriagleByIndex(i);
		const size_t sz = tri->GetVerticiesSize();
		std::memcpy(dst + m_GeomOffsetInUpload + cur, tri->m_Verticies, sz);
		cur += sz;
	}

	// ★ write-only라면 Unmap range = nullptr
	m_UploadBuffer->Unmap(0, nullptr);
}
