# Series outline: Asteroids on SDL3 GPU

**Part 1** is this guide ([guide.md](guide.md)): window → device → pipeline → textured quad. **Part 2** is text, immediately after, because a glyph is just that quad. **Part 3** is palettes, because that quad and a 1-bit glyph are already indexed images. The parts after Part 1 are planned. They will be released gradually, one part at a time. There are no dates.

The series then builds a complete classic vector Asteroids on top of that exact setup. Everything stays **standalone SDL3**: plain C++20, SDL3 core only, and no engine. Audio uses SDL3 core audio streams with a tiny custom mixer, **never SDL_mixer**.

Reference (for the author, optional for readers): Ervin's public [Ridejock/Asteroids](https://github.com/Ridejock/Asteroids), which is built on the Emerald engine. Useful files to cross-check behavior: `src/Shared/Ship.cpp`, `Asteroid.cpp`, `Bullet.h`, `Playfield.h`, `Saucer.cpp`, `ParticleEffects.cpp`, `HighScores.cpp`. The tutorial code is written fresh and doesn't depend on Emerald.

Each part ends with a runnable build, a short "what changed" diff, and a 1-post X summary.

| # | Part | New SDL GPU / C++ ideas | Result on screen |
| --- | --- | --- | --- |
| 1 | **Textured quad** | This guide: window, GPU device, pipeline, one spinning checkerboard quad | A spinning checkerboard quad |
| 2 | **Text: a font atlas** | A glyph is that same quad. Recommended: bake glyphs with stb_truetype. Optional old-school path: a hand-drawn bitmap font. Upload one atlas, draw glyph quads with alpha blend and pixel snapping. See below. | Your own words on screen |
| 3 | **Palettes** | An index grid plus a color table. Expand to RGBA on the CPU, or look the index up in a palette texture in the fragment shader. Nearest sampling. See below. | One sprite in two color schemes |
| 4 | **Vector ship as a line list** | `SDL_GPU_PRIMITIVETYPE_LINELIST` pipeline, colour-only shader (no texture), shape data as a `constexpr std::array` of points, one model→clip transform pushed as a uniform | A white triangle ship in the centre |
| 5 | **Fixed timestep, input, thrust, rotation, wrap** | `SDL_GetTicksNS` fixed 120 Hz update + interpolation, `SDL_GetKeyboardState` vs events, a key-binding table (data, not `if` chains), velocity/drag, screen wrap with `std::fmod` | Ship flies, rotates, wraps around the edges |
| 6 | **Dynamic vertex buffer** | Rebuild a per-frame line buffer: transfer buffer mapped with **`cycle = true`**, a single upload per frame, one draw call for all lines | Many shapes drawn with one buffer/draw |
| 7 | **Bullets** | Fixed-size pool (`std::array` + alive flags), lifetime, fire cooldown, wrap | Ship shoots |
| 8 | **Asteroids + splitting** | Data table for sizes (large → 2 medium → 2 small: radius, speed, score), seeded RNG (`std::mt19937`), jagged polygons generated once per rock | Rocks drift and split when hit |
| 9 | **Collisions** | Circle-vs-circle broad checks, wrap-aware distance, ship death + respawn invulnerability blink | Bullets break rocks; rocks kill the ship |
| 10 | **Score, lives, waves, UI** | Game state machine (title / playing / game over), wave spawner, HUD strings drawn with the Part 2 atlas | HUD, waves, title + game over screens |
| 11 | **Audio: core audio streams + tiny mixer** | `SDL_OpenAudioDeviceStream`, a callback that mixes N voices (float, clamp), procedurally generated SFX (thrust noise, fire blip, explosion), **no SDL_mixer** | Classic beeps and booms |
| 12 | **Juice** | Particle debris as lines, screen shake (camera offset uniform), additive blend glow pipeline | It *feels* like Asteroids |
| 13 | **Polish + ship it** | Saucers (optional), high-score file via `SDL_GetPrefPath`, packaging a release zip (exe + shaders folder) | A shareable build |

## Part 2: drawing text

A glyph is just a textured quad, the same one from Part 1. Beginners love seeing their own text early, and every later part reuses this: score, lives, and a debug overlay. This part is only about drawing text.

**Where the glyphs come from.** The recommended path is [stb_truetype](https://github.com/nothings/stb/blob/master/stb_truetype.h), one public-domain header. You hand it an OFL or CC0 TTF, and it rasterizes the characters you ask for. That is the modern standard for this series, and it is what Part 10 and the later HUD text build on.

### Old-school option: hand-drawn bitmap font

This is a fun retro approach, not the modern standard. Try it if you want a font that is entirely yours, with no font file and no license to track. For the HUD, stick with stb_truetype and an atlas.

Years ago the author would take graph paper to a coffee shop, map a font pixel by pixel, and pack each row of pixels into bits for OpenGL's `glBitmap`. He wrote those rows in hex, the way the OpenGL Red Book does. The same idea still works, and the draw at the end is the one you already know:

1. Draw an 8×8 glyph on graph paper (5×7 works too). A filled square is a 1, an empty square is a 0. The leftmost square is the high bit.
2. Write each row as one hex byte, with the bits in a comment so you can check the row against the paper. A glyph is a `const uint8_t` array, eight bytes for an 8×8 character.
3. At startup, walk those bits and expand them into an R8 or RGBA atlas. A 1 becomes an opaque texel, and a 0 stays transparent.
4. Upload that atlas and draw it with the same glyph-quad path as the TTF atlas.

An 8×8 "A", in the same hex style. Read each comment left to right and it is the graph-paper row:

```cpp
// 8x8 "A". Leftmost square is bit 7.
constexpr uint8_t kGlyphA[8] = {
    0x3C, // 0b00111100
    0x66, // 0b01100110
    0xC3, // 0b11000011
    0xC3, // 0b11000011
    0xFF, // 0b11111111
    0xC3, // 0b11000011
    0xC3, // 0b11000011
    0x00, // 0b00000000
};
```

The pixels are original, so the font is license-free.

Bake whichever source you picked into **one texture atlas** (a single image of glyphs). Keep a tiny table next to it: for each character, the rectangle inside the atlas, how far the next character should start (the advance), and where the glyph sits relative to the baseline (the bearing).

**Onto the GPU.** Upload that atlas the same way as the checkerboard: CPU pixels, a transfer buffer, a copy pass, a GPU texture. Then draw one textured quad per glyph with the pipeline you already have. A string is a row of those quads.

**Alpha blending.** Turn it on for the glyph pipeline. The edge of a glyph fades out instead of drawing a hard rectangle, which is what makes it look like text.

**Left to right.** Start a pen at the origin. Place a glyph, then move the pen by that glyph's advance. Kerning is a small extra nudge between some pairs (an "A" and a "V" want to sit closer than their advances say). stb_truetype can give you that nudge. A bitmap font can skip kerning at first and still look fine.

**Keep it crisp.** Snap the pen to whole pixels before you draw. If a HUD glyph sits on a fractional pixel it shimmers as it moves. HUD text is snapped. Text that lives in the world can stay smooth.

**SDL_ttf is a real option, and we don't use it here.** SDL_ttf will rasterize for you, and it is a good library. The atlas is a few dozen lines on top of the upload and the draw you already know, it links nothing extra, and you can see every byte that reaches the GPU. Once that path is obvious, SDL_ttf is easy to pick up. The sample stays on SDL3 core.

**Fonts you can ship.** A TTF has to be one you are allowed to redistribute: an [OFL](https://openfontlicense.org/) font (SIL Open Font License) or a CC0 font, with its license file kept next to the font. Don't drop in a font from the operating system, and don't use a commercial face. The hand-drawn bitmap font above is original pixels, so it is license-free.

## Part 3: palettes

A palette image, also called an indexed-color image, is two small pieces of data. The picture is a grid of index numbers, one per pixel: "use color 0", "use color 3", and so on. The palette is the color table those numbers point at: index 0 is this RGBA, index 1 is that RGBA. The color lives only in the table.

You have already drawn two of these. The Part 1 checkerboard is a 2-color palette: each cell is the light index or the dark index, and those two colors live in one table. A hand-drawn 1-bit glyph from Part 2 is the same thing. A filled square is index 1 (ink). An empty square is index 0 (paper, or transparent).

Courses skip palettes now that photos are stored as RGBA. They are still useful while you build a game:

- **Variants and teams.** One ship picture, two palettes. The second enemy is the same pixels with another table.
- **Hit flashes.** For a few frames, use a palette whose entries are white or red. Then bind the normal table again.
- **Recolor in one edit.** Change three entries and the whole sprite follows. You do not repaint every pixel.
- **Day, night, mood.** One global table tints every sprite that shares it.
- **Tiny memory.** The picture is indexes. The colors are a handful of bytes.
- **Programmer art.** Invent the table in code and paint indexes. No image file, and the placeholder is easy to throw away later.

Keep the palette as data, a `constexpr` array of colors. Drawing code names an index. It does not paste `0xE8, 0xC8, 0x5A` into three different functions. Edit the table, and every picture that uses it changes with it.

**On the GPU.** Sample with **nearest** filtering in both approaches below. A linear filter blends neighboring indexes or neighboring palette entries into colors that were never in the table.

1. **Expand on the CPU (simplest).** At load time, walk the index grid and write RGBA pixels. Upload that texture the same way as the checkerboard: transfer buffer, copy pass. A new color scheme means expand again and upload again. That is enough for a few variants.
2. **Look up in the shader (live swaps).** Upload the indexes as an R8 texture, one byte per pixel, and the palette as a second tiny texture, one texel per entry. The fragment shader reads the index, then reads the palette texture at that index. Swap schemes by binding another palette texture, or by rewriting those few texels. The sprite pixels stay where they are.

## Running rules for the series

- Keep it data-driven: shapes, rock sizes, key bindings, palette entries, and SFX parameters live in tables, not scattered constants.
- One pipeline per primitive and blend combo. Few draws, big buffers.
- Every API name is checked against wiki.libsdl.org/SDL3 before publishing.
- Every part compiles with warnings on and runs on Vulkan, D3D12, and Metal (shaders via shadercross).
- No SDL satellite libraries in the sample (no SDL_mixer, SDL_image, or SDL_ttf). Text is the atlas from Part 2, not SDL_ttf. Audio is the tiny mixer, not SDL_mixer. SDL_ttf is named in Part 2 so readers know the alternative.

## Open questions

- Host: the code for each part as a tag in a small public repo, or as gists?
- Include the saucer in Part 13, or leave it as an "exercise" post?
