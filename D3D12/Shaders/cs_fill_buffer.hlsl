RWStructuredBuffer<float> OutBuf : register(u0);
cbuffer CB : register(b0)
{
    uint Count;
    float Value;
};

[numthreads(256, 1, 1)]
void main(uint id : SV_DispatchThreadID)
{
    if (id < Count)
        OutBuf[id] = Value;
}
