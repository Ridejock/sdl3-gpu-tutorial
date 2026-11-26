// Text.vert.hlsl
// Pixel positions (origin top-left, y down) become clip space.
// SDL GPU rule: vertex uniform buffers live in (b[n], space1).
cbuffer Uniforms : register(b0, space1)
{
    float2 ScreenSize; // window width, height, in pixels
    float2 Padding;
};

struct Input
{
    float2 Pixel : TEXCOORD0;    // snapped pixel position
    float2 TexCoord : TEXCOORD1; // atlas coordinate
};

struct Output
{
    float2 TexCoord : TEXCOORD0;
    float4 Position : SV_Position;
};

Output main(Input input)
{
    Output output;
    output.TexCoord = input.TexCoord;
    float2 ndc;
    ndc.x = (input.Pixel.x / ScreenSize.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (input.Pixel.y / ScreenSize.y) * 2.0;
    output.Position = float4(ndc, 0.0, 1.0);
    return output;
}
