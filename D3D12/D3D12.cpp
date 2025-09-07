#include <iostream>

#include "Support/ImageLoader.h"
#include "Support/WinInclude.h"
#include "Support/ComPointer.h"
#include "Support/Shader.h"
#include "Support/Window.h"

#include "DebugD3D12/DebugLayer.h"

#include "D3D/DXContext.h"

void pukeColor(float* color)
{
	static int pukeState = 0;
	color[pukeState] += 0.01f;
	if (color[pukeState] > 1.f)
	{
		pukeState++;
		if (pukeState == 3)
		{
			color[0] = 0.f;
			color[1] = 0.f;
			color[2] = 0.f;

			pukeState = 0;
		}
	}
}

int main()
{
	DX_DEBUG_LAYER.Init();

	if (DX_CONTEXT.Init() == true && DX_WINDOW.Init() == true)
	{

		D3D12_HEAP_PROPERTIES hpUpload{};
		hpUpload.Type = D3D12_HEAP_TYPE_UPLOAD;
		hpUpload.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		hpUpload.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		hpUpload.CreationNodeMask = 0;
		hpUpload.VisibleNodeMask = 0;

		D3D12_HEAP_PROPERTIES hpDefault{};
		hpDefault.Type = D3D12_HEAP_TYPE_DEFAULT;
		hpDefault.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		hpDefault.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		hpDefault.CreationNodeMask = 0;
		hpDefault.VisibleNodeMask = 0;

		// === Vertex Data ===

		struct Vertex
		{
			float x, y;
			float u, v;
		};

		Vertex verticies[] = 
		{
			{ -1.f, -1.f, 0.f, 1.f },
			{  0.f,  1.f, 0.5f, 0.f },
			{  1.f, -1.f, 1.0f, 1.f }
		};

		//const char* verticies = "Hello World!";
		D3D12_INPUT_ELEMENT_DESC vertexLayout[] = 
		{
			{"Position", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{"Texcoord", 0, DXGI_FORMAT_R32G32_FLOAT, 0, sizeof(float) * 2, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};

		// === Texture Data ===
		ImageLoader::ImageData textureData;
		ImageLoader::LoadImageFromDisk("./TEX_Noise.png", textureData);
		uint32_t textureStride = textureData.width * ((textureData.bpp + 7) / 8);
		uint32_t textureSize = (textureData.height * textureStride);

		// === Upload & Vertex Buffer
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

		ComPointer<ID3D12Resource2> uploadBuffer, vertexBuffer;
		DX_CONTEXT.GetDevice()->CreateCommittedResource(&hpUpload, D3D12_HEAP_FLAG_NONE, &rdu, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&uploadBuffer));
		DX_CONTEXT.GetDevice()->CreateCommittedResource(&hpDefault, D3D12_HEAP_FLAG_NONE, &rdv, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&vertexBuffer));
		
		// === Texture ===
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

		ComPointer<ID3D12Resource2> texture;
		DX_CONTEXT.GetDevice()->CreateCommittedResource(&hpDefault, D3D12_HEAP_FLAG_NONE, &rdt, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture));

		// === Descriptor Heap for Texture(s) ===
		D3D12_DESCRIPTOR_HEAP_DESC dhd{};
		dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		dhd.NumDescriptors = 8;
		dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		dhd.NodeMask = 0;

		ComPointer<ID3D12DescriptorHeap> srvheap;
		DX_CONTEXT.GetDevice()->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&srvheap));

		// === SRV ===
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = textureData.giPixelFormat;
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Texture2D.MipLevels = 1;
		srv.Texture2D.MostDetailedMip = 0;
		srv.Texture2D.PlaneSlice = 0;
		srv.Texture2D.ResourceMinLODClamp = 0.f;

		DX_CONTEXT.GetDevice()->CreateShaderResourceView(texture, &srv, srvheap->GetCPUDescriptorHandleForHeapStart());

		// Copy void* --> CPU Resource
		char* uploadBufferAddress;
		D3D12_RANGE uploadRange;
		uploadRange.Begin = 0;
		uploadRange.End = 1024 + textureSize;
		uploadBuffer->Map(0, &uploadRange, (void**)&uploadBufferAddress);
		memcpy(&uploadBufferAddress[0], textureData.data.data(), textureSize);
		memcpy(&uploadBufferAddress[textureSize], verticies, sizeof(verticies));
		uploadBuffer->Unmap(0, &uploadRange);

		// Copy CPU Resource --> GPU Resource
		auto* cmdList = DX_CONTEXT.InitCommandList();
		cmdList->CopyBufferRegion(vertexBuffer, 0, uploadBuffer, textureSize, 1024);

		D3D12_BOX textureSizeAsBox;
		textureSizeAsBox.left = textureSizeAsBox.top = textureSizeAsBox.front = 0;
		textureSizeAsBox.right = textureData.width;
		textureSizeAsBox.bottom = textureData.height;
		textureSizeAsBox.back = 1;

		D3D12_TEXTURE_COPY_LOCATION txtcSrc, txtcDst;
		txtcSrc.pResource = uploadBuffer;
		txtcSrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		txtcSrc.PlacedFootprint.Offset = 0;
		txtcSrc.PlacedFootprint.Footprint.Width = textureData.width;
		txtcSrc.PlacedFootprint.Footprint.Height = textureData.height;
		txtcSrc.PlacedFootprint.Footprint.Depth = 1;
		txtcSrc.PlacedFootprint.Footprint.RowPitch = textureStride;
		txtcSrc.PlacedFootprint.Footprint.Format = textureData.giPixelFormat;

		txtcDst.pResource = texture;
		txtcDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		txtcDst.SubresourceIndex = 0;

		cmdList->CopyTextureRegion(&txtcDst, 0, 0, 0, &txtcSrc, &textureSizeAsBox);
		DX_CONTEXT.ExecuteCommandList();

		// === Shaders ===
		Shader rootSignatureShader("RootSignature.cso");
		Shader vertexShader("VertexShader.cso");
		Shader pixelShader("PixelShader.cso");

		// == Create root signature ===
		ComPointer<ID3D12RootSignature> rootSignature;
		DX_CONTEXT.GetDevice()->CreateRootSignature(0, rootSignatureShader.GetBuffer(), rootSignatureShader.GetSize(), IID_PPV_ARGS(&rootSignature));


		// === Pipeline State ===
		D3D12_GRAPHICS_PIPELINE_STATE_DESC gfxPsod{};
		gfxPsod.pRootSignature = rootSignature;
		gfxPsod.InputLayout.NumElements = _countof(vertexLayout);
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

		ComPointer<ID3D12PipelineState> pso;
		DX_CONTEXT.GetDevice()->CreateGraphicsPipelineState(&gfxPsod, IID_PPV_ARGS(&pso));

		// === Vertex buffer view ===
		D3D12_VERTEX_BUFFER_VIEW vbv{};
		vbv.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
		vbv.SizeInBytes = sizeof(Vertex) * _countof(verticies);
		vbv.StrideInBytes = sizeof(Vertex);

		DX_WINDOW.SetFullScreen(false);
		while (DX_WINDOW.ShouldClose() == false)
		{
			//Process pending window message
			DX_WINDOW.Update();

			// Handle resizing
			if (DX_WINDOW.ShouldResize())
			{
				DX_CONTEXT.Flush(DXWindow::GetFrameCount());
				DX_WINDOW.Resize();
			}

			//Begin drawing
			cmdList = DX_CONTEXT.InitCommandList();

			//Draw
			DX_WINDOW.BegineFrame(cmdList);

			// === PSO ===
			cmdList->SetPipelineState(pso);
			cmdList->SetGraphicsRootSignature(rootSignature);
			cmdList->SetDescriptorHeaps(1, &srvheap);
			// === IA ===
			cmdList->IASetVertexBuffers(0, 1, &vbv);
			cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			// === RS ===
			D3D12_VIEWPORT vp{};
			vp.TopLeftX = vp.TopLeftY = 0.f;
			vp.Width = DX_WINDOW.GetWidth();
			vp.Height = DX_WINDOW.GetHeight();
			vp.MinDepth = 1.f;
			vp.MaxDepth = 0.f;
			RECT scRect;
			scRect.left = scRect.top = 0;
			scRect.right = DX_WINDOW.GetWidth();
			scRect.bottom = DX_WINDOW.GetHeight();
			cmdList->RSSetScissorRects(1, &scRect);

			cmdList->RSSetViewports(1, &vp);

			// === OM ===
			static float bf_ff = 0.f;
			bf_ff += 0.01f;
			if (bf_ff > 1.f) bf_ff = 0.f;

			float bf[] = { bf_ff , bf_ff , bf_ff , bf_ff };
			cmdList->OMSetBlendFactor(bf);

			// === Update ===
			static  float color[] = { 0.f, 0.f, 0.f };
			pukeColor(color);

			static float angle = 0.0f;
			angle += 0.001f;
			struct Correction 
			{
				float aspectRatio;
				float zoom;
				float sinAngle;
				float cosAngle;
			};
			Correction correction{
				.aspectRatio = ((float)DX_WINDOW.GetHeight() / ((float)DX_WINDOW.GetWidth())),
				.zoom = 0.8f,
				.sinAngle = sinf(angle),
				.cosAngle = cosf(angle)
			};

			// === ROOT ===
			cmdList->SetGraphicsRoot32BitConstants(0, 3, color, 0);
			cmdList->SetGraphicsRoot32BitConstants(1, 4, &correction, 0);
			cmdList->SetGraphicsRootDescriptorTable(2, srvheap->GetGPUDescriptorHandleForHeapStart());

			// === Draw ===
			cmdList->DrawInstanced(_countof(verticies), 1, 0, 0);
			
			DX_WINDOW.EndFrame(cmdList);

			// a lot of setup
			// a draw

			//Finish drawing and present
			DX_CONTEXT.ExecuteCommandList();
			DX_WINDOW.Preset();
			// Show me stuff

		}

		DX_CONTEXT.Flush(DXWindow::GetFrameCount());

		// Close
		vertexBuffer.Release();
		uploadBuffer.Release();

		DX_WINDOW.Shutdown();
		DX_CONTEXT.Shutdown();
	}

	DX_DEBUG_LAYER.Shutdown();
}
