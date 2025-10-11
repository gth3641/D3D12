#pragma once

#include <Windows.h>
#include <memory>

class OnnxPassResources;
class Image;
struct ID3D12GraphicsCommandList7;
struct ID3D12DescriptorHeap;
struct ID3D12Resource2;
struct ID3D12Resource;
struct OnnxGPUResources;
enum D3D12_RESOURCE_STATES;

class OnnxService_ReCoNet
{
public:
	static void RecordPreprocess_ReCoNet(
		ID3D12GraphicsCommandList7* cmd,
		ID3D12DescriptorHeap* heap,
		OnnxPassResources* onnxResource,
		OnnxGPUResources* onnxGPUResource,
		ID3D12Resource2* sceneColor,
		Image& styleImage
	);

	static void RecordPostprocess_ReCoNet(
		ID3D12GraphicsCommandList7* cmd,
		ID3D12DescriptorHeap* heap,
		OnnxPassResources* onnxResource,
		OnnxGPUResources* onnxGPUResource,
		D3D12_RESOURCE_STATES& mOnnxTexState
	);

	static void CreateOnnxResources_ReCoNet(
		UINT W, UINT H,
		Image& styleImage,
		OnnxPassResources* onnxResource,
		OnnxGPUResources* onnxGPUResource,
		ID3D12DescriptorHeap* heapCPU,
		ID3D12Resource2* sceneColor,
		D3D12_RESOURCE_STATES& mOnnxTexState,
		D3D12_RESOURCE_STATES& mOnnxInputState
	);

};


