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

Texture2D TerrainTexture : register(t0);
SamplerState TerrainSampler : register(s0);

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float Height : TEXCOORD1;
    float3 DebugColor : COLOR0;
    float DebugHighlight : TEXCOORD2;
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

VertexOutput VSMain(VertexInput input)
{
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

float4 PSMain(VertexOutput input) : SV_Target0
{
    const float3 normal = normalize(input.Normal);
    const float3 lightDir = normalize(-LightDirection.xyz);
    const float3 viewDir = normalize(CameraPosition.xyz - input.WorldPosition);
    const float3 halfDir = normalize(lightDir + viewDir);

    const float3 textureColor = TerrainTexture.Sample(TerrainSampler, input.TexCoord * 12.0).rgb;
    const float heightBlend = smoothstep(0.2, 0.92, input.Height);
    const float3 heightTint = lerp(float3(0.10, 0.32, 0.12), float3(0.66, 0.62, 0.48), heightBlend);
    const float3 baseColor = lerp(textureColor, heightTint, 0.35);

    const float diffuse = max(dot(normal, lightDir), 0.0);
    const float specular = pow(max(dot(normal, halfDir), 0.0), 32.0);
    float3 lighting = baseColor *
        (LightingParameters.x + diffuse * LightingParameters.y) * LightColor.rgb;
    lighting += LightColor.rgb * specular * LightingParameters.z;

    if (DebugParameters.x == 1)
    {
        const float highlight = saturate(input.DebugHighlight);
        float3 debugLit = input.DebugColor * (0.45 + 0.45 * diffuse);
        debugLit += input.DebugColor * highlight * 0.35;
        lighting = lerp(lighting, debugLit, saturate(LightingParameters.w));
    }

    return float4(lighting, 1.0);
}
