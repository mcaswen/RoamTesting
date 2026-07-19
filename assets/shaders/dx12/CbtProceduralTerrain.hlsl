cbuffer TerrainConstants : register(b0)
{
    column_major float4x4 View;
    column_major float4x4 Projection;
    float4 CameraPosition;
    float4 LightDirection;
    float4 LightColor;
    float4 LightingParameters;
    int4 DebugParameters;
};

struct TerrainVertexData
{
    float3 Position;
    float3 Normal;
    float2 TexCoord;
    float Height;
    float3 DebugColor;
    float DebugHighlight;
};

struct VertexOutput
{
    float4 Position : SV_Position;
    float3 WorldPosition : TEXCOORD0;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD1;
    float Height : TEXCOORD2;
    float3 DebugColor : COLOR0;
    float DebugHighlight : TEXCOORD3;
};

// 活动序号通过该列表映射到稳定的物理二分器槽位
StructuredBuffer<uint> ActiveBisectors : register(t1);
StructuredBuffer<TerrainVertexData> CbtVertices : register(t2);

VertexOutput VSCbtProcedural(uint vertexId : SV_VertexID)
{
    // 每三个 SV_VertexID 组成一个活动三角形
    const uint activeOrdinal = vertexId / 3u;
    const uint localVertex = vertexId % 3u;
    const uint physicalSlot = ActiveBisectors[activeOrdinal];
    const TerrainVertexData input = CbtVertices[physicalSlot * 3u + localVertex];

    VertexOutput output;
    output.WorldPosition = input.Position;
    output.Normal = input.Normal;
    output.TexCoord = input.TexCoord;
    output.Height = input.Height;
    output.DebugColor = input.DebugColor;
    output.DebugHighlight = input.DebugHighlight;
    output.Position = mul(Projection, mul(View, float4(input.Position, 1.0)));
    return output;
}
