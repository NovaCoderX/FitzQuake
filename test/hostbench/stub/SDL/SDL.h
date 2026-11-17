/* Minimal SDL 1.2 declarations for the headless host build. Not used by the Amiga build. */
#ifndef STUB_SDL_H
#define STUB_SDL_H
#include <stdlib.h>
typedef unsigned char Uint8; typedef unsigned short Uint16; typedef unsigned int Uint32; typedef short Sint16; typedef int Sint32;
typedef struct { Sint16 x,y; Uint16 w,h; } SDL_Rect;
typedef struct SDL_PixelFormat { void *palette; Uint8 BitsPerPixel; Uint8 BytesPerPixel; } SDL_PixelFormat;
typedef struct SDL_Surface { int w,h; } SDL_Surface;
typedef struct { int current_w, current_h; } SDL_VideoInfo;
typedef enum {
	SDLK_FIRST_ = 0,
	SDLK_0, SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6, SDLK_7, SDLK_8, SDLK_9,
	SDLK_AMPERSAND, SDLK_ASTERISK, SDLK_AT, SDLK_BACKQUOTE, SDLK_BACKSLASH, SDLK_BACKSPACE,
	SDLK_BREAK, SDLK_CAPSLOCK, SDLK_CARET, SDLK_COLON, SDLK_COMMA, SDLK_COMPOSE, SDLK_DELETE,
	SDLK_DOLLAR, SDLK_DOWN, SDLK_END, SDLK_EQUALS, SDLK_ESCAPE, SDLK_EURO, SDLK_EXCLAIM, SDLK_F1,
	SDLK_F10, SDLK_F11, SDLK_F12, SDLK_F13, SDLK_F14, SDLK_F15, SDLK_F2, SDLK_F3, SDLK_F4,
	SDLK_F5, SDLK_F6, SDLK_F7, SDLK_F8, SDLK_F9, SDLK_GREATER, SDLK_HASH, SDLK_HELP, SDLK_HOME,
	SDLK_INSERT, SDLK_KP0, SDLK_KP1, SDLK_KP2, SDLK_KP3, SDLK_KP4, SDLK_KP5, SDLK_KP6, SDLK_KP7,
	SDLK_KP8, SDLK_KP9, SDLK_KP_DIVIDE, SDLK_KP_ENTER, SDLK_KP_EQUALS, SDLK_KP_MINUS,
	SDLK_KP_MULTIPLY, SDLK_KP_PERIOD, SDLK_KP_PLUS, SDLK_LALT, SDLK_LCTRL, SDLK_LEFT,
	SDLK_LEFTBRACKET, SDLK_LEFTPAREN, SDLK_LESS, SDLK_LMETA, SDLK_LSHIFT, SDLK_LSUPER, SDLK_MENU,
	SDLK_MINUS, SDLK_MODE, SDLK_NUMLOCK, SDLK_PAGEDOWN, SDLK_PAGEUP, SDLK_PAUSE, SDLK_PERIOD,
	SDLK_PLUS, SDLK_POWER, SDLK_PRINT, SDLK_QUESTION, SDLK_QUOTE, SDLK_QUOTEDBL, SDLK_RALT,
	SDLK_RCTRL, SDLK_RETURN, SDLK_RIGHT, SDLK_RIGHTBRACKET, SDLK_RIGHTPAREN, SDLK_RMETA,
	SDLK_RSHIFT, SDLK_RSUPER, SDLK_SCROLLOCK, SDLK_SEMICOLON, SDLK_SLASH, SDLK_SPACE, SDLK_SYSREQ,
	SDLK_TAB, SDLK_UNDERSCORE, SDLK_UNDO, SDLK_UP, SDLK_a, SDLK_b, SDLK_c, SDLK_d, SDLK_e, SDLK_f,
	SDLK_g, SDLK_h, SDLK_i, SDLK_j, SDLK_k, SDLK_l, SDLK_m, SDLK_n, SDLK_o, SDLK_p, SDLK_q,
	SDLK_r, SDLK_s, SDLK_t, SDLK_u, SDLK_v, SDLK_w, SDLK_x, SDLK_y, SDLK_z,
	SDLK_LAST
} SDLKey;
typedef struct { Uint8 scancode; SDLKey sym; int mod; Uint16 unicode; } SDL_keysym;
typedef struct { Uint8 type, which, state; SDL_keysym keysym; } SDL_KeyboardEvent;
typedef struct { Uint8 type, which, state; Uint16 x,y; Sint16 xrel, yrel; } SDL_MouseMotionEvent;
typedef struct { Uint8 type, which, button, state; Uint16 x,y; } SDL_MouseButtonEvent;
typedef union { Uint8 type; SDL_KeyboardEvent key; SDL_MouseMotionEvent motion; SDL_MouseButtonEvent button; } SDL_Event;
enum { SDL_KEYDOWN = 2, SDL_KEYUP, SDL_MOUSEMOTION, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP, SDL_QUIT = 12 };
enum { SDL_BUTTON_LEFT = 1, SDL_BUTTON_MIDDLE, SDL_BUTTON_RIGHT, SDL_BUTTON_WHEELUP, SDL_BUTTON_WHEELDOWN };
#define SDL_PRESSED 1
#define SDL_OPENGL 2
#define SDL_FULLSCREEN 0x80000000u
#define SDL_INIT_AUDIO 0x10
#define SDL_INIT_VIDEO 0x20
#define SDL_INIT_NOPARACHUTE 0x100000
#define SDL_ENABLE 1
#define SDL_DISABLE 0
#define SDL_MIX_MAXVOLUME 128
typedef enum { SDL_GRAB_QUERY = -1, SDL_GRAB_OFF, SDL_GRAB_ON } SDL_GrabMode;
typedef enum { SDL_GL_RED_SIZE, SDL_GL_GREEN_SIZE, SDL_GL_BLUE_SIZE, SDL_GL_ALPHA_SIZE, SDL_GL_BUFFER_SIZE, SDL_GL_DOUBLEBUFFER, SDL_GL_DEPTH_SIZE, SDL_GL_STENCIL_SIZE, SDL_GL_SWAP_CONTROL } SDL_GLattr;
int SDL_Init(Uint32); int SDL_InitSubSystem(Uint32); void SDL_QuitSubSystem(Uint32); void SDL_Quit(void);
char *SDL_GetError(void); int SDL_putenv(const char *); Uint32 SDL_GetTicks(void); int SDL_PollEvent(SDL_Event *);
int SDL_GL_SetAttribute(SDL_GLattr, int); SDL_Surface *SDL_SetVideoMode(int, int, int, Uint32); void SDL_GL_SwapBuffers(void);
SDL_Rect **SDL_ListModes(SDL_PixelFormat *, Uint32); const SDL_VideoInfo *SDL_GetVideoInfo(void);
void SDL_WM_SetCaption(const char *, const char *); SDL_GrabMode SDL_WM_GrabInput(SDL_GrabMode); int SDL_ShowCursor(int);
void SDL_MixAudio(Uint8 *, const Uint8 *, Uint32, int);
void SDL_LockAudio(void); void SDL_UnlockAudio(void);
#endif
