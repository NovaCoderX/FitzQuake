# FitzQuake (Amiga 68k) — notes for Claude

FitzQuake 0.80 (John Fitzgibbons, SDL port by Kristian Duske) ported to Amiga
68k RTG on top of the owner's AmiQuake SDL port. It renders through NovaMesa
(software OpenGL, Mesa 4.1, statically linked) and SDL 1.2. The practical
target is a PiStorm 4 (Emu68) at 320x240, 32-bit colour.

The three repositories belong together:

- `NovaCoderX/FitzQuake` — this one. All game source is in `src/`, flat.
- `NovaCoderX/NovaMesa` — the GL library. Its `CLAUDE.md` explains which GL
  state selects which rasteriser path; read it before touching any GL state
  here.
- `NovaCoderX/SDL-NovaMesa` — SDL 1.2 for Amiga 68k (AHI audio, Picasso96
  video, `SDL_GL_SwapBuffers` -> `amesa_swap_buffers`).

The owner cross-compiles with gcc 13 from Eclipse CDT. There is no Makefile.
The compiler flags are presumably the ones listed in NovaMesa's `CLAUDE.md`
(`-O2 -m68060 -noixemul -fno-strict-aliasing ...`); that has not been
confirmed for this project.

## Ground rules

- Changes are marked with a `NOVA:` (or `//NOVA --`) comment saying why. Older
  port changes are marked `NOVA_CODER`.
- Line endings are mixed: about half the files in `src/` are CRLF
  (`host.c`, `common.c`, `gl_rmain.c`, `gl_draw.c`, ...), the rest LF
  (`r_world.c`, `r_brush.c`, `r_alias.c`, `gl_vidsdl.c`, ...). Preserve what a
  file uses. Editing a CRLF file with a script that reads in text mode will
  silently convert it.
- The code assumes 32-bit pointers (pointers are stored in `int`/`unsigned`
  in the texture manager and the QuakeC VM). Byte order is detected at run
  time in `COM_Init`, so the same source runs on little-endian hosts.
- **Measure before optimising, and measure the whole frame.** The profile
  below shows the engine's own code is about 1% of a frame. A change to
  FitzQuake only matters if it changes which NovaMesa path is used or how many
  pixels are drawn. Use `test/hostbench/`.
- The owner's bar is "no drop in visual quality". Say explicitly whether a
  change is pixel-identical, and if not, by how much.

## Layout

Platform layer:

- `amiga_main.c` — entry point, stack check, Workbench tooltypes -> argv.
- `sys_sdl.c`, `sys_file_amiga.c` — timing (`SDL_GetTicks`), event pump, file
  access, log file.
- `gl_vidsdl.c` — mode setting, GL initial state (`GL_SetupState`), swap. 32bpp
  only; video settings live in `<gamedir>/video_config.cfg`, not `config.cfg`.
- `in_sdl.c`, `keys.c` — input. `snd_sdl.c`, `cd_sdl.c` — sound effects and
  music through SDL_mixer (music is `<gamedir>/music/trackNN.ogg`).

Renderer:

- `gl_rmain.c` — frame setup, entity loop, `R_EntityMatrix` (NOVA).
- `r_world.c` — PVS marking, texture chains, world passes.
- `r_brush.c` — brush entities, lightmap building and upload.
- `r_alias.c` — alias (MDL) models. `gl_sky.c`, `gl_warp.c`, `r_part.c`,
  `gl_rlight.c`, `r_sprite.c`, `gl_fog.c` — as named.
- `gl_texmgr.c` — FitzQuake's texture manager, `GL_Bind`, texture unit
  switching, `gl_texturemode`.
- `gl_draw.c`, `gl_screen.c`, `sbar.c`, `view.c` — 2D, screen update, status
  bar, view blends (`V_PolyBlend`).

Settings this port changes from stock FitzQuake: `gl_texturemode` defaults to
`GL_NEAREST_MIPMAP_NEAREST`, `r_oldwater 1`, `gl_subdivide_size 256`,
`gl_flashblend 1`, perspective hint `GL_FASTEST`, default mode 320x240, the
72 fps cap is removed, `sndspeed 22050`.

## Which NovaMesa path each pass uses (default settings)

| Pass | GL state | NovaMesa triangle function |
|---|---|---|
| World, brush entities | unit 0 REPLACE (RGB, mipmapped nearest) + unit 1 lightmap (RGB, LINEAR), COMBINE modulate x2 | `dualtex_triangle` — integer fast path |
| Sky, flat pre-pass | untextured, flat | driver direct fill |
| Sky, cloud layers | unit 0 REPLACE + unit 1 DECAL (RGBA), blend on, depth GEQUAL | `multitextured_triangle` — general float path |
| Alias model, no fullbright pixels | one texture, COMBINE modulate x2 | `general_textured_triangle` — general float path |
| Alias model with fullbright mask | unit 0 COMBINE x2 + unit 1 DECAL, blend on | `multitextured_triangle` — general float path |
| World fullbright ("glow") pass | one RGBA texture, blend | `persp_textured_triangle` + one-pass blend |
| Water (`r_oldwater 1`), particles, sprites, 2D | one texture, not mipmapped | `affine_textured_triangle` |
| View blend (`V_PolyBlend`), dlight blobs | untextured, blend | `flat_rgba_triangle` / `smooth_rgba_triangle` through the generic span code |

## Where the time goes (measured)

Method: the engine built for the host as a 32-bit program against NovaMesa,
with a stub SDL (`test/hostbench/`, below), `timedemo` on the shareware demos
at 320x240 with default settings, counted with callgrind. Instruction counts
are deterministic; they are x86 counts, not 68k, so trust proportions and
direction rather than absolute numbers. Float-heavy paths cost relatively more
on a 68k than these numbers show, and far more under a software FPU (FS-UAE,
qemu).

`timedemo demo1`: 969 frames, 30.87 G instructions, about 32 M per frame.

Self cost by component:

| Component | Share |
|---|---|
| NovaMesa | 96.5% |
| FitzQuake (of which 1.2% is level loading) | 2.3% |
| libc | 1.2% |

By pass (inclusive, demo1 / demo2 / demo3):

| Pass | demo1 | demo2 | demo3 |
|---|---|---|---|
| World (`R_DrawWorld`) | 72.4% | 75.6% | 74.4% |
| View blend (`V_PolyBlend`) | 6.1% | 7.9% | 3.3% |
| Brush entities | 5.6% | 3.9% | 7.3% |
| View model (weapon) | 5.2% | 3.2% | 5.9% |
| Other alias models | 4.6% | 3.5% | 4.5% |
| Status bar | 1.1% | 0.8% | 0.6% |
| Dlight blobs | 0.8% | 0.8% | 1.2% |
| Water, particles | under 1% | | |

Inside NovaMesa, one loop dominates: the `DUALTEX_LOOP(NEAREST, LINEAR)` inner
loop in `dualtex_span` (world texture nearest + bilinear lightmap + modulate)
is **41% of all instructions**, and `dualtex_triangle` as a whole is 63%. After
it, the generic span code that every dualtex span still goes through
(`write_rgba_span`, `depth_test_span`, `_mesa_write_rgba_span`,
`_mesa_span_interpolate_z`) is about 13.5%.

World overdraw, counted by instrumenting `dualtex_span` over demo1: 85.3 M
pixels rasterised; 15.5% thrown away by the existing whole-span depth reject;
of the 72.1 M that were textured only 3.7% were hidden when drawn. The view is
61.4 K pixels (320x192 with the status bar), so the world textures about 1.2
pixels per visible pixel.

Not covered by these numbers, because the harness stubs them out or demos do
not run them: the server and QuakeC VM during real play, sound mixing and OGG
decoding (both run in SDL's audio task), and the swap (`p96WritePixelArray`
of the whole window every frame). Measure those on the PiStorm itself:
compare `timedemo` with and without `-nosound` and `-nocdaudio`.

### Experiments, demo1 unless noted

Change in total instructions against the default configuration:

| Change | Result | Visual effect |
|---|---|---|
| `r_fullbright 1` (world with one texture) | -47.6% | no lighting; shows what the lightmap unit costs in total |
| lightmaps sampled `GL_NEAREST` (scratch patch) | -22.3% | blocky lighting; this is the ceiling for any lightmap-filter optimisation |
| NovaMesa prototype: filter lightmap every 4th pixel, see below | -6.4% | max difference 3/255 |
| same, wider threshold | -8.6% | max difference 5/255 |
| `gl_polyblend 0` | -6.1% | no damage/pickup/underwater tint |
| `gl_flashblend 0` | -2.9% | better: real dynamic lights instead of orange blobs |
| `gl_fullbrights 0` | -1.8% | fullbright pixels become lit |
| `gl_overbright_models 0` | -1.4% | models lose overbright |
| drop the no-op `glEnable(GL_BLEND)` on fullbright-mask models | -0.13% | none |
| perspective hint `GL_NICEST` instead of `GL_FASTEST` | +0.2% | better: no affine warping |
| `r_drawviewmodel 0` | -5.2% | no weapon |
| `viewsize 120` (no status bar, 25% more 3D pixels) | +25% wall clock | |
| `-nomtex` (two-pass world) | +12% wall clock | none |
| `gl_texturemode GL_LINEAR_MIPMAP_NEAREST` | +24% wall clock | filtered textures |
| `gl_texturemode GL_LINEAR_MIPMAP_LINEAR` | 5.2x slower wall clock | trilinear |

Sky, `map e1m5` looking straight up (`+lookup`), sky about 45% of the view,
137 frames:

| Variant | `Sky_DrawSky` | Whole render | Output |
|---|---|---|---|
| as is | 17.3 M per frame, 61% of the render | | |
| state set once, one `glBegin`, no blend | -6.8% | -4.2% | byte-identical |
| two passes (solid, then blended alpha layer) | -25% | -15.4% | 6.9% of pixels differ slightly (affine) |

A sky pixel currently costs about 2.6 times what a world pixel costs.

## Conclusions

1. **There is no silly performance bug in FitzQuake.** Its own code is 1% of a
   frame. Culling, chain building, alias setup, lightmap updates: each is under
   0.5%. Rewriting any of it will not show up in the frame rate.
2. **The frame is fill-rate bound in one NovaMesa loop**, and the expensive part
   of that loop is the second texture: the bilinear lightmap and the modulate
   are about half of the whole frame (`r_fullbright 1` is 48% cheaper).
3. **Overdraw is already low**, so re-ordering the world front to back is not
   worth doing (see "Measured and not worth doing").
4. What is left on the FitzQuake side adds up to a few percent, plus knobs that
   trade appearance for speed. The larger remaining gains are in NovaMesa and
   are listed separately below, with numbers.

## Bugs found

None of these explain the frame rate. In rough order of how likely they are to
bite:

1. **`+map`, `+timedemo` and other `+commands` are ignored with shareware data.**
   `common.c:1057`: `Cvar_Set ("cmdline", com_cmdline)` is only reached in the
   registered branch of `COM_CheckRegistered`; the shareware branch returns at
   line 1047 first. `stuffcmds` reads that cvar. Fix: set it before the
   `h == -1` test.
2. **The first map loaded from the command line or `autoexec.cfg` is never
   freed, and config.cfg overwrites cvar defaults.** `host.c:796` runs
   `Cbuf_Execute ()` inside `Host_Init`, before `host_hunklevel` is taken and
   before `host_initialized` is set. A `map`/`load` executed there allocates
   below the mark, so `Host_ClearMemory` never releases that level's hunk
   memory. And `Cvar_Set` copies the value into `default_string` while
   `host_initialized` is false, so "reset to defaults" restores the saved
   config, not the defaults. Fix: take `host_hunklevel` and set
   `host_initialized = true` before that `Cbuf_Execute`, not after.
3. **`vis_changed` is never cleared.** `r_world.c:80`: stock FitzQuake has
   `vis_changed = false;` after the early-return block; it is missing here.
   Once `r_novis` or `r_oldskyleaf` has been changed, the surface chains are
   rebuilt every frame for the rest of the session. (Cheap, about 0.1%, but
   wrong.)
4. **`-nosound` loses the saved sound settings.** `S_Init` (`snd_dma.c:162`)
   returns before registering `volume`, `bgmvolume`, `sndspeed` and the rest.
   `default.cfg` then prints `Unknown command "volume"`, and because
   `Cvar_WriteVariables` only writes registered cvars, the next `config.cfg`
   is saved without them. Fix: register the cvars before the `-nosound` test.
5. **`Sys_Printf` can overflow its buffer, and the log is on and unbuffered by
   default.** `sys_sdl.c:37`: `vsprintf` into `text[2048]`, but `Con_Printf`
   passes messages of up to 4096 bytes. `gl_info` with NovaMesa's extension
   list is about 2000 bytes, so it is close already. Also the `logfile` cvar
   defaults to 1 and the file is opened with `setbuf(logfile, NULL)`, so every
   console line is a separate DOS write, including pickup messages during
   play. Fix: `vsnprintf` (or write `msg` directly), default `logfile` to 0 or
   leave the stream buffered.
6. **Sound mixing is fragile** (`snd_sdl.c`, `snd_dma.c`). It works as shipped,
   by timing rather than by construction:
   - `paint_audio` (SDL's audio task) and `S_Update_` (main task) both call
     `S_PaintChannels`. `shm->samplepos` counts frames, but `GetSoundtime`
     divides it by the channel count, so the main task believes time runs at
     half speed and stops mixing after about 2048 frames. Until then both
     tasks mix into the same buffers.
   - `shm->buffer` is NULL until the first callback. A main-task mix before
     that writes 4 K at address 0.
   - `sfx_mixing_buffer` is a fixed 4096 bytes and `S_TransferStereo16` assumes
     `len` is exactly 1024 frames and `paintedtime` is a multiple of it. A
     backend that returns a different buffer size overruns it.
   - `S_PaintChannels` can call `S_LoadSound` -> `Cache_Check`/`Cache_Alloc`
     from the audio task while the main task is allocating.

   Fix: make `S_Update_` do nothing for this backend (SDLQuake does exactly
   that), size the buffer and `shm->samples` from the first callback's `len`,
   and bracket `S_StartSound`, `S_StopAllSounds` and level loading with
   `SDL_LockAudio`/`SDL_UnlockAudio`.
7. **`gl_affinemodels 1` changes the hint for everything.** `r_alias.c:538`
   restores `GL_NICEST`, but this port's global setting is `GL_FASTEST`
   (`gl_vidsdl.c:614`). Harmless if the global hint becomes `GL_NICEST` (see
   below).
8. Inherited from GLQuake, cosmetic: `gl_rlight.c:81` uses `v_blend[1]` where
   `v_blend[0]` is meant. `snd_dma.c` writes `sndspeed.value` directly for
   `-sndspeed`, leaving the cvar's string stale.

In SDL-NovaMesa, noticed while reading (not this repository):

- `src/audio/SDL_audio.c:71`: `if (!audio->opened)` treats `AHI_OpenAudio`'s
  failure value of -1 as success.
- `src/timer/amigaos/SDL_systimer.c:117`: `SDL_GetTicks()` is `clock()`. **Worth
  checking on the real toolchain:** if libnix implements `clock()` with
  `DateStamp()`, its resolution is one tick, 20 ms. `Sys_FloatTime` feeds
  `host_frametime` from it, so at 25-50 fps frame times would alternate between
  20 and 40 ms and motion would judder even at a steady frame rate. Print a
  few hundred consecutive `SDL_GetTicks()` values to find out. If so, use
  `timer.device` (`ReadEClock` or `GetSysTime`), which SDL already has open.

Also stock behaviour, not a bug: every `Con_Printf` while not fully connected
calls `SCR_UpdateScreen`, so start-up and level loading draw and swap a full
frame per console line.

## Suggested changes in FitzQuake

No loss of visual quality:

1. **Default `gl_flashblend` to 0** (`gl_rmain.c`). -2.9% in demo1, and it is
   stock FitzQuake's default: dynamic lights light the walls instead of being
   drawn as blended orange spheres. With 1, every shot the player fires also
   triggers the full-screen blend, because the view is inside its own muzzle
   flash.
2. **Set the sky state once.** `Sky_DrawFaceQuad` rebinds both textures,
   enables and disables unit 1, sets the env mode and toggles blending for
   every quad, up to several hundred times a frame; each one makes Mesa
   revalidate its state. Do it once in `Sky_DrawSky` and emit all quads inside
   one `glBegin (GL_QUADS)`. Drop the `glEnable (GL_BLEND)` in the multitexture
   branch: alpha is 1 after `GL_DECAL`, so the blend changes nothing. Measured:
   byte-identical output, -7% sky cost, -4% frame in a sky-heavy view.
3. **Use `GL_NICEST` as the global hint** (`gl_vidsdl.c:614`). Measured +0.2%.
   NovaMesa's perspective front end already falls back to the affine loop for
   triangles where the error is under half a pixel, so `GL_FASTEST` buys
   nothing and costs visible warping on water, sprites and large polygons.
4. `vid.numpages = 1` (`gl_vidsdl.c:291`). NovaMesa's back buffer is copied to
   the window, never flipped, so the status bar and tile border only need
   redrawing once after a change, not twice. About 0.5%.

With a visual trade-off, for the owner to decide:

- **`viewsize`.** Cost is proportional to 3D pixels. `viewsize 90` is 19% fewer.
- **`gl_polyblend 0`.** 6% on average, about 19% of every frame in which a
  flash is active, and continuously while underwater or holding a powerup.
- **Sky.** `r_fastsky 1` removes most of the 61% the sky costs in an open view.
  The two-pass variant above is -15% of such a frame for slightly different
  pixels. `r_sky_quality` (default 12) sets the quad count.
- `gl_overbright_models 0` and `gl_fullbrights 0`: 1.4% and 1.8%. Not worth
  the loss on a PiStorm; more under a software FPU.
- Keep `gl_texturemode` at its default, `GL_NEAREST_MIPMAP_NEAREST`.
  `GL_LINEAR_MIPMAP_NEAREST` is +24% and `GL_LINEAR_MIPMAP_LINEAR` (trilinear)
  is 5 times slower. A leftover `gl_texturemode` line in an `autoexec.cfg` is
  the first thing to rule out if a setup is unexpectedly slow. (The command
  takes names or 1-based numbers; the variable in `gl_texmgr.c` is 0-based.)

## Where the remaining speed is: NovaMesa

These are leads, measured here but not implemented. All are selected by GL
state alone, as NovaMesa's rules require.

1. **Lightmap filtering in `dualtex_span`.** 41% of all instructions. A lightmap
   texel covers 16 texture texels, so on most surfaces it is magnified many
   times and neighbouring pixels get almost the same light. Prototype (in
   `dualtex_span`, unit 0 nearest and unit 1 linear only): when the lightmap
   step is at most 1/8 texel per pixel, filter it every 4th pixel and
   interpolate the three light values in between; the last run of a span ends
   on the span's last pixel so no sample is taken outside the surface's
   lightmap block. Result on demo1: -6.4% instructions, 8% of pixels differ,
   0.001% by more than 2/255, maximum 3/255. With the threshold at 1/4 texel:
   -8.6%, maximum 5/255. The 64-bit corridor benchmark gives -10.6%; the 32-bit
   x86 build is short of registers and spills the interpolators, so a 68k
   should land nearer the higher figure. Ceiling: -22%.
2. **Direct blended flat fill in the driver.** `V_PolyBlend` goes through
   `flat_rgba_triangle` and the generic span code at about 98 instructions per
   pixel for a constant colour. A driver function for "untextured, flat,
   blend `SRC_ALPHA`/`ONE_MINUS_SRC_ALPHA`, nothing else" should need about 15.
   Worth about 5% of an average demo and most of the 19% while a flash is
   active. This is the "direct blended triangles" idea in NovaMesa's notes,
   which was shelved for lack of an application that needed it.
3. **Sky state.** REPLACE + DECAL (RGBA) on unit 1 falls to
   `multitextured_triangle`. Adding DECAL on unit 1 to the dualtex path (also
   already on NovaMesa's list) would cut a sky pixel to roughly the cost of a
   world pixel, about a third of a sky-heavy frame.
4. **A dualtex variant that tests and writes Z and ARGB itself**, for the plain
   world state (depth LESS/LEQUAL with writes, no blend, alpha test, fog or
   stencil). Removes most of the 13.5% spent in the generic span code after
   texturing.
5. **Single-unit `GL_COMBINE` modulate with RGB scale** (alias overbright) is
   on the general float path. Only 1.4% here; it matters more where float is
   slow.

## Measured and not worth doing

- **Drawing the world front to back.** With perfect ordering and per-pixel
  rejection the world would texture 61 K pixels a frame instead of 74 K, at
  most 10% of the frame, and it would bind a texture and a lightmap per
  surface instead of per chain. The existing whole-span reject already gets
  most of what there is.
- **Batching dynamic lightmap uploads.** `R_UploadLightmap` is 0.36%.
- **Removing the no-op blend on fullbright-mask alias models**
  (`r_alias.c`, cases 1 and 5). 0.13%.
- **`-nomtex`.** The two-pass world is 12% slower than multitexture.
- **Anything in the vertex path or engine logic.** `R_MarkSurfaces`,
  `R_CullSurfaces`, `R_SetupAliasLighting`, `GL_DrawAliasFrame`: 0.1-0.3% each.

Already done by the owner (marked `NOVA`), all sound: sin/cos tables and a
single matrix per entity (`R_EntityMatrix`), per-model vertex colour table for
alias models, texture chains bucketed by lightmap so `GL_Bind` de-duplicates
binds.

## Host profiling harness

`test/hostbench/` builds the engine for the host with no window, input or
sound, against the NovaMesa checkout next to this one. Nothing in it is used
by the Amiga build.

    cd test/hostbench
    ./build.sh                                    # -> out/fq  (needs gcc -m32: gcc-multilib)
    mkdir -p run/id1 && cp /path/to/pak0.pak run/id1/   # shareware pak0 is enough
    ./run.sh "timedemo demo1"                     # wall clock
    PROFILE=1 ./run.sh "timedemo demo1"           # callgrind instruction count
    PROFILE=1 ./run.sh "gl_polyblend 0\ntimedemo demo1"
    QUITFRAMES=150 PROFILE=1 ./run.sh "map e1m5\n+lookup"    # sky-heavy view
    DUMPEVERY=60 DUMPPREFIX=before ./run.sh "timedemo demo1" # PPM screenshots

- `sdl_stub.c` stands in for SDL and SDL_mixer and creates the NovaMesa
  context; `host_main.c` replaces `amiga_main.c` and `sys_file_amiga.c`;
  `stub/SDL/` has the few declarations the source needs.
- `FAKECLOCK` (default 50 ms per frame in `run.sh`) replaces the real clock,
  which makes a run deterministic: two runs of the same binary produce
  identical screenshots and instruction counts.
- To compare engine variants, copy `src/` somewhere, patch it, and build with
  `FQSRC=... TAG=name ./build.sh`. For NovaMesa variants use
  `NOVAMESA=... TAG=name`. Use `BIN=out/fq_name DIR=name ./run.sh ...` so runs
  can go in parallel.
- Wall-clock differences under about 5% are lost in noise on a shared
  machine; use `PROFILE=1`. One profiled demo takes about five minutes.
- Commands go through `id1/autoexec.cfg` because of bug 1 above.
- It is a little-endian x86 build. It says nothing about big-endian
  correctness; NovaMesa's own m68k/qemu benchmark covers that for the library.
