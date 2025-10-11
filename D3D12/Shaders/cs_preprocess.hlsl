// cs_preprocess.hlsl

static const uint LINEAR_TO_SRGB = 0x0001; // BGRA 등일 때 BGR <-> RGB 스왑
static const uint PRE_BGR_SWAP = 0x0010; // BGRA 등일 때 BGR <-> RGB 스왑
static const uint PRE_MUL_255 = 0x0100; // 0..1 -> 0..255
static const uint PRE_IMAGENET_MEANSTD = 0x0400; // (x-mean)/std
static const uint PRE_CAFFE_BGR_MEAN = 0x0800; // x*255 - meanBGR
static const uint PRE_TANH_INPUT = 0x1000; // 0..1 -> [-1,1]
static const uint PRE_PT_VALID = 0x0200; // Pt 유효(이전 프레임)
static const uint OUT_TANH = 0x0001; // Pt 유효(이전 프레임)
static const uint OUT_255 = 0x0002; // Pt 유효(이전 프레임)

// 정규화 프로파일 상수
static const float3 IMAGENET_MEAN = float3(0.485, 0.456, 0.406);
static const float3 IMAGENET_STD = float3(0.229, 0.224, 0.225);
static const float3 CAFFE_MEAN_BGR = float3(103.939, 116.779, 123.68); // 0..255 기준, BGR 순

cbuffer CB : register(b0)
{
    uint W, H, C, Flags;
    uint _pad0, _pad1, _pad2, _pad3;
};

Texture2D<float4> Src : register(t0);
Texture2D<float4> PreSrc : register(t1);

SamplerState Smp : register(s0);
RWStructuredBuffer<float> Out : register(u0);

float3 LinearToSRGB(float3 x)
{
    x = saturate(x);
    float3 lo = 12.92 * x;
    float3 hi = 1.055 * pow(x, 1.0 / 2.4) - 0.055;
    return lerp(lo, hi, step(0.0031308, x));
}

float3 SRGBToLinear(float3 c)
{
    float3 lo = c / 12.92;
    float3 hi = pow((c + 0.055) / 1.055, 2.4);
    return lerp(lo, hi, step(0.04045, c));
}


[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= W || id.y >= H)
        return;

    float2 uv = (float2(id.x + 0.5, id.y + 0.5) / float2(W, H));
    float3 rgb = Src.SampleLevel(Smp, uv, 0).rgb;
    float3 preRgb = PreSrc.SampleLevel(Smp, uv, 0).rgb;
    
    // 1) 샘플 (텍셀 좌표로 정확히)
    //float3 it = Src.Load(int3(id.xy, 0)).rgb;
    //float3 pt = PreSrc.Load(int3(id.xy, 0)).rgb;
    
        // 2) 채널 스왑 (필요한 경우만 1회)
    if (Flags & PRE_BGR_SWAP)
    {
        rgb = rgb.bgr;
        preRgb = preRgb.bgr;
    }
    
    if (Flags & LINEAR_TO_SRGB)
    {
        rgb = LinearToSRGB(rgb);
        preRgb = LinearToSRGB(preRgb);
    }

    
    // 4) 정규화 프로파일  **서로 배타적으로 사용**
    if (Flags & PRE_CAFFE_BGR_MEAN)
    {
        rgb = rgb * 255.0 - CAFFE_MEAN_BGR;
        preRgb = preRgb * 255.0 - CAFFE_MEAN_BGR;
    }
    else if (Flags & PRE_IMAGENET_MEANSTD)
    {
        // ImageNet: (x-mean)/std (입력 0..1)
        rgb = (rgb - IMAGENET_MEAN) / IMAGENET_STD;
        preRgb = (preRgb - IMAGENET_MEAN) / IMAGENET_STD;
    }
    else if (Flags & PRE_TANH_INPUT)
    {
        // [-1,1] 입력
        rgb = rgb * 2.0 - 1.0;
        preRgb = preRgb * 2.0 - 1.0;
    }

    if (Flags & PRE_MUL_255)
    {
        rgb *= 255.0;
        preRgb *= 255.0;
    }
    
    uint idx = id.y * W + id.x;
    uint plane = W * H;

    // CHW layout
    Out[idx + 0 * plane] = rgb.r;
    Out[idx + 1 * plane] = rgb.g;
    Out[idx + 2 * plane] = rgb.b;
    if (C >= 6)
    {
        float3 p = (Flags & PRE_PT_VALID) ? preRgb : 0.0.xxx;
        Out[idx + 3 * plane] = p.r;
        Out[idx + 4 * plane] = p.g;
        Out[idx + 5 * plane] = p.b;
    }
}

