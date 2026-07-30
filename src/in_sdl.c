/*
Copyright (C) 1996-1997 Id Software, Inc.

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


/*cvar_t	lookspring = {"lookspring","0", true};
cvar_t	lookstrafe = {"lookstrafe","0", true};
cvar_t	sensitivity = {"sensitivity","3", true};
cvar_t	m_pitch = {"m_pitch","0.022", true};
cvar_t	m_yaw = {"m_yaw","0.022", true};
cvar_t	m_forward = {"m_forward","1", true};
cvar_t	m_side = {"m_side","0.8", true};*/
cvar_t m_filter = {"m_filter","0", true};

// total accumulated mouse movement since last frame
// this gets updated from the main game loop via IN_MouseMove
static int mouse_x, mouse_y = 0;

// Only used for filtering.
static int old_mouse_x, old_mouse_y = 0;

// Is it grabbed or not?
qboolean mouseCaptured;

void IN_MLookDown();


void IN_Init (void)
{
    mouse_x = mouse_y = 0;
    old_mouse_x = old_mouse_y = 0;
    
	Cvar_RegisterVariable (&lookspring);
	Cvar_RegisterVariable (&lookstrafe);
	Cvar_RegisterVariable (&sensitivity);
	Cvar_RegisterVariable (&m_pitch);
	Cvar_RegisterVariable (&m_yaw);
	Cvar_RegisterVariable (&m_forward);
	Cvar_RegisterVariable (&m_side);
	Cvar_RegisterVariable (&m_filter);

    // Mouse look by default.
    IN_MLookDown();
}

void IN_Shutdown (void)
{
    // Empty
}

void IN_MouseMove(int dx, int dy)
{
    mouse_x += dx;
    mouse_y += dy;
}

void IN_Move (usercmd_t *cmd)
{
	if (m_filter.value)
	{
		if ((mouse_x > 1) || (mouse_x < -1))
		{
			mouse_x = (mouse_x + old_mouse_x) * 0.5;
		}

		if ((mouse_y > 1) || (mouse_y < -1))
		{
			mouse_y = (mouse_y + old_mouse_y) * 0.5;
		}
	}

	old_mouse_x = mouse_x;
	old_mouse_y = mouse_y;  
        
	if ((mouse_x != 0) || (mouse_y != 0)) {			
        mouse_x *= sensitivity.value;
        mouse_y *= sensitivity.value;

		/* add mouse X/Y movement to cmd */
		if ((in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1)))
			cmd->sidemove += m_side.value * mouse_x;
		else
			cl.viewangles[YAW] -= m_yaw.value * mouse_x;

		if (in_mlook.state & 1)
			V_StopPitchDrift ();

		if ((in_mlook.state & 1) && !(in_strafe.state & 1)) {
			cl.viewangles[PITCH] += m_pitch.value * mouse_y;

			if (cl.viewangles[PITCH] > 80)
			  cl.viewangles[PITCH] = 80;

			if (cl.viewangles[PITCH] < -70)
			  cl.viewangles[PITCH] = -70;
		} else {
			if ((in_strafe.state & 1) && noclip_anglehack)
			  cmd->upmove -= m_forward.value * mouse_y;
			else
			  cmd->forwardmove -= m_forward.value * mouse_y;
		}
		
		// Reset.
		mouse_x = mouse_y = 0;
	}
}

void IN_GrabMouse(qboolean grab)
{
	if (grab) {
		if (SDL_WM_GrabInput(SDL_GRAB_QUERY) == SDL_GRAB_OFF) {
			SDL_WM_GrabInput(SDL_GRAB_ON);
		}
	} else {
		// Only release capture when in window mode.
		if (mode_state == MODE_WINDOWED) {
			if (SDL_WM_GrabInput(SDL_GRAB_QUERY) == SDL_GRAB_ON) {
				SDL_WM_GrabInput(SDL_GRAB_OFF);
			}
		}
	}

	if (SDL_WM_GrabInput(SDL_GRAB_QUERY) == SDL_GRAB_ON) {
		// Only hide the mouse cursor when input is grabbed.
		SDL_ShowCursor(SDL_DISABLE);
		mouseCaptured = true;
	} else {
		SDL_ShowCursor(SDL_ENABLE);
		mouseCaptured = false;
	}
}

