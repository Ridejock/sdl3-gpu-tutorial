# Series outline: Asteroids on SDL3 GPU

**Part 0** is the intro guide ([guide.md](guide.md)): window → device → pipeline → textured quad. The parts below are planned. There is no schedule yet.
The series then builds a complete classic vector Asteroids on top of that exact setup. Everything stays **standalone SDL3**: plain C++20, SDL3 core only, and no engine. Audio uses SDL3 core audio streams with a tiny custom mixer, **never SDL_mixer**.

Reference (for the author, optional for readers): Ervin's public [Ridejock/Asteroids](https://github.com/Ridejock/Asteroids), which is built on the Emerald engine. Useful files to cross-check behavior: `src/Shared/Ship.cpp`, `Asteroid.cpp`, `Bullet.h`, `Playfield.h`, `Saucer.cpp`, `ParticleEffects.cpp`, `HighScores.cpp`. The tutorial code is written fresh and doesn't depend on Emerald.

Each part ends with a runnable build, a short "what changed" diff, and a 1-post X summary.

| # | Part | New SDL GPU / C++ ideas | Result on screen |
| --- | --- | --- | --- |
| 1 | **Vector ship as a line list** | `SDL_GPU_PRIMITIVETYPE_LINELIST` pipeline, colour-only shader (no texture), shape data as a `constexpr std::array` of points, one model→clip transform pushed as a uniform | A white triangle ship in the centre |
| 2 | **Fixed timestep, input, thrust, rotation, wrap** | `SDL_GetTicksNS` fixed 120 Hz update + interpolation, `SDL_GetKeyboardState` vs events, a key-binding table (data, not `if` chains), velocity/drag, screen wrap with `std::fmod` | Ship flies, rotates, wraps around the edges |
| 3 | **Dynamic vertex buffer** | Rebuild a per-frame line buffer: transfer buffer mapped with **`cycle = true`**, a single upload per frame, one draw call for all lines | Many shapes drawn with one buffer/draw |
| 4 | **Bullets** | Fixed-size pool (`std::array` + alive flags), lifetime, fire cooldown, wrap | Ship shoots |
| 5 | **Asteroids + splitting** | Data table for sizes (large → 2 medium → 2 small: radius, speed, score), seeded RNG (`std::mt19937`), jagged polygons generated once per rock | Rocks drift and split when hit |
| 6 | **Collisions** | Circle-vs-circle broad checks, wrap-aware distance, ship death + respawn invulnerability blink | Bullets break rocks; rocks kill the ship |
| 7 | **Score, lives, waves, UI** | A tiny vector font (segment table → lines, same pipeline), game state machine (title / playing / game over), wave spawner | HUD, waves, title + game over screens |
| 8 | **Audio: core audio streams + tiny mixer** | `SDL_OpenAudioDeviceStream`, a callback that mixes N voices (float, clamp), procedurally generated SFX (thrust noise, fire blip, explosion), **no SDL_mixer** | Classic beeps and booms |
| 9 | **Juice** | Particle debris as lines, screen shake (camera offset uniform), additive blend glow pipeline | It *feels* like Asteroids |
| 10 | **Polish + ship it** | Saucers (optional), high-score file via `SDL_GetPrefPath`, packaging a release zip (exe + shaders folder) | A shareable build |

## Running rules for the series

- Keep it data-driven: shapes, rock sizes, key bindings, and SFX parameters live in tables, not scattered constants.
- One pipeline per primitive and blend combo. Few draws, big buffers.
- Every API name is checked against wiki.libsdl.org/SDL3 before publishing.
- Every part compiles with warnings on and runs on Vulkan, D3D12, and Metal (shaders via shadercross).
- No SDL satellite libraries (no SDL_mixer, SDL_image, or SDL_ttf), so readers learn the core API.

## Open questions

- Cadence: one part per week (fits the Saturday update rhythm), or batches?
- Host: the code for each part as a tag in a small public repo, or as gists?
- Include the saucer in Part 10, or leave it as an "exercise" post?
