// Quad.frag.hlsl - sample one texture.
// SDL GPU rule: fragment-stage textures + samplers live in (t[n]/s[n], space2).
Texture2D<float4> QuadTexture : register(t0, space2);
SamplerState QuadSampler : register(s0, space2);

float4 main(float2 texCoord : TEXCOORD0) : SV_Target0
{
    return QuadTexture.Sample(QuadSampler, texCoord);
}
