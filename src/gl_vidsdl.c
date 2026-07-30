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
// gl_vidnt.c -- SDL 1.2 GL vid component
//
// AMIGA 68k / RTG PORT -- NovaCoder
//
// Changes from the stock Kristian Duske SDL port, all marked NOVA:
//
//  1.  32bpp only. Nova-Mesa renders 32-bit colour exclusively, so every
//      path that negotiated a bit depth (findbpp walk, bpp enumeration,
//      windowed mode inheriting the desktop depth, the menu bpp selector)
//      has been hard-wired to VID_BPP instead. vid_bpp survives as a
//      read-only cvar so existing configs and VID_Restart still work.
//  2.  No SDL_GL_GetProcAddress. Nova-Mesa is statically linked, so the
//      ARB entry points are taken directly rather than looked up.
//  3.  Vsync / SDL_GL_SWAP_CONTROL removed. WGL/GLX concept, no RTG analogue.
//  4.  Anisotropic filtering detection removed (enums may not exist in the
//      Mesa 4.1 headers, and swrast does not implement it).
//  5.  Hardware gamma removed. See VID_Gamma_Init for the TODO.
//  6.  GL attributes are now requested explicitly before every mode set.
//  7.  Assorted robustness fixes: real SDL_SetVideoMode failure detection,
//      NULL-safe glGetString use, SDL_ListModes "any mode" handling,
//      -mode range check, graceful fallback to windowed.
//
// Correctness pass only -- performance tuning for Nova-Mesa/PiStorm is
// deliberately left for a later revision. Spots worth revisiting are
// tagged with "PERF:".

#include "quakedef.h"

#define MAX_MODE_LIST       64      // NOVA: was 600, we will never enumerate that many
#define WARP_WIDTH          320
#define WARP_HEIGHT         200
#define MAXWIDTH            10000
#define MAXHEIGHT           10000
#define BASEWIDTH           320
#define BASEHEIGHT          200
#define SDL_DEFAULT_FLAGS   SDL_OPENGL

// NOVA: Nova-Mesa is 32-bit colour only. This is the single source of truth
// for colour depth in this file -- nothing negotiates any more.
#define VID_BPP             32

// NOVA: requested depth buffer size. 16 keeps the buffer small and the
// z-compare inner loop cheap; raise to 24 only if you see z-fighting.
// PERF: worth benchmarking 16 vs 24 once the port is up.
#define VID_DEPTH_BITS      16

typedef struct {
	modestate_t	type;
	int			width;
	int			height;
	int			modenum;
	int			dib;
	int			fullscreen;
	int			bpp;
	int			halfscreen;
	char		modedesc[17];
} vmode_t;

typedef struct {
	int			width;
	int			height;
} lmode_t;

// NOVA: used when SDL_ListModes() reports "any mode is acceptable"
// ((SDL_Rect **)-1), which is the likely answer from an RTG backend that
// does not enumerate. Trim or extend to suit your Picasso96 mode list.
static const lmode_t fallbackmodes[] = {
	{320, 240},
	{400, 300},
	{512, 384},
	{640, 400},
	{640, 480},
	{800, 600},
	{1024, 768},
	{1280, 720},
	{1280, 1024},
};

#define NUM_FALLBACK_MODES (int)(sizeof(fallbackmodes)/sizeof(fallbackmodes[0]))

const char *gl_vendor;
const char *gl_renderer;
const char *gl_version;
const char *gl_extensions;

qboolean		DDActive;
qboolean		scr_skipupdate;

static vmode_t	modelist[MAX_MODE_LIST];
static int		nummodes;
static vmode_t	badmode;

static qboolean	vid_initialized = false;
static qboolean	windowed, leavecurrentmode;
static qboolean vid_canalttab = false;

SDL_Surface *draw_context;

int			vid_modenum = NO_MODE;
int			vid_realmode;
int			vid_default = MODE_WINDOWED;
unsigned char	vid_curpal[256*3];
static qboolean fullsbardraw = false;

glvert_t glv;
//viddef_t	vid;				// global video state

modestate_t	mode_state = MODE_WINDOWED;

void VID_Menu_Init (void); //johnfitz
void VID_Menu_f (void); //johnfitz
void VID_MenuDraw (void);
void VID_MenuKey (int key);

char *VID_GetModeDescription (int mode);
void ClearAllStates (void);
void VID_UpdateWindowStatus (void);
void GL_Init (void);
void TexMgr_RecalcWarpImageSize (void);

PFNGLMULTITEXCOORD2FARBPROC GL_MTexCoord2fFunc = NULL; //johnfitz
PFNGLACTIVETEXTUREARBPROC GL_SelectTextureFunc = NULL; //johnfitz

qboolean isPermedia = false;
qboolean gl_mtexable = false;
qboolean gl_texture_env_combine = false; //johnfitz
qboolean gl_swap_control = false; //johnfitz -- NOVA: always false, kept for the menu/externs
qboolean gl_anisotropy_able = false; //johnfitz -- NOVA: always false
float gl_max_anisotropy = 1.0f; //johnfitz -- NOVA: must be 1.0, not 0, gl_texturemode divides by it

int	gl_stencilbits; //johnfitz

qboolean vid_locked = false; //johnfitz
qboolean vid_changed = false;

void GL_SetupState (void); //johnfitz

//====================================

//johnfitz -- new cvars
cvar_t		vid_fullscreen = {"vid_fullscreen", "1", true};
cvar_t		vid_width = {"vid_width", "640", true};
cvar_t		vid_height = {"vid_height", "480", true};
cvar_t		vid_bpp = {"vid_bpp", "32", true};		// NOVA: fixed at 32, read-only in practice
cvar_t		vid_refreshrate = {"vid_refreshrate", "60", true};
cvar_t		vid_vsync = {"vid_vsync", "0", true};	// NOVA: registered but inert

cvar_t		_windowed_mouse = {"_windowed_mouse","1", true};
cvar_t		vid_gamma = {"gamma", "1", true}; //johnfitz -- moved here from view.c

//==========================================================================
//
//  GAMMA -- NOVA
//
//  RTG has no per-window gamma ramp, so the SDL_SetGammaRamp path is gone.
//  vid_gammaworks is retained as a definition (always 0) so that any
//  stray extern in the rest of the tree still links.
//
//  TODO: implement gamma in TexMgr instead -- build a 256-entry table from
//  vid_gamma.value, apply it during texture upload, and call
//  TexMgr_ReloadImages() from VID_Gamma_f. Until then the gamma cvar and
//  the options-menu slider do nothing.
//
//==========================================================================

int vid_gammaworks = 0;

void VID_Gamma_SetGamma (void)
{
}

void VID_Gamma_Restore (void)
{
}

void VID_Gamma_Shutdown (void)
{
	VID_Gamma_Restore ();
}

/*
================
VID_Gamma_f -- callback when the cvar changes
================
*/
void VID_Gamma_f (void)
{
	static float oldgamma = -1;

	if (vid_gamma.value == oldgamma)
		return;

	oldgamma = vid_gamma.value;

	// NOVA: nothing to apply yet -- see the TODO above.
	VID_Gamma_SetGamma ();
}

/*
================
VID_Gamma_Init -- call on init
================
*/
void VID_Gamma_Init (void)
{
	vid_gammaworks = 0;
	Cvar_RegisterVariableEx (&vid_gamma, VID_Gamma_f);
}

//==========================================================================
//
//  MODE SETTING
//
//==========================================================================

/*
================
VID_SetGLAttributes -- NOVA

Must be called before every SDL_SetVideoMode. Requesting these explicitly
stops Mesa picking oversized defaults (in particular a 32-bit depth buffer).
================
*/
static void VID_SetGLAttributes (void)
{
	SDL_GL_SetAttribute (SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, VID_DEPTH_BITS);
	SDL_GL_SetAttribute (SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_ALPHA_SIZE, 0);
	SDL_GL_SetAttribute (SDL_GL_STENCIL_SIZE, 0);
}

/*
================
VID_SetMode
================
*/
int VID_SetMode (int modenum)
{
	int			temp;
	qboolean	mode_ok;	// NOVA: was 'stat', which shadows POSIX stat()
	Uint32		flags = SDL_DEFAULT_FLAGS;
	char		caption[64];

	if ((windowed && (modenum != 0)) ||
		(!windowed && (modenum < 1)) ||
		(!windowed && (modenum >= nummodes)))
	{
		Sys_Error ("Bad video mode\n");
	}

// so Con_Printfs don't mess us up by forcing vid and snd updates
	temp = scr_disabled_for_loading;
	scr_disabled_for_loading = true;

	CDAudio_Pause ();

	if (modelist[modenum].type == MODE_WINDOWED)
	{
		mode_state = MODE_WINDOWED;
	}
	else if (modelist[modenum].type == MODE_FULLSCREEN_DEFAULT)
	{
		flags |= SDL_FULLSCREEN;
		mode_state = MODE_FULLSCREEN_DEFAULT;
	}
	else
	{
		Sys_Error ("VID_SetMode: Bad mode type in modelist");
	}

	VID_SetGLAttributes ();

	// NOVA: always VID_BPP -- modelist[].bpp is 32 everywhere, but be explicit
	draw_context = SDL_SetVideoMode (modelist[modenum].width,
									 modelist[modenum].height,
									 VID_BPP,
									 flags);

	// NOVA: the original set this to true unconditionally, so a failed mode
	// set fell through into TexMgr_ReloadImages() with a NULL context and
	// crashed somewhere inside Mesa instead of reporting the real problem.
	mode_ok = (draw_context != NULL);

	if (!mode_ok)
	{
		scr_disabled_for_loading = temp;
		CDAudio_Resume ();
		Sys_Error ("Couldn't set video mode %dx%dx%d (%s)",
				   modelist[modenum].width,
				   modelist[modenum].height,
				   VID_BPP,
				   SDL_GetError ());
	}

	sprintf (caption, "FitzQuake (Amiga) Version %1.2f", FITZQUAKE_VERSION);
	SDL_WM_SetCaption (caption, caption);

	vid.width = modelist[modenum].width;
	vid.height = modelist[modenum].height;
	vid.conwidth = vid.width & 0xFFFFFFF8;
	vid.conheight = vid.conwidth * vid.height / vid.width;
	vid.numpages = 2;
	vid.type = modelist[modenum].type;

	VID_UpdateWindowStatus ();

	CDAudio_Resume ();
	scr_disabled_for_loading = temp;

	vid_modenum = modenum;

	ClearAllStates ();

	if (!msg_suppress_1)
		Con_SafePrintf ("Video mode %s initialized\n", VID_GetModeDescription (vid_modenum));

	vid.recalc_refdef = 1;

	// with SDL, this needs to be done every time the render context is recreated
	TexMgr_ReloadImages ();
	GL_SetupState ();

	vid_changed = false;

	return true;
}

/*
===================
VID_Changed_f -- kristian
===================
*/
void VID_Changed_f (void)
{
	vid_changed = true;
}

/*
===================
VID_Restart -- johnfitz -- change video modes on the fly
===================
*/
void VID_Restart (void)
{
	int	i;

	if (vid_locked || !vid_changed)
		return;

	// NOVA: colour depth is not user selectable, keep the cvar honest
	if ((int)vid_bpp.value != VID_BPP)
		Cvar_Set ("vid_bpp", va("%i", VID_BPP));

	if (vid_fullscreen.value)
	{
		for (i=1; i<nummodes; i++)
		{
			if (modelist[i].width == (int)vid_width.value &&
				modelist[i].height == (int)vid_height.value)
			{
				break;
			}
		}

		if (i == nummodes)
		{
			Con_Printf ("%dx%d is not a valid fullscreen mode\n",
						(int)vid_width.value,
						(int)vid_height.value);
			return;
		}

		windowed = false;
		vid_default = i;
	}
	else //not fullscreen
	{
		if (vid_width.value < 320)
		{
			Con_Printf ("Window width can't be less than 320\n");
			return;
		}

		if (vid_height.value < 200)
		{
			Con_Printf ("Window height can't be less than 200\n");
			return;
		}

		modelist[0].width = (int)vid_width.value;
		modelist[0].height = (int)vid_height.value;
		modelist[0].bpp = VID_BPP;
		sprintf (modelist[0].modedesc, "%dx%dx%d",
				 modelist[0].width,
				 modelist[0].height,
				 modelist[0].bpp);

		windowed = true;
		vid_default = 0;
	}

	VID_SetMode (vid_default);

	vid_canalttab = true;

	//warpimages needs to be recalculated
	TexMgr_RecalcWarpImageSize ();

	//conwidth and conheight need to be recalculated
	vid.conwidth = (scr_conwidth.value > 0) ? (int)scr_conwidth.value : vid.width;
	vid.conwidth = CLAMP (320, vid.conwidth, vid.width);
	vid.conwidth &= 0xFFFFFFF8;
	vid.conheight = vid.conwidth * vid.height / vid.width;

//
// keep cvars in line with actual mode
//
	Cvar_Set ("vid_width", va("%i", modelist[vid_default].width));
	Cvar_Set ("vid_height", va("%i", modelist[vid_default].height));
	Cvar_Set ("vid_bpp", va("%i", VID_BPP));
	Cvar_Set ("vid_fullscreen", (windowed) ? "0" : "1");
}

/*
================
VID_Test -- johnfitz
================
*/
void VID_Test (void)
{
	vmode_t oldmode;

	if (vid_locked || !vid_changed)
		return;

	oldmode = modelist[vid_default];

	VID_Restart ();

	//pop up confirmation dialoge
	if (!SCR_ModalMessage("Would you like to keep this\nvideo mode? (y/n)\n", 5.0f))
	{
		//revert cvars and mode
		Cvar_Set ("vid_width", va("%i", oldmode.width));
		Cvar_Set ("vid_height", va("%i", oldmode.height));
		Cvar_Set ("vid_fullscreen", (oldmode.type == MODE_WINDOWED) ? "0" : "1");
		vid_changed = true;
		VID_Restart ();
	}
}

/*
================
VID_Unlock -- johnfitz
================
*/
void VID_Unlock (void)
{
	vid_locked = false;

	//sync up cvars in case they were changed during the lock
	Cvar_Set ("vid_width", va("%i", modelist[vid_default].width));
	Cvar_Set ("vid_height", va("%i", modelist[vid_default].height));
	Cvar_Set ("vid_bpp", va("%i", VID_BPP));
	Cvar_Set ("vid_fullscreen", (windowed) ? "0" : "1");
}

/*
================
VID_UpdateWindowStatus
================
*/
void VID_UpdateWindowStatus (void)
{
}

//==============================================================================
//
//	OPENGL STUFF
//
//==============================================================================

/*
===============
GL_MakeNiceExtensionsList -- johnfitz
===============
*/
char *GL_MakeNiceExtensionsList (const char *in)
{
	char *copy, *token, *out;
	int i, count, len;

	if (!in)		// NOVA: NULL-safe
		in = "";

	len = (int) strlen (in);

	//each space will be replaced by 4 chars, so count the spaces before we malloc
	for (i = 0, count = 1; i < len; i++)
		if (in[i] == ' ')
			count++;

	out = Z_Malloc (len + count*3 + 1);
	out[0] = 0;

	copy = Z_Malloc (len + 1);
	strcpy (copy, in);

	for (token = strtok(copy, " "); token; token = strtok(NULL, " "))
	{
		strcat (out, "\n   ");
		strcat (out, token);
	}

	Z_Free (copy);
	return out;
}

/*
===============
GL_Info_f -- johnfitz
===============
*/
void GL_Info_f (void)
{
	static char *gl_extensions_nice = NULL;

	if (!gl_extensions_nice)
		gl_extensions_nice = GL_MakeNiceExtensionsList (gl_extensions);

	Con_SafePrintf ("GL_VENDOR: %s\n", gl_vendor);
	Con_SafePrintf ("GL_RENDERER: %s\n", gl_renderer);
	Con_SafePrintf ("GL_VERSION: %s\n", gl_version);
	Con_Printf ("GL_EXTENSIONS: %s\n", gl_extensions_nice);
}

/*
===============
CheckArrayExtensions

NOVA: kept as an empty stub so any remaining callers still link. Mesa 4.1
exposes vertex arrays as core GL 1.1, so there is nothing to detect and the
GL_EXT_vertex_array entry points do not exist.
===============
*/
void CheckArrayExtensions (void)
{
}

/*
===============
GL_CheckExtensions -- johnfitz

NOVA: rewritten. Nova-Mesa is statically linked so there is no runtime
symbol lookup -- the ARB entry points are taken directly. Vsync and
anisotropy detection are gone entirely.
===============
*/
void GL_CheckExtensions (void)
{
	//
	// multitexture
	//
	if (COM_CheckParm("-nomtex"))
	{
		Con_Printf ("WARNING: Multitexture disabled at command line\n");
	}
	else if (strstr(gl_extensions, "GL_ARB_multitexture"))
	{
	    GLint units = 0;

	    glGetIntegerv (GL_MAX_TEXTURE_UNITS_ARB, &units);   /* 0x84E2 */

	    if (units < 2)
	    {
	        Con_Printf ("WARNING: multitexture present but only %i unit(s)\n", (int)units);
	    }
	    else
	    {
	        GL_MTexCoord2fFunc   = (PFNGLMULTITEXCOORD2FARBPROC) glMultiTexCoord2fARB;
	        GL_SelectTextureFunc = (PFNGLACTIVETEXTUREARBPROC) glActiveTextureARB;
	        TEXTURE0 = GL_TEXTURE0_ARB;
	        TEXTURE1 = GL_TEXTURE1_ARB;
	        gl_mtexable = true;
	        Con_Printf ("FOUND: ARB_multitexture (%i units)\n", (int)units);
	    }
	}
	else
	{
		Con_Printf ("WARNING: multitexture not supported (extension not found)\n");
	}

	//
	// texture_env_combine
	//
	// PERF: this drives FitzQuake's overbright lightmaps via GL_RGB_SCALE,
	// which puts swrast on the slow generic texenv path. Try -nocombine and
	// compare before deciding to keep it.
	//
	if (COM_CheckParm("-nocombine"))
	{
		Con_Printf ("WARNING: texture_env_combine disabled at command line\n");
	}
	else if (strstr(gl_extensions, "GL_ARB_texture_env_combine"))
	{
		Con_Printf ("FOUND: ARB_texture_env_combine\n");
		gl_texture_env_combine = true;
	}
	else if (strstr(gl_extensions, "GL_EXT_texture_env_combine"))
	{
		Con_Printf ("FOUND: EXT_texture_env_combine\n");
		gl_texture_env_combine = true;
	}
	else
	{
		Con_Printf ("WARNING: texture_env_combine not supported\n");
	}

	//
	// NOVA: swap control removed -- WGL/GLX only, no RTG equivalent.
	// NOVA: anisotropic filtering removed -- not implemented by Mesa 4.1 swrast.
	//
	gl_swap_control = false;
	gl_anisotropy_able = false;
	gl_max_anisotropy = 1.0f;
}

/*
===============
GL_SetupState -- johnfitz

does all the stuff from GL_Init that needs to be done every time a new GL
render context is created
===============
*/
void GL_SetupState (void)
{
	glClearColor (0.15,0.15,0.15,0);
	glCullFace (GL_BACK);
	glFrontFace (GL_CW);
	glEnable (GL_TEXTURE_2D);

	// PERF: FitzQuake only needs alpha test for sky and fence textures.
	// Enabling it globally costs a per-fragment test on every surface.
	glEnable (GL_ALPHA_TEST);
	glAlphaFunc (GL_GREATER, 0.666);

	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel (GL_FLAT);

	// PERF: GL_FASTEST here may buy back real time on swrast. Left at
	// NICEST for the correctness pass so the output matches the reference.
	glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glDepthRange (0, 1);
	glDepthFunc (GL_LEQUAL);
}

/*
===============
GL_Init
===============
*/
void GL_Init (void)
{
	// NOVA: NULL-safe -- a bad context would otherwise blow up in strstr()
	gl_vendor     = (const char *) glGetString (GL_VENDOR);
	gl_renderer   = (const char *) glGetString (GL_RENDERER);
	gl_version    = (const char *) glGetString (GL_VERSION);
	gl_extensions = (const char *) glGetString (GL_EXTENSIONS);

	if (!gl_vendor)     gl_vendor = "unknown";
	if (!gl_renderer)   gl_renderer = "unknown";
	if (!gl_version)    gl_version = "unknown";
	if (!gl_extensions) gl_extensions = "";

	Con_SafePrintf ("GL_RENDERER: %s\n", gl_renderer);
	Con_SafePrintf ("GL_VERSION: %s\n", gl_version);

	GL_CheckExtensions ();

	Cmd_AddCommand ("gl_info", GL_Info_f);

	Cvar_RegisterVariableEx (&vid_vsync, VID_Changed_f);

	// NOVA: PowerVR / Permedia driver sniffing removed, neither exists here.
	isPermedia = false;

	GL_SetupState ();
}

/*
=================
GL_BeginRendering
=================
*/
void GL_BeginRendering (int *x, int *y, int *width, int *height)
{
	*x = *y = 0;
	*width = vid.width;
	*height = vid.height;
}

/*
=================
GL_EndRendering
=================
*/
void GL_EndRendering (void)
{
	if (!scr_skipupdate || block_drawing)
		SDL_GL_SwapBuffers ();

	if (fullsbardraw)
		Sbar_Changed ();
}

void VID_SetDefaultMode (void)
{
}

void VID_Shutdown (void)
{
	if (vid_initialized)
	{
		vid_canalttab = false;
		VID_Gamma_Shutdown ();

		SDL_QuitSubSystem (SDL_INIT_VIDEO);
		draw_context = NULL;
	}
}

//==========================================================================

/*
================
ClearAllStates
================
*/
void ClearAllStates (void)
{
	Key_ClearStates ();
}

//==========================================================================
//
//  COMMANDS
//
//==========================================================================

int VID_NumModes (void)
{
	return nummodes;
}

vmode_t *VID_GetModePtr (int modenum)
{
	if ((modenum >= 0) && (modenum < nummodes))
		return &modelist[modenum];
	else
		return &badmode;
}

/*
=================
VID_GetModeDescription
=================
*/
char *VID_GetModeDescription (int mode)
{
	char		*pinfo;
	vmode_t		*pv;
	static char	temp[100];

	if ((mode < 0) || (mode >= nummodes))
		return NULL;

	if (!leavecurrentmode)
	{
		pv = VID_GetModePtr (mode);
		pinfo = pv->modedesc;
	}
	else
	{
		sprintf (temp, "Desktop resolution (%ix%ix%i)",
				 modelist[MODE_FULLSCREEN_DEFAULT].width,
				 modelist[MODE_FULLSCREEN_DEFAULT].height,
				 modelist[MODE_FULLSCREEN_DEFAULT].bpp);
		pinfo = temp;
	}

	return pinfo;
}

/*
=================
VID_GetExtModeDescription
=================
*/
char *VID_GetExtModeDescription (int mode)
{
	static char	pinfo[40];
	vmode_t		*pv;

	if ((mode < 0) || (mode >= nummodes))
		return NULL;

	pv = VID_GetModePtr (mode);
	if (modelist[mode].type == MODE_FULLSCREEN_DEFAULT)
	{
		if (!leavecurrentmode)
		{
			sprintf (pinfo,"%s fullscreen", pv->modedesc);
		}
		else
		{
			sprintf (pinfo, "Desktop resolution (%ix%ix%i)",
					 modelist[MODE_FULLSCREEN_DEFAULT].width,
					 modelist[MODE_FULLSCREEN_DEFAULT].height,
					 modelist[MODE_FULLSCREEN_DEFAULT].bpp);
		}
	}
	else
	{
		if (mode_state == MODE_WINDOWED)
			sprintf (pinfo, "%s windowed", pv->modedesc);
		else
			sprintf (pinfo, "windowed");
	}

	return pinfo;
}

void VID_DescribeCurrentMode_f (void)
{
	Con_Printf ("%s\n", VID_GetExtModeDescription (vid_modenum));
}

/*
=================
VID_DescribeModes_f -- johnfitz
=================
*/
void VID_DescribeModes_f (void)
{
	int			i, lnummodes, t;
	vmode_t		*pv;
	int			lastwidth=0, lastheight=0, count=0;

	lnummodes = VID_NumModes ();

	t = leavecurrentmode;
	leavecurrentmode = 0;

	for (i=1 ; i<lnummodes ; i++)
	{
		pv = VID_GetModePtr (i);
		if (lastwidth != pv->width || lastheight != pv->height)
		{
			if (count>0)
				Con_SafePrintf ("\n");
			Con_SafePrintf ("   %4i x %4i x %i", pv->width, pv->height, pv->bpp);
			lastwidth = pv->width;
			lastheight = pv->height;
			count++;
		}
	}
	Con_Printf ("\n%i modes\n", count);

	leavecurrentmode = t;
}

//==========================================================================
//
//  INIT
//
//==========================================================================

/*
=================
VID_InitDIB -- windowed mode

NOVA: no longer inherits the desktop depth from SDL_GetVideoInfo(), since
Nova-Mesa only renders 32-bit regardless of what the Workbench screen is.
=================
*/
void VID_InitDIB (void)
{
	modelist[0].type = MODE_WINDOWED;

	if (COM_CheckParm("-width"))
		modelist[0].width = Q_atoi(com_argv[COM_CheckParm("-width")+1]);
	else
		modelist[0].width = 640;

	if (modelist[0].width < 320)
		modelist[0].width = 320;

	if (COM_CheckParm("-height"))
		modelist[0].height = Q_atoi(com_argv[COM_CheckParm("-height")+1]);
	else
		modelist[0].height = modelist[0].width * 240/320;

	if (modelist[0].height < 200)
		modelist[0].height = 200;

	modelist[0].bpp = VID_BPP;

	sprintf (modelist[0].modedesc, "%dx%dx%d",
			 modelist[0].width,
			 modelist[0].height,
			 modelist[0].bpp);

	modelist[0].modenum = MODE_WINDOWED;
	modelist[0].dib = 1;
	modelist[0].fullscreen = 0;
	modelist[0].halfscreen = 0;

	nummodes = 1;
}

/*
=================
VID_AddMode -- NOVA helper
=================
*/
static void VID_AddMode (int w, int h)
{
	int k;

	if (nummodes >= MAX_MODE_LIST)
		return;

	if (w > MAXWIDTH || h > MAXHEIGHT || w < 320 || h < 200)
		return;

	// reject duplicates
	for (k = 1; k < nummodes; k++)
	{
		if (modelist[k].width == w && modelist[k].height == h)
			return;
	}

	modelist[nummodes].type = MODE_FULLSCREEN_DEFAULT;
	modelist[nummodes].width = w;
	modelist[nummodes].height = h;
	modelist[nummodes].modenum = 0;
	modelist[nummodes].halfscreen = 0;
	modelist[nummodes].dib = 1;
	modelist[nummodes].fullscreen = 1;
	modelist[nummodes].bpp = VID_BPP;

	sprintf (modelist[nummodes].modedesc, "%dx%dx%d", w, h, VID_BPP);

	nummodes++;
}

/*
=================
VID_InitFullDIB

NOVA: enumerates 32bpp only. Also handles SDL_ListModes() returning
(SDL_Rect **)-1 ("any mode is fine"), which the original silently skipped --
that left nummodes == 1 and tripped the "No RGB fullscreen modes available"
error path on any backend that does not enumerate.
=================
*/
void VID_InitFullDIB (void)
{
	SDL_PixelFormat		format;
	SDL_Rect			**modes;
	Uint32				flags;
	int					j, originalnummodes;

	originalnummodes = nummodes;

	memset (&format, 0, sizeof(format));
	format.palette = NULL;
	format.BitsPerPixel = VID_BPP;

	flags = SDL_DEFAULT_FLAGS | SDL_FULLSCREEN;
	modes = SDL_ListModes (&format, flags);

	if (modes == (SDL_Rect **)-1)
	{
		// any dimension is acceptable -- use our own list
		Con_SafePrintf ("SDL reports all modes available, using built-in list\n");

		for (j = 0; j < NUM_FALLBACK_MODES; j++)
			VID_AddMode (fallbackmodes[j].width, fallbackmodes[j].height);
	}
	else if (modes != (SDL_Rect **)0)
	{
		for (j = 0; modes[j]; j++)
			VID_AddMode (modes[j]->w, modes[j]->h);
	}

	if (nummodes == originalnummodes)
		Con_SafePrintf ("No fullscreen modes found\n");
}

/*
===================
VID_Init
===================
*/
void VID_Init (void)
{
	const SDL_VideoInfo	*info;
	int					i, width, height;
	char				gldir[MAX_OSPATH];

	Cvar_RegisterVariableEx (&vid_fullscreen, VID_Changed_f);
	Cvar_RegisterVariableEx (&vid_width, VID_Changed_f);
	Cvar_RegisterVariableEx (&vid_height, VID_Changed_f);
	Cvar_RegisterVariableEx (&vid_bpp, VID_Changed_f);
	Cvar_RegisterVariable (&_windowed_mouse);

	Cmd_AddCommand ("vid_unlock", VID_Unlock);
	Cmd_AddCommand ("vid_restart", VID_Restart);
	Cmd_AddCommand ("vid_test", VID_Test);
	Cmd_AddCommand ("vid_describecurrentmode", VID_DescribeCurrentMode_f);
	Cmd_AddCommand ("vid_describemodes", VID_DescribeModes_f);

	if (SDL_InitSubSystem (SDL_INIT_VIDEO) == -1)
		Sys_Error ("Could not initialize SDL Video: %s", SDL_GetError());

	// NOVA: colour depth is not negotiable
	Cvar_Set ("vid_bpp", va("%i", VID_BPP));

	VID_InitDIB ();
	VID_InitFullDIB ();

	if (COM_CheckParm("-window"))
	{
		windowed = true;
		vid_default = MODE_WINDOWED;
	}
	else if (nummodes == 1)
	{
		// NOVA: fall back to windowed rather than dying outright
		Con_SafePrintf ("WARNING: no fullscreen modes available, using windowed\n");
		windowed = true;
		vid_default = MODE_WINDOWED;
	}
	else
	{
		windowed = false;

		if (COM_CheckParm("-mode"))
		{
			// NOVA: range check, the original would Sys_Error inside VID_SetMode
			vid_default = Q_atoi (com_argv[COM_CheckParm("-mode")+1]);

			if (vid_default < 1 || vid_default >= nummodes)
			{
				Con_SafePrintf ("WARNING: -mode %i out of range, ignoring\n", vid_default);
				vid_default = 0;
			}
		}

		if (!vid_default && COM_CheckParm("-current"))
		{
			info = SDL_GetVideoInfo ();
			modelist[MODE_FULLSCREEN_DEFAULT].width = info->current_w;
			modelist[MODE_FULLSCREEN_DEFAULT].height = info->current_h;
			vid_default = MODE_FULLSCREEN_DEFAULT;
			leavecurrentmode = 1;
		}

		if (!vid_default)
		{
			// NOVA: no findbpp walk -- everything in the list is 32bpp, so we
			// only ever match on dimensions.
			width = COM_CheckParm("-width") ?
					Q_atoi(com_argv[COM_CheckParm("-width")+1]) : 640;

			height = COM_CheckParm("-height") ?
					 Q_atoi(com_argv[COM_CheckParm("-height")+1]) : 0;

			// if they want to force it, add the specified mode to the list
			if (COM_CheckParm("-force") && height)
				VID_AddMode (width, height);

			for (i = 1; i < nummodes; i++)
			{
				if (modelist[i].width != width)
					continue;

				if (height && modelist[i].height != height)
					continue;

				vid_default = i;
				break;
			}

			if (!vid_default)
			{
				Con_SafePrintf ("WARNING: %ix%i not available, using %s\n",
								width, height, modelist[1].modedesc);
				vid_default = 1;
			}
		}
	}

	vid_initialized = true;

	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));

	VID_SetMode (vid_default);
	GL_Init ();

	sprintf (gldir, "%s/glquake", com_gamedir);
	Sys_Mkdir (gldir);

	vid_realmode = vid_modenum;

	vid_menucmdfn = VID_Menu_f;
	vid_menudrawfn = VID_MenuDraw;
	vid_menukeyfn = VID_MenuKey;

	strcpy (badmode.modedesc, "Bad mode");
	vid_canalttab = true;

	if (COM_CheckParm("-fullsbar"))
		fullsbardraw = true;

	VID_Gamma_Init ();
	VID_Menu_Init ();

	//johnfitz -- command line vid settings should override config file settings.
	if (COM_CheckParm("-width") || COM_CheckParm("-height") ||
		COM_CheckParm("-mode") || COM_CheckParm("-window"))
	{
		vid_locked = true;
	}
}

/*
================
VID_SyncCvars -- johnfitz
================
*/
void VID_SyncCvars (void)
{
	Cvar_Set ("vid_width", va("%i", modelist[vid_default].width));
	Cvar_Set ("vid_height", va("%i", modelist[vid_default].height));
	Cvar_Set ("vid_bpp", va("%i", VID_BPP));
	Cvar_Set ("vid_fullscreen", (windowed) ? "0" : "1");
	// NOVA: no vsync query, gl_swap_control is always false
}

//==========================================================================
//
//  VIDEO MENU -- johnfitz
//
//  NOVA: trimmed from 7 items to 5. Colour depth is display-only (always 32),
//  and the refresh rate and vertical sync rows are gone.
//
//==========================================================================

void M_Menu_Options_f (void);
void M_Print (int cx, int cy, char *str);
void M_PrintWhite (int cx, int cy, char *str);
void M_DrawCharacter (int cx, int line, int num);
void M_DrawTransPic (int x, int y, qpic_t *pic);
void M_DrawPic (int x, int y, qpic_t *pic);
void M_DrawCheckbox (int x, int y, int on);

extern qboolean	m_entersound;

#define VIDEO_OPTIONS_ITEMS 5

// 0 video mode, 1 colour depth (read only), 2 fullscreen, 3 test, 4 apply
int		video_cursor_table[] = {48, 56, 64, 80, 88};
int		video_options_cursor = 0;

typedef struct {int width,height;} vid_menu_mode;

vid_menu_mode vid_menu_modes[MAX_MODE_LIST];
int vid_menu_nummodes = 0;

/*
================
VID_Menu_Init
================
*/
void VID_Menu_Init (void)
{
	int i,j,h,w;

	vid_menu_nummodes = 0;

	for (i=1;i<nummodes;i++) //start i at mode 1 because 0 is windowed mode
	{
		w = modelist[i].width;
		h = modelist[i].height;

		for (j=0;j<vid_menu_nummodes;j++)
		{
			if (vid_menu_modes[j].width == w &&
				vid_menu_modes[j].height == h)
				break;
		}

		if (j==vid_menu_nummodes)
		{
			vid_menu_modes[j].width = w;
			vid_menu_modes[j].height = h;
			vid_menu_nummodes++;
		}
	}
}

/*
================
VID_Menu_ChooseNextMode
================
*/
void VID_Menu_ChooseNextMode (int dir)
{
	int i;

	if (vid_menu_nummodes < 1)		// NOVA: nothing to cycle through
		return;

	for (i=0;i<vid_menu_nummodes;i++)
	{
		if (vid_menu_modes[i].width == vid_width.value &&
			vid_menu_modes[i].height == vid_height.value)
			break;
	}

	if (i==vid_menu_nummodes) //can't find it in list, so it must be a custom windowed res
	{
		i = 0;
	}
	else
	{
		i+=dir;
		if (i>=vid_menu_nummodes)
			i = 0;
		else if (i<0)
			i = vid_menu_nummodes-1;
	}

	Cvar_Set ("vid_width",va("%i",vid_menu_modes[i].width));
	Cvar_Set ("vid_height",va("%i",vid_menu_modes[i].height));
}

/*
================
VID_MenuKey
================
*/
void VID_MenuKey (int key)
{
	switch (key)
	{
	case K_ESCAPE:
		VID_SyncCvars ();
		S_LocalSound ("misc/menu1.wav");
		M_Menu_Options_f ();
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		video_options_cursor--;
		if (video_options_cursor < 0)
			video_options_cursor = VIDEO_OPTIONS_ITEMS-1;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		video_options_cursor++;
		if (video_options_cursor >= VIDEO_OPTIONS_ITEMS)
			video_options_cursor = 0;
		break;

	case K_LEFTARROW:
		S_LocalSound ("misc/menu3.wav");
		switch (video_options_cursor)
		{
		case 0:
			VID_Menu_ChooseNextMode (-1);
			break;
		case 2:
			Cbuf_AddText ("toggle vid_fullscreen\n");
			break;
		default:
			break;
		}
		break;

	case K_RIGHTARROW:
		S_LocalSound ("misc/menu3.wav");
		switch (video_options_cursor)
		{
		case 0:
			VID_Menu_ChooseNextMode (1);
			break;
		case 2:
			Cbuf_AddText ("toggle vid_fullscreen\n");
			break;
		default:
			break;
		}
		break;

	case K_ENTER:
		m_entersound = true;
		switch (video_options_cursor)
		{
		case 0:
			VID_Menu_ChooseNextMode (1);
			break;
		case 2:
			Cbuf_AddText ("toggle vid_fullscreen\n");
			break;
		case 3:
			Cbuf_AddText ("vid_test\n");
			break;
		case 4:
			Cbuf_AddText ("vid_restart\n");
			key_dest = key_game;
			menu_state = m_none;
			break;
		default:
			break;
		}
		break;

	default:
		break;
	}
}

/*
================
VID_MenuDraw
================
*/
void VID_MenuDraw (void)
{
	int i = 0;
	qpic_t *p;
	char *title;

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp"));

	p = Draw_CachePic ("gfx/p_option.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);

	// title
	title = "Video Options";
	M_PrintWhite ((320-8*strlen(title))/2, 32, title);

	// options
	M_Print (16, video_cursor_table[i], "            Video mode");
	M_Print (216, video_cursor_table[i], va("%ix%i", (int)vid_width.value, (int)vid_height.value));
	i++;

	M_Print (16, video_cursor_table[i], "           Color depth");
	M_Print (216, video_cursor_table[i], va("%i", VID_BPP));
	i++;

	M_Print (16, video_cursor_table[i], "            Fullscreen");
	M_DrawCheckbox (216, video_cursor_table[i], (int)vid_fullscreen.value);
	i++;

	M_Print (16, video_cursor_table[i], "          Test changes");
	i++;

	M_Print (16, video_cursor_table[i], "         Apply changes");

	// cursor
	M_DrawCharacter (200, video_cursor_table[video_options_cursor], 12+((int)(realtime*4)&1));
}

/*
================
VID_Menu_f
================
*/
void VID_Menu_f (void)
{
	IN_GrabMouse(false);
	key_dest = key_menu;
	menu_state = m_video;
	m_entersound = true;

	//set all the cvars to match the current mode when entering the menu
	VID_SyncCvars ();
}
