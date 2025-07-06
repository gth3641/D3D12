#include <iostream>

#include "Support/WinInclude.h"
#include "Support/ComPointer.h"

#include "DebugD3D12/DebugLayer.h"
#include "D3D/DXContext.h"

int main()
{
	DX_DEBUG_LAYER.Init();

	if (DX_CONTEXT.Init() == true)
	{
		DX_CONTEXT.GetDevice() = nullptr;

		while(true)
		{
			auto* cmdList = DX_CONTEXT.InitCommandList();

			// a lot of setup
			// a draw



			DX_CONTEXT.ExecuteCommandList();
			// Show me stuff

		}


		DX_CONTEXT.Shutdown();
	}

	DX_DEBUG_LAYER.Shutdown();
}
