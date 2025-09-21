#include "DirectXManager.h"

#include "OnnxManager.h"
#include "ImageManager.h"

#include "Support/Shader.h"

#include "d3dx12.h"
#include "d3d12.h"
#include <d3dcompiler.h>

D3D12_HEAP_PROPERTIES DirectXManager::GetHeapUploadProperties()
{
	D3D12_HEAP_PROPERTIES hpUpload{};
	hpUpload.Type = D3D12_HEAP_TYPE_UPLOAD;
	hpUpload.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	hpUpload.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	hpUpload.CreationNodeMask = 0;
	hpUpload.VisibleNodeMask = 0;

	return hpUpload;
}

D3D12_HEAP_PROPERTIES DirectXManager::GetDefaultUploadProperties()
{
	D3D12_HEAP_PROPERTIES hpDefault{};
	hpDefault.Type = D3D12_HEAP_TYPE_DEFAULT;
	hpDefault.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	hpDefault.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	hpDefault.CreationNodeMask = 0;
	hpDefault.VisibleNodeMask = 0;

	return hpDefault;
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC DirectXManager::GetPipelineState(ComPointer<ID3D12RootSignature>& rootSignature, D3D12_INPUT_ELEMENT_DESC* vertexLayout, uint32_t vertexLayoutCount, Shader& vertexShader, Shader& pixelShader)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gfxPsod{};
	gfxPsod.pRootSignature = rootSignature;
	gfxPsod.InputLayout.NumElements = vertexLayoutCount;
	gfxPsod.InputLayout.pInputElementDescs = vertexLayout;
	gfxPsod.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
	gfxPsod.VS.BytecodeLength = vertexShader.GetSize();
	gfxPsod.VS.pShaderBytecode = vertexShader.GetBuffer();
	gfxPsod.PS.BytecodeLength = pixelShader.GetSize();
	gfxPsod.PS.pShaderBytecode = pixelShader.GetBuffer();
	gfxPsod.DS.BytecodeLength = 0;
	gfxPsod.DS.pShaderBytecode = nullptr;
	gfxPsod.HS.BytecodeLength = 0;
	gfxPsod.HS.pShaderBytecode = nullptr;
	gfxPsod.GS.BytecodeLength = 0;
	gfxPsod.GS.pShaderBytecode = nullptr;
	gfxPsod.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gfxPsod.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	gfxPsod.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	gfxPsod.RasterizerState.FrontCounterClockwise = FALSE;
	gfxPsod.RasterizerState.DepthBias = 0;
	gfxPsod.RasterizerState.DepthBiasClamp = 0.0f;
	gfxPsod.RasterizerState.SlopeScaledDepthBias = 0.f;
	gfxPsod.RasterizerState.DepthClipEnable = FALSE;
	gfxPsod.RasterizerState.MultisampleEnable = FALSE;
	gfxPsod.RasterizerState.AntialiasedLineEnable = FALSE;
	gfxPsod.RasterizerState.ForcedSampleCount = 0;

	gfxPsod.StreamOutput.NumEntries = 0;
	gfxPsod.StreamOutput.NumStrides = 0;
	gfxPsod.StreamOutput.pBufferStrides = nullptr;
	gfxPsod.StreamOutput.pSODeclaration = nullptr;
	gfxPsod.StreamOutput.RasterizedStream = 0;
	gfxPsod.NumRenderTargets = 1;
	gfxPsod.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	gfxPsod.DSVFormat = DXGI_FORMAT_UNKNOWN;
	gfxPsod.BlendState.AlphaToCoverageEnable = FALSE;
	gfxPsod.BlendState.IndependentBlendEnable = FALSE;
	gfxPsod.BlendState.RenderTarget[0].BlendEnable = TRUE;
	gfxPsod.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	gfxPsod.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
	gfxPsod.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	gfxPsod.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
	gfxPsod.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	gfxPsod.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	gfxPsod.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
	gfxPsod.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
	gfxPsod.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	gfxPsod.DepthStencilState.DepthEnable = FALSE;
	gfxPsod.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	gfxPsod.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	gfxPsod.DepthStencilState.StencilEnable = FALSE;
	gfxPsod.DepthStencilState.StencilReadMask = 0;
	gfxPsod.DepthStencilState.StencilWriteMask = 0;
	gfxPsod.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	gfxPsod.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	gfxPsod.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	gfxPsod.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	gfxPsod.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	gfxPsod.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	gfxPsod.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	gfxPsod.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	gfxPsod.SampleMask = 0xFFFFFFFF;
	gfxPsod.SampleDesc.Count = 1;
	gfxPsod.SampleDesc.Quality = 0;
	gfxPsod.NodeMask = 0;
	gfxPsod.CachedPSO.CachedBlobSizeInBytes = 0;
	gfxPsod.CachedPSO.pCachedBlob = nullptr;
	gfxPsod.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	return gfxPsod;
}

D3D12_RESOURCE_DESC DirectXManager::GetVertexResourceDesc()
{
	D3D12_RESOURCE_DESC rdv{};
	rdv.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rdv.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	rdv.Width = 1024;
	rdv.Height = 1;
	rdv.DepthOrArraySize = 1;
	rdv.MipLevels = 1;
	rdv.Format = DXGI_FORMAT_UNKNOWN;
	rdv.SampleDesc.Count = 1;
	rdv.SampleDesc.Quality = 0;
	rdv.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	rdv.Flags = D3D12_RESOURCE_FLAG_NONE;

	return rdv;
}

D3D12_RESOURCE_DESC DirectXManager::GetUploadResourceDesc(uint32_t textureSize)
{
	D3D12_RESOURCE_DESC rdu{};

	rdu.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rdu.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	rdu.Width = textureSize + 1024;
	rdu.Height = 1;
	rdu.DepthOrArraySize = 1;
	rdu.MipLevels = 1;
	rdu.Format = DXGI_FORMAT_UNKNOWN;
	rdu.SampleDesc.Count = 1;
	rdu.SampleDesc.Quality = 0;
	rdu.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	rdu.Flags = D3D12_RESOURCE_FLAG_NONE;

	return rdu;
}

D3D12_RESOURCE_DESC DirectXManager::GetTextureResourceDesc(const ImageData& textureData)
{
	D3D12_RESOURCE_DESC rdt{};
	rdt.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rdt.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	rdt.Width = textureData.width;
	rdt.Height = textureData.height;
	rdt.DepthOrArraySize = 1;
	rdt.MipLevels = 1;
	rdt.Format = textureData.giPixelFormat;
	rdt.SampleDesc.Count = 1;
	rdt.SampleDesc.Quality = 0;
	rdt.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	rdt.Flags = D3D12_RESOURCE_FLAG_NONE;
	return rdt;
}

D3D12_TEXTURE_COPY_LOCATION DirectXManager::GetTextureSource(ComPointer<ID3D12Resource2>& uploadBuffer, ImageData& textureData, uint32_t textureStride)
{
	D3D12_TEXTURE_COPY_LOCATION txtcSrc;
	txtcSrc.pResource = uploadBuffer;
	txtcSrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	txtcSrc.PlacedFootprint.Offset = 0;
	txtcSrc.PlacedFootprint.Footprint.Width = textureData.width;
	txtcSrc.PlacedFootprint.Footprint.Height = textureData.height;
	txtcSrc.PlacedFootprint.Footprint.Depth = 1;
	txtcSrc.PlacedFootprint.Footprint.RowPitch = textureStride;
	txtcSrc.PlacedFootprint.Footprint.Format = textureData.giPixelFormat;

	return txtcSrc;
}

D3D12_TEXTURE_COPY_LOCATION DirectXManager::GetTextureDestination(ComPointer<ID3D12Resource2>& texture)
{
	D3D12_TEXTURE_COPY_LOCATION txtcDst;
	txtcDst.pResource = texture;
	txtcDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	txtcDst.SubresourceIndex = 0;

	return txtcDst;
}

D3D12_BOX DirectXManager::GetTextureSizeAsBox(const ImageData& textureData)
{
	D3D12_BOX textureSizeAsBox;
	textureSizeAsBox.left = textureSizeAsBox.top = textureSizeAsBox.front = 0;
	textureSizeAsBox.right = textureData.width;
	textureSizeAsBox.bottom = textureData.height;
	textureSizeAsBox.back = 1;

	return textureSizeAsBox;
}

D3D12_VERTEX_BUFFER_VIEW DirectXManager::GetVertexBufferView(ComPointer<ID3D12Resource2>& vertexBuffer, uint32_t vertexCount, uint32_t vertexSize)
{
	D3D12_VERTEX_BUFFER_VIEW vbv{};
	vbv.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vbv.SizeInBytes = vertexSize * vertexCount;
	vbv.StrideInBytes = vertexSize;

	return vbv;
}


bool DirectXManager::Init()
{
	SetVerticies();
	SetVertexLayout();

	CreateDescriptorHipForTexture();

	InitUploadRenderingObject();

	InitShader();
	return true;
}


void DirectXManager::Shutdown()
{
	m_RootSignature.Release();
	m_PipelineStateObj.Release();
	//m_Srvheap.Release();
}

void DirectXManager::UploadGPUResource(ID3D12GraphicsCommandList7* cmdList)
{
	if (cmdList == nullptr)
	{
		return;
	}

	RenderingObject1.UploadGPUResource(cmdList);
	RenderingObject2.UploadGPUResource(cmdList);
}

//D3D12_CPU_DESCRIPTOR_HANDLE DirectXManager::GetCPUDescriptorHandle(const UINT64 index)
//{
//	if (m_Srvheap != nullptr)
//	{
//		D3D12_CPU_DESCRIPTOR_HANDLE srvHeapHandle = m_Srvheap->GetCPUDescriptorHandleForHeapStart();
//		srvHeapHandle.ptr += index * DX_CONTEXT.GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
//
//		return srvHeapHandle;
//	}
//
//	return D3D12_CPU_DESCRIPTOR_HANDLE();
//}
//
//D3D12_GPU_DESCRIPTOR_HANDLE DirectXManager::GetGPUDescriptorHandle(const UINT64 index)
//{
//	if (m_Srvheap != nullptr)
//	{
//		D3D12_GPU_DESCRIPTOR_HANDLE srvHeapHandle = m_Srvheap->GetGPUDescriptorHandleForHeapStart();
//		srvHeapHandle.ptr += index * DX_CONTEXT.GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
//
//		return srvHeapHandle;
//	}
//
//	return D3D12_GPU_DESCRIPTOR_HANDLE();
//}

void DirectXManager::CreateOnnxResources(UINT W, UINT H)
{
	onnx_.Width = W; onnx_.Height = H;

	// 전처리/후처리 컴퓨트 PSO/루트시그 생성 (HLSL은 cs_preprocess.hlsl / cs_postprocess.hlsl 가정)
	// RootSig 예: SRV(t0), UAV(u0), CBV(b0)
	// Pre: SceneSRV → inputBuf(UAV)  / Post: outputBuf(SRV/Structured) → OnnxTex(UAV)
	// (구체 RS/PSO 생성 코드는 너 힙/셰이더 로더 구조에 맞춰 넣으면 됨)
	CreateOnnxComputePipeline();

	DX_ONNX.PrepareIO(DX_CONTEXT.GetDevice(), W, H);
}

void DirectXManager::ResizeOnnxResources(UINT W, UINT H)
{
	if (W == onnx_.Width && H == onnx_.Height) return;
	DX_ONNX.ResizeIO(DX_CONTEXT.GetDevice(), W, H);
}

void DirectXManager::RecordOnnxPass(ID3D12GraphicsCommandList* cmd)
{
	const UINT W = onnx_.Width, H = onnx_.Height;

	// 1) 씬을 오프스크린 RT로 렌더 (네 기존 드로우 코드 대신/또는 앞뒤에 배치)
	// Transition(SceneTex, COMMON->RTV), Clear, DrawScene..., Transition(SceneTex, RTV->SRV)

	// 2) 전처리 Compute: SceneTex(SRV) → onnxMgr_.GetInputBuffer()(UAV, StructuredBuffer<float>)
	cmd->SetComputeRootSignature(onnx_.PreRS.Get());
	cmd->SetPipelineState(onnx_.PrePSO.Get());
	// DescriptorHeaps 설정 (글로벌 SRV/UAV 힙)
	ID3D12DescriptorHeap* heaps[] = { DX_IMAGE.GetSrvheap() };
	cmd->SetDescriptorHeaps(1, heaps);

	// 루트 바인딩: SRV(t0)=onnx_.SceneSRV, UAV(u0)=inputBuf의 UAV, CBV(b0)=W,H,정규화 파라미터
	// cmd->SetComputeRootDescriptorTable(…);
	// cmd->SetComputeRootConstantBufferView(…);
	cmd->Dispatch((W + 7) / 8, (H + 7) / 8, 1);

	// 3) DML 추론 실행(큐는 Init 때 넘긴 동일 큐)
	DX_ONNX.Run();

	// 4) 후처리 Compute: onnxMgr_.GetOutputBuffer()(SRV/Structured) → onnx_.OnnxTex(UAV, RGBA8)
	//cmd->SetComputeRootSignature(onnx_.PostRS.Get());
	cmd->SetComputeRootSignature(onnx_.PreRS.Get());
	cmd->SetPipelineState(onnx_.PostPSO.Get());
	// Set SRV(t0)=outputBuf Structured SRV, UAV(u0)=onnx_.OnnxTex UAV, CBV(b0)=W,H
	cmd->Dispatch((W + 7) / 8, (H + 7) / 8, 1);

	// 5) 최종 합성: onnx_.OnnxTex(SRV)로 풀스크린 그리기 → 백버퍼
	//Transition(Backbuffer, PRESENT->RTV)
	//DrawFullscreenQuad(onnx_.OnnxTex SRV)
	//Transition(Backbuffer, RTV->PRESENT)
}

bool DirectXManager::CreateOnnxComputePipeline()
{
	ID3D12Device* device = DX_CONTEXT.GetDevice();

	// ---- (A) 컴퓨트 루트시그: [SRV(t0)] [UAV(u0)] [CBV(b0)] + Static Sampler(s0) ----
	CD3DX12_DESCRIPTOR_RANGE1 rangeSRV(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, /*baseReg*/0, /*space*/0,
		D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
	CD3DX12_DESCRIPTOR_RANGE1 rangeUAV(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, /*baseReg*/0, /*space*/0,
		D3D12_DESCRIPTOR_RANGE_FLAG_NONE);

	CD3DX12_ROOT_PARAMETER1 params[3];
	params[0].InitAsDescriptorTable(1, &rangeSRV, D3D12_SHADER_VISIBILITY_ALL); // t0
	params[1].InitAsDescriptorTable(1, &rangeUAV, D3D12_SHADER_VISIBILITY_ALL); // u0
	params[2].InitAsConstantBufferView(/*bReg*/0, /*space*/0,
		D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
		D3D12_SHADER_VISIBILITY_ALL);            // b0

	D3D12_STATIC_SAMPLER_DESC samp{};
	samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samp.ShaderRegister = 0; // s0
	samp.RegisterSpace = 0;
	samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
	rsDesc.Init_1_1(_countof(params), params, 1, &samp, D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ComPointer<ID3DBlob> sigBlob, errBlob;
	(D3D12SerializeVersionedRootSignature(&rsDesc, &sigBlob, &errBlob));
	(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
		IID_PPV_ARGS(&onnx_.PreRS)));

	// ---- (B) 컴퓨트 PSO들: 전처리 / 후처리 ----
	// cs_preprocess.hlsl 의 entry: "CSMain"
	ComPointer<ID3DBlob> csPre;
	HRESULT hr1 = (D3DCompileFromFile(L"./Shaders/cs_preprocess.hlsl", nullptr, nullptr,
		"main", "cs_5_0", 0, 0, &csPre, &errBlob));

	D3D12_COMPUTE_PIPELINE_STATE_DESC preDesc{};
	preDesc.pRootSignature = onnx_.PreRS.Get();
	preDesc.CS = { csPre->GetBufferPointer(), csPre->GetBufferSize() };
	(device->CreateComputePipelineState(&preDesc, IID_PPV_ARGS(&onnx_.PrePSO)));

	// cs_postprocess.hlsl 의 entry: "CSMain"
	ComPointer<ID3DBlob> csPost;
	hr1 = (D3DCompileFromFile(L"./Shaders/cs_postprocess.hlsl", nullptr, nullptr,
		"main", "cs_5_0", 0, 0, &csPost, &errBlob));

	D3D12_COMPUTE_PIPELINE_STATE_DESC postDesc{};
	postDesc.pRootSignature = onnx_.PreRS.Get();
	postDesc.CS = { csPost->GetBufferPointer(), csPost->GetBufferSize() };
	(device->CreateComputePipelineState(&postDesc, IID_PPV_ARGS(&onnx_.PostPSO)));

	return true;
}

void DirectXManager::SetVerticies()
{
	Vertex vertex1[] =
	{
		{ 0.f,  0.f, 0.0f, 0.f },
		{ 0.f, 500.f, 0.f, 1.f },
		{ 500.f, 500.f, 1.f, 1.f }
	};

	Vertex vertex2[] =
	{
		{ 0.f, 0.f, 0.f, 0.f },
		{ 500.f, 0.f, 1.f, 0.f },
		{ 500.f, 500.f, 1.f, 1.f }
	};

	RenderingObject1.AddTriangle(vertex1, 3);
	RenderingObject1.AddTriangle(vertex2, 3);

	Vertex vertex3[] =
	{
		{ 500.f,  0.f, 1.f, 1.f },
		{ 500.f, 500.f, 1.f, 0.f },
		{ 1000.f, 500.f, 0.f, 0.f }
	};

	Vertex vertex4[] =
	{
		{ 500.f, 0.f, 1.f, 1.f },
		{ 1000.f, 0.f, 0.f, 1.f },
		{ 1000.f, 500.f, 0.f, 0.f }
	};

	RenderingObject2.AddTriangle(vertex3, 3);
	RenderingObject2.AddTriangle(vertex4, 3);
}

void DirectXManager::SetVertexLayout()
{
	m_VertexLayout[0] = { "Position", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
	m_VertexLayout[1] = { "Texcoord", 0, DXGI_FORMAT_R32G32_FLOAT, 0, sizeof(float) * 2, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
}

void DirectXManager::CreateDescriptorHipForTexture()
{
	/*D3D12_DESCRIPTOR_HEAP_DESC dhd{};
	dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	dhd.NumDescriptors = 4096;
	dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	dhd.NodeMask = 0;

	DX_CONTEXT.GetDevice()->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&m_Srvheap));*/
}

void DirectXManager::InitUploadRenderingObject()
{
	if (RenderingObject1.Init("./Resources/TEX_Noise.png", 0) == false)
	{
		return;
	}

	if (RenderingObject2.Init("./Resources/Image.png", 1) == false)
	{
		return;
	}
}

void DirectXManager::InitShader()
{
	Shader rootSignatureShader("RootSignature.cso");
	Shader vertexShader("VertexShader.cso");
	Shader pixelShader("PixelShader.cso");

	DX_CONTEXT.GetDevice()->CreateRootSignature(0, rootSignatureShader.GetBuffer(), rootSignatureShader.GetSize(), IID_PPV_ARGS(&m_RootSignature));

	InitPipelineSate(vertexShader, pixelShader);
}

void DirectXManager::InitPipelineSate(Shader& vertexShader, Shader& pixelShader)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gfxPsod = GetPipelineState(m_RootSignature, GetVertexLayout(), GetVertexLayoutCount(), vertexShader, pixelShader);
	DX_CONTEXT.GetDevice()->CreateGraphicsPipelineState(&gfxPsod, IID_PPV_ARGS(&m_PipelineStateObj));
}
