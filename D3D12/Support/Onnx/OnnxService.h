#pragma once
#include "OnnxService_Udnie.h"
#include "OnnxService_AdaIN.h"
#include "OnnxService_FastNeuralStyle.h"

class OnnxService : 
	public OnnxService_Udnie, 
	public OnnxService_AdaIN,
	public OnnxService_FastNeuralStyle
{
public:
};


