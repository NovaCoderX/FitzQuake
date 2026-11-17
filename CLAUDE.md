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
- Line endings are mixed. Most of `src/` is CRLF (`host.c`, `common.c`,
  `gl_rmain.c`, `r_world.c`, `gl_draw.c`, `sys_sdl.c`, nearly all headers,
  ...). These are LF: `amiga_main.c cd_sdl.c cl_parse.c console.c gl_mesh.c
  gl_model.c gl_refrag.c gl_rmisc.c gl_screen.c gl_sky.c gl_texmgr.c
  gl_vidsdl.c r_alias.c r_brush.c r_part.c sbar.c snd_dma.c snd_mix.c
  snd_sdl.c view.c gl_warp_sin.h glquake.h quakedef.h render.h sound.h`.
  Preserve what a file uses: check with `grep -c $'\r' file` before editing,
  and edit CRLF files in binary mode. A script that reads in text mode
  converts the whole file silently.
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
`GL_NEAREST_MIPMAP_NEAREST`, `r_oldwater 1`, `gl_subdivide_size 256`, default
mode 320x240, the 72 fps cap is removed, `sndspeed 22050`. (`gl_flashblend 1`
and the `GL_FASTEST` hint used to be on this list; both are back to stock,
see "Changes applied".)

## Which NovaMesa path each pass uses (default settings)

| Pass | GL state | NovaMesa triangle function |
|---|---|---|
| World, brush entities | unit 0 REPLACE (RGB, mipmapped nearest) + unit 1 lightmap (RGB, LINEAR), COMBINE modulate x2 | `dualtex_triangle` — integer fast path |
| Sky, flat pre-pass | untextured, flat | driver direct fill |
| Sky, cloud layers | unit 0 REPLACE + unit 1 DECAL (RGBA), depth GEQUAL | `multitextured_triangle` — general float path |
| Alias model, no fullbright pixels | one texture, COMBINE modulate x2 | `general_textured_triangle` — general float path |
| Alias model with fullbright mask | unit 0 COMBINE x2 + unit 1 DECAL, blend on | `multitextured_triangle` — general float path |
| World fullbright ("glow") pass | one RGBA texture, blend | `persp_textured_triangle` + one-pass blend |
| Water (`r_oldwater 1`), particles, sprites, 2D | one texture, not mipmapped | `persp_textured_triangle`, which uses its affine loop wherever that is within half a pixel (all 2D) |
| View blend (`V_PolyBlend`); dlight blobs with `gl_flashblend 1` | untextured, blend | `flat_rgba_triangle` / `smooth_rgba_triangle` through the generic span code |

## Where the time goes (measured)

These measurements were taken on commit `fc9432e`, before the fixes listed
under "Changes applied"; the defaults then were `gl_flashblend 1` and
`GL_FASTEST`. With the changes, demo1 is 30.02 G instructions (-2.8%) and the
sky-heavy view below renders 4.2% cheaper. The proportions are otherwise
unchanged.

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
4. What there was on the FitzQuake side added up to a few percent and has been
   applied (below). Beyond that there are only knobs that trade appearance for
   speed. The larger remaining gains are in NovaMesa and are listed separately,
   with numbers.

## Changes applied

All of these are in the tree, marked `NOVA`. "Verified" means checked with the
host harness: screenshots every 60 frames of demo1 and every 100 of demo2,
and three sky configurations, compared against the build before the change.

Bug fixes:

1. **`+map`, `+timedemo` and other `+commands` were ignored with shareware
   data.** `COM_CheckRegistered` (`common.c`) only set the `cmdline` cvar on
   the registered path. It is now set before the test. Verified: `+map e1m2`
   used to start the demo loop and now loads the map.
2. **The first map loaded from the command line or `autoexec.cfg` was never
   freed, and config.cfg overwrote cvar defaults.** `Host_Init` (`host.c`) ran
   `quake.rc` before taking `host_hunklevel` and before setting
   `host_initialized`. The mark and the flag are now set first, as in stock
   Quake. Verified with gdb: the mark used to end up at 4.77 MB after
   `map e1m1` and now stays at 0.73 MB across map changes.
3. **`vis_changed` was never cleared** (`R_MarkSurfaces`, `r_world.c`), so one
   change of `r_novis` or `r_oldskyleaf` rebuilt the surface chains every
   frame from then on.
4. **`-nosound` lost the saved sound settings.** `S_Init` (`snd_dma.c`)
   returned before registering `volume`, `bgmvolume`, `sndspeed` and the rest,
   so they were dropped from the next `config.cfg`. They are registered first
   now.
5. **`Sys_Printf` and `Sys_Error` could overflow their 2048-byte buffers**
   (`sys_sdl.c`); both use `vsnprintf` now. Left alone on purpose: the
   `logfile` cvar still defaults to 1 and the file is still unbuffered, so
   every console line is one DOS write. That is the owner's choice between a
   log that survives a crash and one that costs nothing.
6. **Sound mixing** (`snd_sdl.c`, `snd_dma.c`, `snd_mix.c`):
   - `paint_audio` paints into a ring of 8192 frames, the way Quake uses a DMA
     buffer, and copies out whatever length SDL asks for. It used to assume
     every callback asked for exactly 4096 bytes and overran its buffer
     otherwise. The ring is allocated before the callback is installed, so
     `shm->buffer` is never NULL.
   - `S_Update_` no longer mixes from the main task (only `-simsound` uses it).
     It used to race the callback for the first 2000 samples.
   - `SNDDMA_Lock`/`SNDDMA_Unlock` (SDL's audio lock) are held while
     `S_StartSound`, `S_StopSound`, `S_StopAllSounds`, `S_StaticSound` and the
     spatialisation loop in `S_Update` change `channels[]`. Never hold it
     across `S_LoadSound`; `S_StartSound` drops it around the load.
   - The mixer no longer calls `S_LoadSound`. It reads `sfx->cache.data`
     directly, and `S_Update` touches (and if needed reloads) the cache entry
     of every active channel from the main task. `Cache_Check` relinks the
     cache's LRU list, which the main task relinks too whenever it draws an
     alias model, so doing it from the audio task could corrupt the list.
   - `SND_PaintChannelFrom8/16` take the offset in `paintbuffer` to start at.
     They always started at 0, so the part of a looping sound after its loop
     point was mixed on top of the start of the block (an old id bug, fixed
     the same way in later engines).

   Verified with the harness's simulated audio device over all of demo1: the
   mixer's output is byte-identical to the old code at the usual block size
   apart from the 22 blocks (of 979) where the loop fix applies; the output
   is identical however the same amount of audio is split into callbacks; and
   AddressSanitizer is clean for block sizes from 4 to 40000 bytes. Not
   tested: real AHI output, and the locking against a real second task.
7. **The dlight view blend had the wrong colour** (`AddLightBlend`,
   `gl_rlight.c`): red was computed from the green channel, a typo inherited
   from GLQuake. Only visible with `gl_flashblend 1`, where the tint is now
   orange instead of yellow-green.
8. `gl_affinemodels 1` used to leave the hint at `GL_NICEST` while the rest of
   the port used `GL_FASTEST`. Fixed by change 3 below; no code change in
   `r_alias.c`.

Not changed: `-sndspeed` still writes `sndspeed.value` directly. Going
through `Cvar_Set` would save a command-line override into `config.cfg`.

Rendering changes, none of which lowers quality:

1. **`gl_flashblend` defaults to 0** (`gl_rmain.c`), as in stock FitzQuake.
   Dynamic lights light the walls instead of being drawn as blended spheres
   with a full-screen tint. -2.9% on demo1.
2. **Sky state is set once per pass** (`Sky_DrawSkyLayers`, `gl_sky.c`), all
   quads go into one `glBegin`, and the no-op `GL_BLEND` in the multitexture
   branch is gone. Verified byte-identical in all three branches (default,
   `-nomtex`, `r_skyalpha 0.5`). -7% sky cost, -4.2% of a sky-heavy frame.
3. **The global hint is `GL_NICEST`** (`gl_vidsdl.c`). +0.2%. Over demo2 it
   changes 2.5% of pixels, all on water surfaces (now perspective-correct);
   the status bar and other 2D are byte-identical.

Together: demo1 30.87 G -> 30.02 G instructions.

Still open, noticed while doing this:

- `vid.numpages = 1` (`gl_vidsdl.c`). NovaMesa's back buffer is copied to the
  window, never flipped, so the status bar and tile border only need
  redrawing once after a change, not twice. About 0.5%. Not applied.
- **NovaMesa**, `src/tnl/t_imm_fixup.c:477`: `is_fan_like[]` has `GL_POLYGON+1`
  entries and is indexed with `ctx->Driver.CurrentExecPrimitive`, which is
  `PRIM_OUTSIDE_BEGIN_END` (`GL_POLYGON+1`) outside `glBegin`. It reads one
  byte past the table on every flush outside a primitive. An old Mesa 4.1
  bug, found by AddressSanitizer; the table needs three more `GL_FALSE`
  entries.
- **SDL-NovaMesa**, `src/audio/SDL_audio.c:71`: `if (!audio->opened)` treats
  `AHI_OpenAudio`'s failure value of -1 as success.
- **SDL-NovaMesa**, `src/timer/amigaos/SDL_systimer.c:117`: `SDL_GetTicks()` is
  `clock()`. **Worth checking on the real toolchain:** if libnix implements
  `clock()` with `DateStamp()`, its resolution is one tick, 20 ms.
  `Sys_FloatTime` feeds `host_frametime` from it, so at 25-50 fps frame times
  would alternate between 20 and 40 ms and motion would judder even at a
  steady frame rate. Print a few hundred consecutive `SDL_GetTicks()` values
  to find out. If so, use `timer.device` (`ReadEClock` or `GetSysTime`), which
  SDL already has open.
- Stock behaviour, not a bug: every `Con_Printf` while not fully connected
  calls `SCR_UpdateScreen`, so start-up and level loading draw and swap a full
  frame per console line.

## Settings that trade appearance for speed

For the owner to decide; none is applied.

- **`viewsize`.** Cost is proportional to 3D pixels. `viewsize 90` is 19% fewer.
- **`gl_polyblend 0`.** 6% on average, about 19% of every frame in which a
  flash is active, and continuously while underwater or holding a powerup.
- **Sky.** `r_fastsky 1` removes most of the 61% the sky costs in an open view.
  Drawing the two cloud layers as two passes instead of multitextured is -15%
  of such a frame for slightly different pixels. `r_sky_quality` (default 12)
  sets the quad count.
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
    SOUND=1 AUDIODUMP=/tmp/a.raw ./run.sh "timedemo demo1"   # run the sound mixer too

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
- Comparing screenshots between two builds: every console line printed during
  start-up costs one buffer swap, so builds that print a different number of
  lines are out of step by that many frames. `DUMPOFFSET=n` shifts the frame
  numbering of one run to line them up. Frames with particles or explosions
  can still differ, because `rand()` is called once per frame.
- With `SOUND=1 AUDIODUMP=file` the stub pretends the audio device opened and
  calls the engine's mixer callback from the frame loop, writing its 16-bit
  stereo output to the file. `AUDIOLEN=a,b,...` sets the block sizes asked for
  each frame. Two builds can be compared byte for byte, and any split of the
  same total must give the same output. The stub also reports an audio lock
  left held across a frame. `CC="gcc -m32 -fsanitize=address"` works for the
  engine; NovaMesa trips it at start-up (see "Still open"), so reuse
  uninstrumented Mesa objects.
- Commands go through `id1/autoexec.cfg`, which works with any data. (`+cmd`
  on the command line works too since bug fix 1.)
- It is a little-endian x86 build. It says nothing about big-endian
  correctness; NovaMesa's own m68k/qemu benchmark covers that for the library.
