// Text.frag.hlsl
// The atlas is R8 coverage. Sample .r and use it as alpha.
// SDL GPU rule: fragment textures and samplers live in (t[n]/s[n], space2).
Texture2D<float4> Glyphs : register(t0, space2);
SamplerState GlyphSampler : register(s0, space2);

float4 main(float2 texCoord : TEXCOORD0) : SV_Target0
{
    float coverage = Glyphs.Sample(GlyphSampler, texCoord).r;
    return float4(0.93, 0.91, 0.84, coverage);
}
