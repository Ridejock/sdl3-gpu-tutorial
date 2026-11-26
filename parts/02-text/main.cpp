// Part 2 - draw text with SDL3 GPU. C++20, SDL3 core only.
// Build: see docs/part-02-text.md.
// Shaders load from shaders/Text.{vert,frag}.{spv,dxil,msl} next to the executable.
// The TTF loads from fonts/ next to the executable.
#include "HandFont.hpp"

#include <stddef.h>
#include "stb_truetype.h"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace {

struct Vertex {
    float X, Y; // pixel position, origin top-left, y down
    float U, V;
};

struct Glyph {
    float U0 = 0, V0 = 0, U1 = 0, V1 = 0;
    float XOff = 0, YOff = 0;
    float Width = 0, Height = 0;
    float XAdvance = 0;
};

struct Atlas {
    SDL_GPUTexture* Texture = nullptr;
    std::array<Glyph, kHandCharCount> Glyphs{};
    float LineHeight = 0;
};

struct TtfKerning {
    stbtt_fontinfo Info{};
    float Scale = 0;
    bool Ready = false;
};

struct ShaderVariant {
    SDL_GPUShaderFormat Format;
    const char* Extension;
    const char* EntryPoint;
};

constexpr std::array<ShaderVariant, 3> kShaderVariants{{
    {SDL_GPU_SHADERFORMAT_SPIRV, "spv", "main"},
    {SDL_GPU_SHADERFORMAT_DXIL, "dxil", "main"},
    {SDL_GPU_SHADERFORMAT_MSL, "msl", "main0"},
}};

struct ShaderDesc {
    const char* Name;
    SDL_GPUShaderStage Stage;
    Uint32 Samplers;
    Uint32 UniformBuffers;
};

constexpr int kAtlasSize = 512;
constexpr float kTtfPixelHeight = 48.0f;
constexpr float kHandScale = 4.0f;
constexpr int kMaxGlyphs = 128;
constexpr Uint32 kVertexBytes = kMaxGlyphs * 6 * sizeof(Vertex);
constexpr const char* kFontFile = "fonts/AtkinsonHyperlegible-Regular.ttf";

std::string BasePathFile(const char* relative)
{
    const char* base = SDL_GetBasePath();
    return std::string(base ? base : "") + relative;
}

SDL_GPUShader* LoadShader(SDL_GPUDevice* device, const ShaderDesc& desc)
{
    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(device);
    for (const ShaderVariant& variant : kShaderVariants) {
        if (!(supported & variant.Format))
            continue;
        const std::string path = BasePathFile("shaders/") + desc.Name + "." + variant.Extension;
        size_t size = 0;
        void* code = SDL_LoadFile(path.c_str(), &size);
        if (!code)
            continue;

        SDL_GPUShaderCreateInfo info{};
        info.code_size = size;
        info.code = static_cast<const Uint8*>(code);
        info.entrypoint = variant.EntryPoint;
        info.format = variant.Format;
        info.stage = desc.Stage;
        info.num_samplers = desc.Samplers;
        info.num_uniform_buffers = desc.UniformBuffers;

        SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
        SDL_free(code);
        if (!shader)
            SDL_Log("SDL_CreateGPUShader(%s): %s", path.c_str(), SDL_GetError());
        return shader;
    }
    SDL_Log("No usable shader variant for %s", desc.Name);
    return nullptr;
}

// One-shot upload: CPU pixels -> transfer buffer -> copy pass -> GPU texture.
SDL_GPUTexture* UploadR8(SDL_GPUDevice* device, const Uint8* pixels, int width, int height)
{
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(width);
    info.height = static_cast<Uint32>(height);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &info);

    const Uint32 byteCount = static_cast<Uint32>(width * height);
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = byteCount;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (!texture || !transfer) {
        SDL_Log("Atlas upload failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (!mapped) {
        SDL_Log("SDL_MapGPUTransferBuffer: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }
    std::memcpy(mapped, pixels, byteCount);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.pixels_per_row = static_cast<Uint32>(width);
    source.rows_per_layer = static_cast<Uint32>(height);
    SDL_GPUTextureRegion destination{};
    destination.texture = texture;
    destination.w = static_cast<Uint32>(width);
    destination.h = static_cast<Uint32>(height);
    destination.d = 1;
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return texture;
}

bool BakeTtfAtlas(SDL_GPUDevice* device, Atlas& atlas, TtfKerning& kerning,
                  std::vector<unsigned char>& ttfBytes)
{
    size_t fileSize = 0;
    const std::string path = BasePathFile(kFontFile);
    void* file = SDL_LoadFile(path.c_str(), &fileSize);
    if (!file) {
        SDL_Log("Could not load %s (%s)", path.c_str(), SDL_GetError());
        return false;
    }

    // stbtt_fontinfo points into these bytes for the rest of the program.
    ttfBytes.assign(static_cast<unsigned char*>(file),
                    static_cast<unsigned char*>(file) + fileSize);
    SDL_free(file);

    if (!stbtt_InitFont(&kerning.Info, ttfBytes.data(), 0)) {
        SDL_Log("stbtt_InitFont failed");
        return false;
    }
    kerning.Scale = stbtt_ScaleForPixelHeight(&kerning.Info, kTtfPixelHeight);
    kerning.Ready = true;

    std::vector<Uint8> pixels(static_cast<size_t>(kAtlasSize * kAtlasSize), 0);
    std::array<stbtt_bakedchar, kHandCharCount> baked{};
    const int rows = stbtt_BakeFontBitmap(ttfBytes.data(), 0, kTtfPixelHeight, pixels.data(),
                                           kAtlasSize, kAtlasSize, kHandFirstChar, kHandCharCount,
                                           baked.data());
    if (rows <= 0) {
        SDL_Log("stbtt_BakeFontBitmap did not fit ASCII 32-126 into the atlas");
        return false;
    }

    for (int i = 0; i < kHandCharCount; ++i) {
        const stbtt_bakedchar& src = baked[static_cast<size_t>(i)];
        Glyph& dst = atlas.Glyphs[static_cast<size_t>(i)];
        const float width = static_cast<float>(src.x1 - src.x0);
        const float height = static_cast<float>(src.y1 - src.y0);
        dst.XOff = src.xoff;
        dst.YOff = src.yoff;
        dst.Width = width;
        dst.Height = height;
        dst.XAdvance = src.xadvance;
        if (width > 0.0f && height > 0.0f) {
            // Half-texel inset so nearest sampling hits texel centers.
            dst.U0 = (static_cast<float>(src.x0) + 0.5f) / kAtlasSize;
            dst.V0 = (static_cast<float>(src.y0) + 0.5f) / kAtlasSize;
            dst.U1 = (static_cast<float>(src.x1) - 0.5f) / kAtlasSize;
            dst.V1 = (static_cast<float>(src.y1) - 0.5f) / kAtlasSize;
        }
    }

    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&kerning.Info, &ascent, &descent, &lineGap);
    atlas.LineHeight = std::ceil((ascent - descent + lineGap) * kerning.Scale);
    atlas.Texture = UploadR8(device, pixels.data(), kAtlasSize, kAtlasSize);
    SDL_Log("TTF atlas baked (%d rows used, line height %.0f)", rows, atlas.LineHeight);
    return atlas.Texture != nullptr;
}

bool BakeHandAtlas(SDL_GPUDevice* device, Atlas& atlas)
{
    constexpr int kColumns = 16;
    constexpr int kCell = 8;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kRows = (kHandCharCount + kColumns - 1) / kColumns;
    constexpr int kHeight = kRows * kCell;

    std::vector<Uint8> pixels(static_cast<size_t>(kWidth * kHeight), 0);
    for (int i = 0; i < kHandCharCount; ++i) {
        const int col = i % kColumns;
        const int row = i / kColumns;
        const int originX = col * kCell;
        const int originY = row * kCell;
        bool anyInk = false;
        for (int gy = 0; gy < kCell; ++gy) {
            const Uint8 bits = kHandGlyphs[i][gy];
            for (int gx = 0; gx < kCell; ++gx) {
                const bool on = (bits & static_cast<Uint8>(1u << (7 - gx))) != 0;
                if (!on)
                    continue;
                anyInk = true;
                pixels[static_cast<size_t>((originY + gy) * kWidth + (originX + gx))] = 255;
            }
        }

        Glyph& dst = atlas.Glyphs[static_cast<size_t>(i)];
        dst.XAdvance = kCell * kHandScale;
        dst.YOff = -kCell * kHandScale;
        if (!anyInk)
            continue;
        dst.Width = kCell * kHandScale;
        dst.Height = kCell * kHandScale;
        dst.U0 = (static_cast<float>(originX) + 0.5f) / kWidth;
        dst.V0 = (static_cast<float>(originY) + 0.5f) / kHeight;
        dst.U1 = (static_cast<float>(originX + kCell) - 0.5f) / kWidth;
        dst.V1 = (static_cast<float>(originY + kCell) - 0.5f) / kHeight;
    }

    atlas.LineHeight = (kCell + 2) * kHandScale;
    atlas.Texture = UploadR8(device, pixels.data(), kWidth, kHeight);
    SDL_Log("Hand atlas expanded (%dx%d, scale %.0f)", kWidth, kHeight, kHandScale);
    return atlas.Texture != nullptr;
}

void AppendQuad(std::vector<Vertex>& vertices, float x, float y, float w, float h, float u0,
                float v0, float u1, float v1)
{
    const Vertex quad[6] = {
        {x, y, u0, v0},         {x + w, y, u1, v0},         {x + w, y + h, u1, v1},
        {x, y, u0, v0},         {x + w, y + h, u1, v1},     {x, y + h, u0, v1},
    };
    vertices.insert(vertices.end(), quad, quad + 6);
}

// Left to right. penY is the baseline. Newlines move to the next line.
// Kerning is optional and only the TTF font has it.
void LayoutText(std::vector<Vertex>& vertices, const Atlas& atlas, const TtfKerning* kerning,
                float originX, float baseline, const char* text)
{
    float penX = originX;
    float penY = baseline;
    int previous = 0;
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        const unsigned char ch = static_cast<unsigned char>(*cursor);
        if (ch == '\n') {
            penX = originX;
            penY += atlas.LineHeight;
            previous = 0;
            continue;
        }
        if (ch < kHandFirstChar || ch >= kHandFirstChar + kHandCharCount)
            continue;
        if (vertices.size() + 6 > kMaxGlyphs * 6)
            break;

        if (kerning && kerning->Ready && previous != 0) {
            const int kern = stbtt_GetCodepointKernAdvance(&kerning->Info, previous, ch);
            penX += static_cast<float>(kern) * kerning->Scale;
        }

        const Glyph& glyph = atlas.Glyphs[ch - kHandFirstChar];
        // Snap the quad origin to a whole pixel so the glyph does not shimmer.
        const float x = std::floor(penX + glyph.XOff);
        const float y = std::floor(penY + glyph.YOff);
        if (glyph.Width > 0.0f && glyph.Height > 0.0f)
            AppendQuad(vertices, x, y, glyph.Width, glyph.Height, glyph.U0, glyph.V0, glyph.U1,
                       glyph.V1);
        penX += glyph.XAdvance;
        previous = ch;
    }
}

SDL_GPUGraphicsPipeline* CreateTextPipeline(SDL_GPUDevice* device, SDL_Window* window,
                                            SDL_GPUShader* vertexShader,
                                            SDL_GPUShader* fragmentShader)
{
    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = SDL_GetGPUSwapchainTextureFormat(device, window);
    colorTarget.blend_state.enable_blend = true;
    colorTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    colorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    colorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUVertexBufferDescription vertexBuffer{};
    vertexBuffer.slot = 0;
    vertexBuffer.pitch = sizeof(Vertex);
    vertexBuffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    const std::array<SDL_GPUVertexAttribute, 2> attributes{{
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, X)},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, U)},
    }};

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertexShader;
    info.fragment_shader = fragmentShader;
    info.vertex_input_state.vertex_buffer_descriptions = &vertexBuffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes.data();
    info.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(attributes.size());
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.target_info.color_target_descriptions = &colorTarget;
    info.target_info.num_color_targets = 1;

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    if (!pipeline)
        SDL_Log("SDL_CreateGPUGraphicsPipeline: %s", SDL_GetError());
    return pipeline;
}

struct ScreenUniforms {
    float Width, Height, Pad0, Pad1;
};

} // namespace

int main(int, char**)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("SDL3 GPU text", 960, 540, SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GPUDevice* device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, true,
        nullptr);
    if (!device || !SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_Log("GPU setup failed: %s", SDL_GetError());
        if (device)
            SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_Log("GPU driver: %s", SDL_GetGPUDeviceDriver(device));

    TtfKerning kerning{};
    std::vector<unsigned char> ttfBytes;
    Atlas ttfAtlas{};
    Atlas handAtlas{};
    if (!BakeTtfAtlas(device, ttfAtlas, kerning, ttfBytes) || !BakeHandAtlas(device, handAtlas)) {
        SDL_WaitForGPUIdle(device);
        SDL_ReleaseGPUTexture(device, ttfAtlas.Texture);
        SDL_ReleaseGPUTexture(device, handAtlas.Texture);
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GPUShader* vertexShader =
        LoadShader(device, {"Text.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1});
    SDL_GPUShader* fragmentShader =
        LoadShader(device, {"Text.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0});
    SDL_GPUGraphicsPipeline* pipeline =
        (vertexShader && fragmentShader) ? CreateTextPipeline(device, window, vertexShader, fragmentShader)
                                          : nullptr;
    SDL_ReleaseGPUShader(device, vertexShader);
    SDL_ReleaseGPUShader(device, fragmentShader);

    SDL_GPUBufferCreateInfo bufferInfo{};
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bufferInfo.size = kVertexBytes;
    SDL_GPUBuffer* vertexBuffer = SDL_CreateGPUBuffer(device, &bufferInfo);

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = kVertexBytes;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);

    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device, &samplerInfo);

    if (!pipeline || !vertexBuffer || !transfer || !sampler) {
        SDL_Log("Text pipeline setup failed: %s", SDL_GetError());
        SDL_WaitForGPUIdle(device);
        SDL_ReleaseGPUSampler(device, sampler);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUBuffer(device, vertexBuffer);
        SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        SDL_ReleaseGPUTexture(device, handAtlas.Texture);
        SDL_ReleaseGPUTexture(device, ttfAtlas.Texture);
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const char* framesEnv = SDL_getenv("GUIDE_FRAMES");
    const int maxFrames = framesEnv ? SDL_atoi(framesEnv) : 0;
    int frame = 0;
    bool running = true;
    bool useHandFont = false;
    bool tWasDown = false;
    Uint64 lastTicks = SDL_GetTicksNS();
    float frameMs = 0.0f;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
        }

        const bool* keys = SDL_GetKeyboardState(nullptr);
        const bool tDown = keys[SDL_SCANCODE_T];
        if (tDown && !tWasDown)
            useHandFont = !useHandFont;
        tWasDown = tDown;

        const Uint64 nowTicks = SDL_GetTicksNS();
        const float elapsedMs = static_cast<float>(nowTicks - lastTicks) / 1.0e6f;
        lastTicks = nowTicks;
        if (elapsedMs > 0.0f && elapsedMs < 1000.0f)
            frameMs = elapsedMs;
        const float fps = frameMs > 0.0f ? 1000.0f / frameMs : 0.0f;

        char message[128];
        std::snprintf(message, sizeof(message), "SDL3 GPU\n%s   %.0f fps   %.1f ms",
                      useHandFont ? "HEX" : "TTF", fps, frameMs);

        const Atlas& atlas = useHandFont ? handAtlas : ttfAtlas;
        const TtfKerning* kerningPtr = useHandFont ? nullptr : &kerning;
        std::vector<Vertex> vertices;
        vertices.reserve(64 * 6);
        LayoutText(vertices, atlas, kerningPtr, 48.0f, 80.0f, message);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (!cmd) {
            SDL_Log("SDL_AcquireGPUCommandBuffer: %s", SDL_GetError());
            break;
        }

        SDL_GPUTexture* swapchain = nullptr;
        Uint32 width = 0;
        Uint32 height = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swapchain, &width, &height)) {
            SDL_Log("SDL_WaitAndAcquireGPUSwapchainTexture: %s", SDL_GetError());
            SDL_CancelGPUCommandBuffer(cmd);
            break;
        }

        if (swapchain && !vertices.empty() && width > 0 && height > 0) {
            const Uint32 byteCount = static_cast<Uint32>(vertices.size() * sizeof(Vertex));
            void* mapped = SDL_MapGPUTransferBuffer(device, transfer, true);
            if (mapped) {
                std::memcpy(mapped, vertices.data(), byteCount);
                SDL_UnmapGPUTransferBuffer(device, transfer);

                SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
                SDL_GPUTransferBufferLocation source{};
                source.transfer_buffer = transfer;
                SDL_GPUBufferRegion destination{};
                destination.buffer = vertexBuffer;
                destination.size = byteCount;
                // cycle = true: this buffer is rewritten every frame.
                SDL_UploadToGPUBuffer(copy, &source, &destination, true);
                SDL_EndGPUCopyPass(copy);
            }

            const ScreenUniforms uniforms{static_cast<float>(width), static_cast<float>(height), 0.0f,
                                          0.0f};
            SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));

            SDL_GPUColorTargetInfo target{};
            target.texture = swapchain;
            target.clear_color = SDL_FColor{0.08f, 0.09f, 0.12f, 1.0f};
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;

            SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            const SDL_GPUBufferBinding vertexBinding{vertexBuffer, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
            const SDL_GPUTextureSamplerBinding textureBinding{atlas.Texture, sampler};
            SDL_BindGPUFragmentSamplers(pass, 0, &textureBinding, 1);
            SDL_DrawGPUPrimitives(pass, static_cast<Uint32>(vertices.size()), 1, 0, 0);
            SDL_EndGPURenderPass(pass);
        }

        SDL_SubmitGPUCommandBuffer(cmd);

        if (maxFrames > 0 && ++frame >= maxFrames)
            running = false;
    }

    SDL_WaitForGPUIdle(device);
    SDL_ReleaseGPUSampler(device, sampler);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUBuffer(device, vertexBuffer);
    SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    SDL_ReleaseGPUTexture(device, handAtlas.Texture);
    SDL_ReleaseGPUTexture(device, ttfAtlas.Texture);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
