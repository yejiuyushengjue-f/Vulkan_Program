struct VSinput {
    float2 inPosition;
    float3 inColor;
};

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};

ConstantBuffer<UniformBuffer> ubo;

struct VSoutput {
    float4 pos : SV_Position;
    float3 color;
};

[shader("vertex")]
VSoutput vertMain(VSinput input)
{
    VSoutput output;
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPosition, 0.0, 1.0))));
    output.color = input.inColor;
    return output;
}

[shader("fragment")]
float4 fragMain(VSoutput input) : SV_Target
{
    return float4(input.color, 1.0);
}
