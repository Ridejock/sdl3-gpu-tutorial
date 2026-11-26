// main.cpp - minimal SDL3 GPU (SDL_gpu) textured quad, C++20.
// Build: see docs/guide.md. Shaders are loaded from shaders/Quad.{vert,frag}.{spv,dxil,msl}
// next to the executable.
#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstring>
#include <span>
#include <string>

namespace {

struct Vertex {
    float X, Y; // position, location 0
    float U, V; // texcoord, location 1
};

constexpr std::array<Vertex, 4> kQuadVertices{{
    {-0.5f, -0.5f, 0.0f, 1.0f},
    {0.5f, -0.5f, 1.0f, 1.0f},
    {0.5f, 0.5f, 1.0f, 0.0f},
    {-0.5f, 0.5f, 0.0f, 0.0f},
}};
constexpr std::array<Uint16, 6> kQuadIndices{0, 1, 2, 0, 2, 3};

// One shader variant per backend. SPIRV-Cross renames `main` to `main0` in MSL.
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

// Resource counts MUST match what the shader declares, or pipeline creation/draws fail.
struct ShaderDesc {
    const char* Name; // e.g. "Quad.vert"
    SDL_GPUShaderStage Stage;
    Uint32 Samplers;
    Uint32 UniformBuffers;
};

SDL_GPUShader* LoadShader(SDL_GPUDevice* device, const ShaderDesc& desc)
{
    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(device);
    const char* base = SDL_GetBasePath(); // folder of the executable (may be null)

    for (const ShaderVariant& variant : kShaderVariants) {
        if (!(supported & variant.Format))
            continue;
        const std::string path =
            std::string(base ? base : "") + "shaders/" + desc.Name + "." + variant.Extension;

        size_t size = 0;
        void* code = SDL_LoadFile(path.c_str(), &size);
        if (!code)
            continue; // not shipped for this backend, try the next one

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

// CPU -> transfer buffer -> copy pass -> GPU buffer. Returns null on failure.
SDL_GPUBuffer* CreateBufferWithData(SDL_GPUDevice* device, SDL_GPUBufferUsageFlags usage,
                                    std::span<const std::byte> bytes)
{
    const auto size = static_cast<Uint32>(bytes.size());

    SDL_GPUBufferCreateInfo bufferInfo{};
    bufferInfo.usage = usage;
    bufferInfo.size = size;
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device, &bufferInfo);

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (!buffer || !transfer) {
        SDL_Log("Buffer creation failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }

    // cycle = false: this transfer buffer is brand new, nothing on the GPU is reading it.
    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    std::memcpy(mapped, bytes.data(), size);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation source{};
    source.transfer_buffer = transfer;
    SDL_GPUBufferRegion destination{};
    destination.buffer = buffer;
    destination.size = size;
    SDL_UploadToGPUBuffer(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(device, transfer); // SDL frees it once the upload is done
    return buffer;
}

// Upload tightly packed RGBA8 pixels into a sampleable 2D texture.
SDL_GPUTexture* CreateTextureRGBA8(SDL_GPUDevice* device, Uint32 width, Uint32 height,
                                   std::span<const Uint8> pixels)
{
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &info);

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(pixels.size());
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (!texture || !transfer) {
        SDL_Log("Texture creation failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    std::memcpy(mapped, pixels.data(), pixels.size());
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.pixels_per_row = width; // rows are tightly packed
    source.rows_per_layer = height;
    SDL_GPUTextureRegion destination{};
    destination.texture = texture;
    destination.w = width;
    destination.h = height;
    destination.d = 1;
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return texture;
}

SDL_GPUGraphicsPipeline* CreateQuadPipeline(SDL_GPUDevice* device, SDL_Window* window,
                                            SDL_GPUShader* vertexShader,
                                            SDL_GPUShader* fragmentShader)
{
    // The pipeline must render into the same format as the swapchain.
    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = SDL_GetGPUSwapchainTextureFormat(device, window);

    SDL_GPUVertexBufferDescription vertexBuffer{};
    vertexBuffer.slot = 0;
    vertexBuffer.pitch = sizeof(Vertex);
    vertexBuffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    const std::array<SDL_GPUVertexAttribute, 2> attributes{{
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, X)}, // TEXCOORD0
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, U)}, // TEXCOORD1
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

// 8x8 checkerboard, generated so the sample needs no image loader.
std::array<Uint8, 8 * 8 * 4> MakeCheckerboard()
{
    std::array<Uint8, 8 * 8 * 4> pixels{};
    for (Uint32 y = 0; y < 8; ++y) {
        for (Uint32 x = 0; x < 8; ++x) {
            const bool light = ((x + y) % 2) == 0;
            Uint8* p = &pixels[(y * 8 + x) * 4];
            p[0] = light ? 0xE8 : 0x2E; // R
            p[1] = light ? 0xC8 : 0x8B; // G
            p[2] = light ? 0x5A : 0x57; // B
            p[3] = 0xFF;                // A
        }
    }
    return pixels;
}

// Uniform block pushed every frame. 16 bytes = one float4 (std140-friendly).
struct QuadUniforms {
    float Cos, Sin, InvAspect, Unused;
};

} // namespace

int main(int, char**)
{
    // 1. SDL + window
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("SDL3 GPU quad", 960, 540, SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // 2. GPU device: list every bytecode format we ship; SDL picks a backend that takes one.
    SDL_GPUDevice* device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL,
        true /* debug mode: validation messages while learning */, nullptr /* any driver */);
    if (!device || !SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_Log("GPU setup failed: %s", SDL_GetError());
        if (device)
            SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_Log("GPU driver: %s", SDL_GetGPUDeviceDriver(device));

    // 3. Shaders -> pipeline. Shaders can be released once the pipeline exists.
    SDL_GPUShader* vertexShader =
        LoadShader(device, {"Quad.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1});
    SDL_GPUShader* fragmentShader =
        LoadShader(device, {"Quad.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0});
    SDL_GPUGraphicsPipeline* pipeline =
        (vertexShader && fragmentShader)
            ? CreateQuadPipeline(device, window, vertexShader, fragmentShader)
            : nullptr;
    SDL_ReleaseGPUShader(device, vertexShader); // SDL ignores null here
    SDL_ReleaseGPUShader(device, fragmentShader);

    // 4. Static data: vertex/index buffers, texture, sampler.
    SDL_GPUBuffer* vertexBuffer = CreateBufferWithData(device, SDL_GPU_BUFFERUSAGE_VERTEX,
                                                       std::as_bytes(std::span(kQuadVertices)));
    SDL_GPUBuffer* indexBuffer = CreateBufferWithData(device, SDL_GPU_BUFFERUSAGE_INDEX,
                                                      std::as_bytes(std::span(kQuadIndices)));
    const auto checker = MakeCheckerboard();
    SDL_GPUTexture* texture = CreateTextureRGBA8(device, 8, 8, checker);

    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST; // crisp pixels
    samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device, &samplerInfo);

    bool running = pipeline && vertexBuffer && indexBuffer && texture && sampler;

    // Optional: GUIDE_FRAMES=N quits after N frames (handy for automated smoke tests).
    const char* framesEnv = SDL_getenv("GUIDE_FRAMES");
    const int maxFrames = framesEnv ? SDL_atoi(framesEnv) : 0;
    int frame = 0;

    // 5. Frame loop
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
        }

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
            SDL_CancelGPUCommandBuffer(cmd); // allowed: nothing was acquired
            break;
        }

        // Null swapchain is NOT an error (minimized/occluded window): skip drawing, still submit.
        if (swapchain) {
            const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
            const QuadUniforms uniforms{std::cos(t), std::sin(t),
                                        static_cast<float>(height) / static_cast<float>(width),
                                        0.0f};
            // Vertex uniform slot 0 == register(b0, space1) in Quad.vert.hlsl.
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
            const SDL_GPUBufferBinding indexBinding{indexBuffer, 0};
            SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

            // Fragment sampler slot 0 == register(t0/s0, space2) in Quad.frag.hlsl.
            const SDL_GPUTextureSamplerBinding textureBinding{texture, sampler};
            SDL_BindGPUFragmentSamplers(pass, 0, &textureBinding, 1);

            SDL_DrawGPUIndexedPrimitives(pass, static_cast<Uint32>(kQuadIndices.size()), 1, 0, 0,
                                         0);
            SDL_EndGPURenderPass(pass);
        }

        SDL_SubmitGPUCommandBuffer(cmd); // also presents the swapchain texture

        if (maxFrames > 0 && ++frame >= maxFrames)
            running = false;
    }

    // 6. Cleanup: wait for the GPU, release resources, then unclaim, destroy device, window.
    SDL_WaitForGPUIdle(device);
    SDL_ReleaseGPUSampler(device, sampler);
    SDL_ReleaseGPUTexture(device, texture);
    SDL_ReleaseGPUBuffer(device, indexBuffer);
    SDL_ReleaseGPUBuffer(device, vertexBuffer);
    SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
