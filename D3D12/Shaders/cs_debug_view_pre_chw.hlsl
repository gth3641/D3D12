// cs_postprocess.hlsl  ← 이 버전으로 교체 (핵심: 정수 맵핑)
StructuredBuffer<float> gIn : register(t0);
RWTexture2D<float4> gOut : register(u0);

cbuffer CB : register(b0)
{
    uint SrcW, SrcH, SrcC, Flags;
    uint DstW, DstH, TilesX, TilesY; // ★ TilesX/TilesY 사용
    float Gain, Bias, _f0, _f1;
}

static uint plane(uint c, uint w, uint h)
{
    return c * w * h;
}
static uint idx(uint x, uint y, uint w)
{
    return y * w + x;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= DstW || tid.y >= DstH)
        return;

    uint tx = max(1u, TilesX);
    uint ty = max(1u, TilesY);
    uint tileW = max(1u, DstW / tx);
    uint tileH = max(1u, DstH / ty);

    uint tileX = min(tx - 1, tid.x / tileW);
    uint tileY = min(ty - 1, tid.y / tileH);
    uint ch = tileY * tx + tileX;

    float val = 0.0;
    if (ch < SrcC)
    {
        uint lx = tid.x - tileX * tileW;
        uint ly = tid.y - tileY * tileH;

        // 정수 비례 매핑(최근접). 반픽셀/부동소수점 누적 오차 제거
        //   (원하는 경우 tileW/2, tileH/2 더해주면 반올림)
        uint sx = min(SrcW - 1, (lx * SrcW) / tileW);
        uint sy = min(SrcH - 1, (ly * SrcH) / tileH);

        uint base = plane(ch, SrcW, SrcH);
        val = gIn[base + idx(sx, sy, SrcW)];
        val = val * Gain + Bias;
    }

    gOut[tid.xy] = float4(saturate(val), saturate(val), saturate(val), 1.0);
}
