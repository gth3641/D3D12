#include "OnnxManager.h"
#include "D3D/DXContext.h"

bool OnnxManager::Init()
{
	//Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "app");
	//Ort::SessionOptions so;
	//so.DisableMemPattern();
	//so.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
	//
	//DMLCreateDevice(DX_CONTEXT.GetDevice(), DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&m_Dml));

	//Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(so, m_Dml));

	//Ort::Session sess(env, L"./Resources/Onnx/udnie-9.onnx", so);

	return true;
}


void OnnxManager::Shutdown()
{
	m_Dml.Release();
}
