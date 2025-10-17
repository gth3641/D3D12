#pragma once

#include <Support/WinInclude.h>

#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <fstream> 

struct PreCBData
{
	UINT W, H, C, Flags;
	UINT _pad0, _pad1, _pad2, _pad3;
};

struct PostCBData 
{
	UINT SrcW, SrcH, SrcC, Flags;
	UINT DstW, DstH, _r1, _r2;
	float Gain, Bias, _pad0, _pad1;
};


class Shader
{
public:
	Shader(std::string_view name);
	~Shader();

	inline const void* GetBuffer() const { return m_data; }
	inline size_t GetSize() const { return m_size; }

private:
	void* m_data = nullptr;
	size_t m_size = 0;
};