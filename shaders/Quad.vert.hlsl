// Quad.vert.hlsl - textured quad, rotated by one uniform.
// SDL GPU rule: vertex-stage uniform buffers live in (b[n], space1).
cbuffer Uniforms : register(b0, space1)
{
    float4 RotAspect; // x = cos(angle), y = sin(angle), z = 1/aspect, w = unused
};

struct Input
{
    float2 Position : TEXCOORD0; // vertex attribute location 0
    float2 TexCoord : TEXCOORD1; // vertex attribute location 1
};

struct Output
{
    float2 TexCoord : TEXCOORD0;
    float4 Position : SV_Position;
};

Output main(Input input)
{
    const float c = RotAspect.x;
    const float s = RotAspect.y;
    float2 p = float2(input.Position.x * c - input.Position.y * s,
                      input.Position.x * s + input.Position.y * c);
    p.x *= RotAspect.z; // keep the quad square on wide windows

    Output output;
    output.TexCoord = input.TexCoord;
    output.Position = float4(p, 0.0f, 1.0f);
    return output;
}
