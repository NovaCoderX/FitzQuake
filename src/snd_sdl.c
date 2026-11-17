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

// NOVA: size of the ring Quake paints into, in sample frames. Must be a power
// of two, and at least as long as the largest block SDL asks for in one go.
#define SFX_RING_FRAMES 8192

static dma_t the_shm;

// We need a local buffer because SDL_mixer's stream is transient
// and we need to mix Quake's SFX into it.
//
// NOVA: this is now a ring, used the way Quake uses a DMA buffer:
// S_PaintChannels() writes sample frame N at offset (N & (SFX_RING_FRAMES-1)).
// It used to be a linear buffer of exactly MIX_BUFFER_SIZE frames, which only
// worked while every callback asked for exactly that many and paintedtime
// stayed a multiple of it; any other length overran it. It is also allocated
// before the callback is installed, so shm->buffer is never NULL.
static Uint8 *sfx_ring = NULL;
static int sfx_ring_bytes = 0;
static int sfx_frame_bytes = 0;		// bytes per sample frame

/*
================
paint_audio

This is called by SDL_mixer after it has processed the music.
We paint Quake's SFX into our local buffer, then mix it into the
final output stream.

It runs in SDL's audio task, with SDL's audio lock held.
================
*/
static void paint_audio(void *unused, Uint8 *stream, int len)
{
    if (shm && sfx_ring) {
        int frames = len / sfx_frame_bytes;
        int start = shm->samplepos;
        int offset, first;

        if (frames > SFX_RING_FRAMES) {
            frames = SFX_RING_FRAMES;
        }

        // Update the sample position based on how much SDL requested
        shm->samplepos = start + frames;

        // Tell Quake to paint its SFX into the ring, up to the new position
        S_PaintChannels(shm->samplepos);

        // Mix Quake's finished SFX into the SDL_mixer stream (which contains music),
        // in two pieces if this block wraps around the end of the ring.
        // SDL_MIX_MAXVOLUME allows Quake's internal volume sliders to stay in control
        offset = (start & (SFX_RING_FRAMES - 1)) * sfx_frame_bytes;
        len = frames * sfx_frame_bytes;
        first = sfx_ring_bytes - offset;
        if (first > len) {
            first = len;
        }

        SDL_MixAudio(stream, sfx_ring + offset, first, SDL_MIX_MAXVOLUME);
        if (len > first) {
            SDL_MixAudio(stream + first, sfx_ring, len - first, SDL_MIX_MAXVOLUME);
        }
    }
}

/*
================
SNDDMA_Lock / SNDDMA_Unlock -- NOVA

paint_audio() runs in SDL's audio task and reads channels[] while the game
changes it. These keep the mixer out while a channel is being set up or torn
down. Never hold the lock across anything slow (file access in particular).
================
*/
void SNDDMA_Lock(void)
{
    if (sfx_ring) {
        SDL_LockAudio();
    }
}

void SNDDMA_Unlock(void)
{
    if (sfx_ring) {
        SDL_UnlockAudio();
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
    shm->samples = SFX_RING_FRAMES * shm->channels;
    shm->samplepos = 0;
    shm->submission_chunk = 1;

    // Allocate the ring Quake will paint into (NOVA: see the comment at the top)
    sfx_frame_bytes = (shm->samplebits / 8) * shm->channels;
    sfx_ring_bytes = SFX_RING_FRAMES * sfx_frame_bytes;
    sfx_ring = (Uint8 *)calloc(1, sfx_ring_bytes);
    if (!sfx_ring) {
    	Con_Printf("Could not allocate the sound mixing buffer\n");
    	Mix_CloseAudio();
    	return 0;
    }
    shm->buffer = sfx_ring;

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

	// Free the mixing buffer. The callback is gone, so nothing is using it.
	if (sfx_ring) {
		Uint8 *ring = sfx_ring;

		sfx_ring = NULL;
		if (shm) {
			shm->buffer = NULL;
		}
		free(ring);
	}

	Mix_CloseAudio();
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
