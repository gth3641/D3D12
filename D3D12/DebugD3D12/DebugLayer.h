#pragma once

#include "Support/WinInclude.h"
#include "Support/ComPointer.h"

#define DX_DEBUG_LAYER DXDebugLayer::Get()

class DXDebugLayer
{
public:

	bool Init();
	void Shutdown();

private:
#ifdef _DEBUG
	ComPointer<ID3D12Debug6> m_d3d12Debug;
	ComPointer<IDXGIDebug1> m_dxgiDebug;
#endif //_DEBUG


public: // Singleton pattern to ensure only one instance exists 
	DXDebugLayer(const DXDebugLayer&) = delete;
	DXDebugLayer& operator=(const DXDebugLayer&) = delete;

	inline static DXDebugLayer& Get()
	{
		static DXDebugLayer instance;
		return instance;
	}

private:
	DXDebugLayer() = default;

};


