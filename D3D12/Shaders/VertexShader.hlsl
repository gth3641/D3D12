#include "RootSignature.hlsl"
#include "Pipeline.hlsli"

//Correction correction : register(b1);
ScreenCB screenCB : register(b1);

[RootSignature(ROOTSIG)]
void main(
    // === IN ===
    in float2 pos : Position,
    in float2 uv : Texcoord,

    // === OUT ===
    out float2 o_uv : Texcoord,
    out float4 o_pos : SV_Position
)
{
    // Rules of transformation: Model -> View -> Projection
    float2 px = pos;
    //px.x = (pos.x * correction.cosAngle) - (pos.y * correction.sinAngle);
    //px.y = (pos.x * correction.sinAngle) + (pos.y * correction.cosAngle);
    //px *= correction.zoom;
    //px.x *= correction.aspecRatio;
    
    px.x = ((px.x + 0.5f) / screenCB.ViewSize.x) * 2.0f - 1.0f;
    px.y = 1.0f - ((px.y + 0.5f) / screenCB.ViewSize.y) * 2.0f; // y-down ¡æ y-up º¯È¯
    //return float2(x, y);
    
    o_pos = float4(px, 0.f, 1.f);
    o_uv = uv;
}

