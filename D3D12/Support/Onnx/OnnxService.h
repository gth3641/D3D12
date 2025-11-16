#pragma once
//#include "OnnxService_Udnie.h"
#include "OnnxService_AdaIN.h"
#include "OnnxService_FastNeuralStyle.h"
//#include "OnnxService_ReCoNet.h"
//#include "OnnxService_BlindVideo.h"
//#include "OnnxService_Sanet.h"

class OnnxService : 
	//public OnnxService_Udnie, 
	public OnnxService_AdaIN,
	public OnnxService_FastNeuralStyle
	//public OnnxService_BlindVideo,
	//public OnnxService_Sanet
	//public OnnxService_ReCoNet
{
public:
};


