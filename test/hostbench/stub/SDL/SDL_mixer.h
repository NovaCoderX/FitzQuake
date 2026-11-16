/* Minimal SDL_mixer declarations for the headless host build. */
#ifndef STUB_MIX_H
#define STUB_MIX_H
typedef struct Mix_Music Mix_Music;
#define MIX_DEFAULT_FORMAT 0x9010
#define MIX_MAX_VOLUME 128
int Mix_OpenAudio(int, Uint16, int, int); int Mix_QuerySpec(int *, Uint16 *, int *); void Mix_CloseAudio(void);
void Mix_SetPostMix(void (*)(void *, Uint8 *, int), void *); char *Mix_GetError(void);
Mix_Music *Mix_LoadMUS(const char *); int Mix_PlayMusic(Mix_Music *, int); int Mix_PlayingMusic(void); int Mix_PausedMusic(void);
void Mix_PauseMusic(void); void Mix_ResumeMusic(void); int Mix_HaltMusic(void); void Mix_FreeMusic(Mix_Music *); int Mix_VolumeMusic(int);
#endif
