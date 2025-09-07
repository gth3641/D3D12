#pragma once

#include "D3D/DXContext.h"
#include "Support/ComPointer.h"

struct ImageData;
class Shader;

class DirectXManager
{
public:
	static D3D12_HEAP_PROPERTIES GetHeapUploadProperties();
	static D3D12_HEAP_PROPERTIES GetDefaultUploadProperties();
	static D3D12_GRAPHICS_PIPELINE_STATE_DESC GetPipelineState(
		ComPointer<ID3D12RootSignature>& rootSignature, 
		D3D12_INPUT_ELEMENT_DESC* vertexLayout, 
		uint32_t vertexLayoutCount, 
		Shader& vertexShader,
		Shader& pixelShader
		);
	static D3D12_RESOURCE_DESC GetVertexResourceDesc();
	static D3D12_RESOURCE_DESC GetUploadResourceDesc(uint32_t textureSize);
	static D3D12_RESOURCE_DESC GetTextureResourceDesc(const ImageData& textureData);
};

