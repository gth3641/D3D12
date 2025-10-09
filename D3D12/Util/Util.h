#pragma once
#include <iostream>
#include "Support/WinInclude.h"
#include <functional>
#include <map>
#include <vector>
#include <DirectXMath.h>

constexpr size_t VERTEX_SIZE = 3;

struct Vertex
{
	float x, y;
	float u, v;
};

struct Vtx 
{ 
    DirectX::XMFLOAT3 pos; 
    DirectX::XMFLOAT2 uv; 
};

struct Triangle
{

public:
	size_t GetVerticiesCount() { return VERTEX_SIZE; }
	size_t GetVerticiesSize() { return sizeof(Vertex) * GetVerticiesCount(); }

public:
	Vertex m_Verticies[VERTEX_SIZE];

};

class Util
{
public:
	static void Print(float time, const char* tag)
	{
		char buf[512];
		char buf2[512];
		sprintf_s(buf, "[%s] \t\t", tag);
		sprintf_s(buf2, "%.6f\n", time);
		OutputDebugStringA(buf);
		OutputDebugStringA(buf2);
	}

    static void Print(float t1, float t2, const char* tag)
    {
        char buf[512];
        char buf2[512];
        sprintf_s(buf, "[%s] \t\t", tag);
        sprintf_s(buf2, "%.6f, %.6f\n", t1, t2);
        OutputDebugStringA(buf);
        OutputDebugStringA(buf2);
    }
};


struct Delegate
{
    std::map<const void*, std::vector<std::function<void()>>> delegateMap;

    template<class T, class P>
    void AddDelegate(T* object, P&& pred)
    {
        auto findList = delegateMap.find(object);
        if (findList == delegateMap.end())
        {
            std::vector<std::function<void()>> funcList;
            funcList.push_back(pred);
            delegateMap.emplace(object, funcList);
        }
        else
        {
            findList->second.push_back(pred);
        }
    }

    template<typename T>
    void AddDelegate(T* object, void (T::* method)()) {

        auto findList = delegateMap.find(object);
        if (findList == delegateMap.end())
        {
            std::vector<std::function<void()>> funcList;
            funcList.push_back([object, method]() { (object->*method)(); });
            delegateMap.emplace(object, funcList);
        }
        else
        {
            findList->second.push_back([object, method]() { (object->*method)(); });
        }
    }

    void RemoveDelegate(const void* object)
    {
        auto findList = delegateMap.find(object);
        if (findList != delegateMap.end())
        {
            delegateMap.erase(object);
        }
    }


};