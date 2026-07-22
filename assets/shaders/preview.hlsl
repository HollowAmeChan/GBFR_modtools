#define MAX_SKIN_BONES 512

cbuffer Scene : register(b0)
{
    float4x4 viewProjection;
    float4 color;
    float4 light;
    uint textured;
    uint eyeMaterial;
    uint lighting;
    uint alphaBlended;
    uint alphaMasked;
    uint alphaClipped;
    uint skinningEnabled;
    float alphaThreshold;
};

cbuffer Bones : register(b1)
{
    float4x4 skinMatrices[MAX_SKIN_BONES];
};

struct VSIn
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float4 color : COLOR0;
    uint4 joints0 : BLENDINDICES0;
    uint4 joints1 : BLENDINDICES1;
    float4 weights0 : BLENDWEIGHT0;
    float4 weights1 : BLENDWEIGHT1;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VSOut VSMain(VSIn input)
{
    float4 position = float4(input.position, 1.0);
    float3 normal = input.normal;
    if (skinningEnabled)
    {
        float weightSum = dot(input.weights0, 1.0.xxxx) + dot(input.weights1, 1.0.xxxx);
        if (weightSum > 0.0)
        {
            float4 skinnedPosition = 0.0.xxxx;
            float3 skinnedNormal = 0.0.xxx;
            [unroll]
            for (uint influence = 0; influence < 4; ++influence)
            {
                float weight = input.weights0[influence];
                skinnedPosition += mul(position, skinMatrices[input.joints0[influence]]) * weight;
                skinnedNormal += mul(normal, (float3x3)skinMatrices[input.joints0[influence]]) * weight;
            }
            [unroll]
            for (uint influence2 = 0; influence2 < 4; ++influence2)
            {
                float weight = input.weights1[influence2];
                skinnedPosition += mul(position, skinMatrices[input.joints1[influence2]]) * weight;
                skinnedNormal += mul(normal, (float3x3)skinMatrices[input.joints1[influence2]]) * weight;
            }
            position = skinnedPosition / weightSum;
            normal = normalize(skinnedNormal);
        }
    }

    VSOut output;
    output.position = mul(position, viewProjection);
    output.normal = normal;
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

VSOut VSDebug(float3 position : POSITION)
{
    VSOut output;
    output.position = mul(float4(position, 1.0), viewProjection);
    output.normal = float3(0.0, 1.0, 0.0);
    output.uv = float2(0.0, 0.0);
    output.color = float4(1.0, 1.0, 1.0, 1.0);
    return output;
}

Texture2D primaryTexture : register(t0);
Texture2D irisTexture : register(t1);
Texture2D highlightTexture : register(t2);
Texture2D alphaMaskTexture : register(t3);
SamplerState linearSampler : register(s0);

float4 PSMain(VSOut input) : SV_TARGET
{
    float2 uv = float2(input.uv.x, 1.0 - input.uv.y);
    float4 primary = textured ? primaryTexture.Sample(linearSampler, uv) : color;
    float3 base = primary.rgb * input.color.rgb;
    if (eyeMaterial)
    {
        float4 iris = irisTexture.Sample(linearSampler, uv);
        float4 highlight = highlightTexture.Sample(linearSampler, uv);
        base = lerp(float3(0.94, 0.92, 0.90), primary.rgb, primary.a);
        base = lerp(base, iris.rgb, iris.a);
        base = lerp(base, highlight.rgb, highlight.a);
    }

    float coverage = primary.a * input.color.a;
    if (alphaMasked)
        coverage *= alphaMaskTexture.Sample(linearSampler, uv).b;
    if (alphaBlended || alphaClipped)
        clip(coverage - alphaThreshold);

    float outputAlpha = alphaBlended ? coverage : color.a;
    if (!lighting)
        return float4(base, outputAlpha);

    float halfLambert = dot(normalize(input.normal), normalize(light.xyz)) * 0.5 + 0.5;
    float shade = halfLambert > 0.72 ? 1.04 : (halfLambert > 0.43 ? 0.86 : 0.68);
    return float4(saturate(base * shade + float3(0.025, 0.03, 0.035)), outputAlpha);
}
