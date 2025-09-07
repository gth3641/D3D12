#include <iostream>

#include "Support/ImageLoader.h"
#include "Support/WinInclude.h"
#include "Support/ComPointer.h"
#include "Support/Shader.h"
#include "Support/Window.h"

#include "Manager/DirectXManager.h"

#include "DebugD3D12/DebugLayer.h"

#include "D3D/DXContext.h"
#include "Util/Util.h"

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
		D3D12_HEAP_PROPERTIES hpUpload = DirectXManager::GetHeapUploadProperties();
		D3D12_HEAP_PROPERTIES hpDefault = DirectXManager::GetDefaultUploadProperties();

		// === Vertex Data ===
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
		ImageData textureData;
		ImageLoader::LoadImageFromDisk("./Resources/TEX_Noise.png", textureData);
		uint32_t textureStride = textureData.width * ((textureData.bpp + 7) / 8);
		uint32_t textureSize = (textureData.height * textureStride);

		// === Upload & Vertex Buffer
		D3D12_RESOURCE_DESC rdv = DirectXManager::GetVertexResourceDesc();
		D3D12_RESOURCE_DESC rdu = DirectXManager::GetUploadResourceDesc(textureSize); //< TODO: 이거 어느 상황에 필요한건지 체크
		
		ComPointer<ID3D12Resource2> uploadBuffer, vertexBuffer;
		DX_CONTEXT.GetDevice()->CreateCommittedResource(&hpUpload, D3D12_HEAP_FLAG_NONE, &rdu, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&uploadBuffer));
		DX_CONTEXT.GetDevice()->CreateCommittedResource(&hpDefault, D3D12_HEAP_FLAG_NONE, &rdv, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&vertexBuffer));
		
		// === Texture ===
		D3D12_RESOURCE_DESC rdt = DirectXManager::GetTextureResourceDesc(textureData);
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
		D3D12_GRAPHICS_PIPELINE_STATE_DESC gfxPsod = DirectXManager::GetPipelineState(rootSignature, vertexLayout, _countof(vertexLayout), vertexShader, pixelShader);
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
