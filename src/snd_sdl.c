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

#define MIX_NUM_CHANNELS 2
#define MIX_BUFFER_SIZE 1024

static dma_t the_shm;

// We need a local buffer because SDL_mixer's stream is transient
// and we need to mix Quake's SFX into it.
static Uint8 *sfx_mixing_buffer = NULL;

/*
================
paint_audio

This is called by SDL_mixer after it has processed the music.
We paint Quake's SFX into our local buffer, then mix it into the
final output stream.
================
*/
static void paint_audio(void *unused, Uint8 *stream, int len)
{
    if (shm && sfx_mixing_buffer) {
        // Clear the temporary buffer so we don't have old sounds looping
        Q_memset(sfx_mixing_buffer, 0, len);
        
        // Point Quake's sound buffer to our temporary mixing buffer
        shm->buffer = sfx_mixing_buffer;

        // Update the sample position based on how much SDL requested
        shm->samplepos += len / (shm->samplebits / 8) / shm->channels;

        // Tell Quake to paint its SFX into shm->buffer
        S_PaintChannels(shm->samplepos);

        // Mix Quake's finished SFX buffer into the SDL_mixer stream (which contains music)
        // SDL_MIX_MAXVOLUME allows Quake's internal volume sliders to stay in control
        SDL_MixAudio(stream, sfx_mixing_buffer, len, SDL_MIX_MAXVOLUME);
    }
}

int SNDDMA_Init(void)
{
    int actual_rate, actual_channels;
    Uint16 actual_format;
    int audio_rate = (int)sndspeed.value;

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) == -1) {
		Con_Printf("Could not initialize the audio subsystem: %s\n", SDL_GetError());
		return 0;
	}

    // Initialize SDL_mixer with the settings Quake expects
    if (Mix_OpenAudio(audio_rate, MIX_DEFAULT_FORMAT, MIX_NUM_CHANNELS, MIX_BUFFER_SIZE) < 0) {
    	Con_Printf("Could not initialize audio mixer: %s\n", Mix_GetError());
        return 0;
    }

    if (!Mix_QuerySpec(&actual_rate, &actual_format, &actual_channels)) {
    	Con_Printf("Could not query the audio mixer: %s\n", Mix_GetError());
    	return 0;
    }

    if (actual_channels != 2) {
    	Con_Printf("Using mono mixing frequency: %dHz\n", actual_rate);
    } else {
    	Con_Printf("Using stereo mixing frequency: %dHz\n", actual_rate);
    }

    // Set up the DMA structure for Quake's internal mixer
    shm = &the_shm;
    shm->splitbuffer = 0;
    shm->samplebits = (actual_format & 0xFF);
    shm->speed = actual_rate;
    shm->channels = actual_channels;
    shm->samples = MIX_BUFFER_SIZE * shm->channels;
    shm->samplepos = 0;
    shm->submission_chunk = 1;

    // Allocate the intermediate buffer Quake will paint into
    sfx_mixing_buffer = (Uint8 *)malloc(MIX_BUFFER_SIZE * (shm->samplebits / 8) * shm->channels);
    shm->buffer = NULL; // This is set per-frame in the callback

    // Set the PostMix callback, this hooks our SFX engine into the SDL_mixer output pipeline
    Mix_SetPostMix(paint_audio, NULL);

    return 1;
}

int SNDDMA_GetDMAPos(void)
{
    return shm->samplepos;
}

void SNDDMA_Shutdown(void)
{
	Mix_SetPostMix(NULL, NULL); // Remove the callback

	// Free the music.
	if (sfx_mixing_buffer) {
		free(sfx_mixing_buffer);
		sfx_mixing_buffer = NULL;
	}

	Mix_CloseAudio();
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
