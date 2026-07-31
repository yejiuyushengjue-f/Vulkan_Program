static float2 positions[3] = float2[](
    float2(0.0, -0.5),
    float2(0.5, 0.5),
    float2(-0.5, 0.5)
);

static float3 colors[3] = float3[](
    float3(1.0, 0.0, 0.0),
    float3(0.0, 1.0, 0.0),
    float3(0.0, 0.0, 1.0)
);

struct V2F {
    float3 color;
    float4 sv_position : SV_Position;
};

[shader("vertex")]
V2F vertMain(uint vertexId : SV_VertexID)
{
    V2F output;
    output.sv_position = float4(positions[vertexId], 0.0, 1.0);
    output.color = colors[vertexId];
    return output;
}

[shader("fragment")]
float4 fragMain(V2F input) : SV_Target
{
    return float4(input.color, 1.0);
}