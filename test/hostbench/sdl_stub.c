// Headless stand-in for SDL 1.2 + SDL_mixer, so FitzQuake can be built for
// the host against NovaMesa and profiled. No window, no input, no sound: the
// GL context renders into NovaMesa's back buffer and "swap" only counts frames.
// Nothing here is used by the Amiga build.
//
// Environment variables:
//   FAKECLOCK=ms    SDL_GetTicks() advances by this much per frame instead of
//                   reading the real clock, which makes a run deterministic
//   QUITFRAMES=n    exit after n buffer swaps (for "map xxx" runs)
//   DUMPEVERY=n     write a PPM of the back buffer every n swaps
//   DUMPPREFIX=p    file name prefix for those PPMs (default "shot")
//   FRAMEHASH=file  write one line per frame, "frame-number hash", for the
//                   whole back buffer: compares every frame of two builds
//                   without storing screenshots (diff the two files)
//   DUMPOFFSET=n    added to the frame number for DUMPEVERY and the file name.
//                   Every console line printed during start-up costs one swap,
//                   so two builds that print a different number of lines are
//                   out of step by that many frames; this lines them up again
//   ECHO=1          copy console output to stdout
//   AUDIODUMP=file  pretend audio opened (run without -nosound: SOUND=1 with
//                   run.sh), call the engine's mixer callback once per frame
//                   and write the raw 16-bit stereo result to the file
//   AUDIOLEN=a,b,.. sizes in bytes of the blocks asked for each frame, one
//                   callback per entry (default 4096). Any split of the same
//                   total must give the same output
//   MODELFROM=n     under NovaMesa's test/emu68model harness (68k build):
//   MODELTO=m       count swaps n+1..m in the model of Emu68 (to the end of
//                   the run if MODELTO is not set). No effect elsewhere
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "libraries/Picasso96.h"
#include <GL/gl.h>
#include <GL/amiga_mesa.h>
#include "amiga_mesa_def.h"
#include <SDL/SDL.h>
#include <SDL/SDL_mixer.h>
#ifdef __m68k__
#include "model.h"
#endif

unsigned swap_sink;		// written by the stub p96WritePixelArray
static AMesaContext *actx;
static SDL_Surface surf;
static int W, H, frameno;
static unsigned fake_ms, fake_step;

int SDL_Init(Uint32 f){ if (getenv("FAKECLOCK")) fake_step = atoi(getenv("FAKECLOCK")); return 0; }
int SDL_InitSubSystem(Uint32 f){ return 0; }
void SDL_QuitSubSystem(Uint32 f){}
void SDL_Quit(void){}
char *SDL_GetError(void){ return "stub"; }
int SDL_putenv(const char *s){ return 0; }
Uint32 SDL_GetTicks(void)
{
	struct timespec ts;
	if (fake_step) return fake_ms;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (Uint32)(ts.tv_sec*1000u + ts.tv_nsec/1000000u);
}
int SDL_PollEvent(SDL_Event *e){ return 0; }
int SDL_GL_SetAttribute(SDL_GLattr a, int v){ return 0; }

SDL_Surface *SDL_SetVideoMode(int w, int h, int bpp, Uint32 flags)
{
	static struct BitMap bm; static struct RastPort rp = { &bm };
	static struct Screen scr; static struct Window win;
	if (actx) return &surf;
	W = w; H = h; win.Width = w; win.Height = h; win.RPort = &rp; win.WScreen = &scr;
	actx = amesa_create_context(&win);
	if (!actx) return NULL;
	amesa_make_current(actx);
	surf.w = w; surf.h = h;
	return &surf;
}

static void dump(const char *name)
{
	FILE *f = fopen(name, "wb"); int i; unsigned *p = (unsigned*)actx->back_buffer;
	if (!f) return;
	fprintf(f, "P6\n%d %d\n255\n", W, H);
	for (i = 0; i < W*H; i++) { unsigned px = p[i]; fputc((px>>16)&255,f); fputc((px>>8)&255,f); fputc(px&255,f); }	// 0xAARRGGBB words
	fclose(f);
}

// Simulated audio device. The callback is run from the frame loop, not from
// another thread, so a run is repeatable and two builds can be compared byte
// for byte. It also checks that the engine never leaves the audio lock held.
static void (*postmix)(void *, Uint8 *, int);
static FILE *audio_file;
static int audio_rate, audio_lock_depth;

static void run_audio(void)
{
	static Uint8 stream[65536];
	const char *list = getenv("AUDIOLEN") ? getenv("AUDIOLEN") : "4096";
	if (!postmix || !audio_file) return;
	if (audio_lock_depth) { printf("ERROR: audio lock held across a frame (depth %d)\n", audio_lock_depth); exit(1); }
	while (*list) {		// one callback per entry in the comma-separated list
		int len = atoi(list);
		if (len > (int)sizeof stream) len = sizeof stream;
		memset(stream, 0, len);
		postmix(NULL, stream, len);
		fwrite(stream, 1, len, audio_file);
		while (*list && *list != ',') list++;
		if (*list == ',') list++;
	}
}

void SDL_LockAudio(void){ audio_lock_depth++; }
void SDL_UnlockAudio(void){ if (--audio_lock_depth < 0) { printf("ERROR: audio unlocked more often than locked\n"); exit(1); } }

void SDL_GL_SwapBuffers(void)
{
	const char *d = getenv("DUMPEVERY");
	int dumpoffset = getenv("DUMPOFFSET") ? atoi(getenv("DUMPOFFSET")) : 0;
	frameno++; fake_ms += fake_step;
#ifdef __m68k__
	if (getenv("MODELFROM") && frameno == atoi(getenv("MODELFROM"))) model_begin();
	if (getenv("MODELTO") && frameno == atoi(getenv("MODELTO"))) model_end("frames");
#endif
	run_audio();
	if (getenv("FRAMEHASH")) {
		static FILE *hf; unsigned h = 2166136261u, *p = (unsigned*)actx->back_buffer; int i;
		if (!hf) hf = fopen(getenv("FRAMEHASH"), "w");
		glFinish();
		for (i = 0; i < W*H; i++) h = (h ^ p[i]) * 16777619u;
		if (hf) { fprintf(hf, "%d %08x\n", frameno + dumpoffset, h); fflush(hf); }
	}
	if (d && (frameno + dumpoffset) % atoi(d) == 0) {
		char nm[256];
		snprintf(nm, sizeof nm, "%s_%05d.ppm", getenv("DUMPPREFIX") ? getenv("DUMPPREFIX") : "shot", frameno + dumpoffset);
		glFinish(); dump(nm);
	}
	amesa_swap_buffers(actx);
	if (getenv("QUITFRAMES") && frameno >= atoi(getenv("QUITFRAMES"))) { printf("quit after %d frames\n", frameno); fflush(stdout); exit(0); }
}

SDL_Rect **SDL_ListModes(SDL_PixelFormat *f, Uint32 fl){ return (SDL_Rect **)-1; }
const SDL_VideoInfo *SDL_GetVideoInfo(void){ static SDL_VideoInfo vi = {640,480}; return &vi; }
void SDL_WM_SetCaption(const char *a, const char *b){}
SDL_GrabMode SDL_WM_GrabInput(SDL_GrabMode m){ return SDL_GRAB_OFF; }
int SDL_ShowCursor(int t){ return 0; }
void SDL_MixAudio(Uint8 *d, const Uint8 *s, Uint32 l, int v)
{
	// native-endian signed 16-bit, full volume, saturating
	short *dp = (short *)d; const short *sp = (const short *)s; Uint32 i;
	for (i = 0; i < l/2; i++) { int x = dp[i] + sp[i]; dp[i] = x > 32767 ? 32767 : x < -32768 ? -32768 : x; }
}

// Normally run with -nosound -nocdaudio, and these only need to link.
// With AUDIODUMP set the device "opens" and run_audio() drives the mixer.
int Mix_OpenAudio(int rate, Uint16 fmt, int ch, int chunk)
{
	if (!getenv("AUDIODUMP")) return -1;
	audio_file = fopen(getenv("AUDIODUMP"), "wb");
	audio_rate = rate;
	return audio_file ? 0 : -1;
}
int Mix_QuerySpec(int *rate, Uint16 *fmt, int *ch){ if (!audio_file) return 0; *rate = audio_rate; *fmt = MIX_DEFAULT_FORMAT; *ch = 2; return 1; }
void Mix_CloseAudio(void){ if (audio_file) { fclose(audio_file); audio_file = NULL; } }
void Mix_SetPostMix(void (*f)(void *, Uint8 *, int), void *a){ postmix = f; }
char *Mix_GetError(void){ return "stub"; }
Mix_Music *Mix_LoadMUS(const char *n){ return NULL; }
int Mix_PlayMusic(Mix_Music *m, int l){ return -1; }
int Mix_PlayingMusic(void){ return 0; }
int Mix_PausedMusic(void){ return 0; }
void Mix_PauseMusic(void){}
void Mix_ResumeMusic(void){}
int Mix_HaltMusic(void){ return 0; }
void Mix_FreeMusic(Mix_Music *m){}
int Mix_VolumeMusic(int v){ return 0; }

// Linked with -Wl,--wrap=Con_Printf: optionally echo the console, and leave
// as soon as a timedemo prints its "N frames T seconds F fps" result.
void __real_Con_Printf(char *fmt, ...);
void __wrap_Con_Printf(char *fmt, ...)
{
	va_list ap; static char msg[8192];
	va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
	if (getenv("ECHO")) fputs(msg, stdout);
	__real_Con_Printf("%s", msg);
	if (strstr(msg, "frames") && strstr(msg, "seconds")) {
		fputs(msg, stdout); printf("rendered frames: %d\n", frameno); fflush(stdout); exit(0);
	}
}
