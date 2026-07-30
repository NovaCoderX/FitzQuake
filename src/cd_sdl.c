/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2005 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "quakedef.h"

extern cvar_t bgmvolume;

#define MAX_TRACKS 24

static qboolean cdaudio_initialized = false;
static Mix_Music *music_cache[MAX_TRACKS];
static qboolean music_load_error[MAX_TRACKS];
static int current_track = -1;

void CDAudio_Init()
{
	if ((!snd_initialized) || (cdaudio_initialized)) {
		return;
	}
	
	if (COM_CheckParm("-nocdaudio")) {
		return;	
    }
    
    // Initialize cache to NULL
	Q_memset(music_cache, 0, sizeof(music_cache));
	Q_memset(music_load_error, 0, sizeof(music_load_error));
	current_track = -1;

	cdaudio_initialized = true;
}

void CDAudio_Play(byte track, qboolean looping)
{
	char name[MAX_OSPATH];

	if (!cdaudio_initialized) {
		return;
	}

	if (track >= MAX_TRACKS) {
		Con_Printf("Invalid track number (%02d)\n", track);
		return;
	}

	// If we are already playing this track
	if (current_track == track && Mix_PlayingMusic()) {
		return;
	}

	// Stop whatever is currently playing
	CDAudio_Stop();

	// Check if we already have this track in the cache
	if (!music_cache[track] && (!music_load_error[track])) {
		// Using com_gamedir ensures it works for mods (id1, hipnotic, rogue, etc.)
		sprintf(name, "%s/music/track%02d.ogg", com_gamedir, track);

		music_cache[track] = Mix_LoadMUS(name);
		if (!music_cache[track]) {
			// We only want to report an issue once per track.
			Con_Printf("Could not load music track '%s'\n", name);
			music_load_error[track] = true;
		}
	}

	if (music_cache[track]) {
		if (Mix_PlayMusic(music_cache[track], looping ? -1 : 0) == 0) {
			// Success.
			current_track = track;
		}
	}
}

void CDAudio_Stop()
{
	if (cdaudio_initialized) {
		if (Mix_PlayingMusic()) {
			Mix_HaltMusic();
		}
	}

	current_track = -1;
}

void CDAudio_Pause()
{
	if (cdaudio_initialized) {
		if (Mix_PlayingMusic()) {
			Mix_PauseMusic();
		}
	}
}

void CDAudio_Resume()
{
	if (cdaudio_initialized) {
		if (Mix_PausedMusic()) {
			Mix_ResumeMusic();
		}
	}
}

void CDAudio_Update()
{
	static float currentVolume = -1;

	if (cdaudio_initialized) {
		if (bgmvolume.value != currentVolume) {
			Mix_VolumeMusic(MIX_MAX_VOLUME * bgmvolume.value);
			currentVolume = bgmvolume.value;
		}
	}
}

void CDAudio_Shutdown()
{
	int i;
    
    CDAudio_Stop();

	// Free all cached music handles
	for (i = 0; i < MAX_TRACKS; i++) {
		if (music_cache[i]) {
			Mix_FreeMusic(music_cache[i]);
			music_cache[i] = NULL;
		}
	}
	
	cdaudio_initialized = false;
}

