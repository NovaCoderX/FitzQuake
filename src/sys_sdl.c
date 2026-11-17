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


static quakeparms_t quakeparms; 

static FILE* logfile = NULL;

// NOVA: the console log is buffered. It used to be unbuffered, which made
// every console line a separate write to disk, and disk writes are slow on a
// 68k. The buffer goes to disk when it fills, before shutting down, and
// before a fatal error is handled; only a hard crash loses what is in it.
static char logbuffer[8192];
extern cvar_t logfile_active;


void Sys_Printf(char *message, ...)
{
	va_list argptr;
	char text[MAX_MESSAGE_SIZE];

    if (logfile_active.value) {
        // NOVA: bounded. Con_Printf passes messages of up to 4096 bytes and
        // this buffer is 2048; "gl_info" alone comes to about 2000.
        va_start (argptr, message);
        vsnprintf (text, sizeof(text), message, argptr);
        va_end (argptr);
    
        if (!logfile) {
            logfile = fopen("q_console.txt", "w");
            if (logfile) {
                setvbuf(logfile, logbuffer, _IOFBF, sizeof(logbuffer)); // NOVA: was setbuf(logfile, NULL)
            }
        }
        
    	if (logfile) {
    	  fwrite(text, 1, strlen(text), logfile);
    	}
    }
}

void Sys_Error(char *message, ...)
{
	va_list argptr;
	char text[MAX_MESSAGE_SIZE];
	FILE *errorLogfile;
    
    va_start (argptr, message);
    vsnprintf (text, sizeof(text), message, argptr); // NOVA: bounded
    va_end (argptr);

    // NOVA: get the buffered console log onto disk before anything else
    if (logfile) {
        fflush(logfile);
    }

    errorLogfile = fopen("q_error.txt", "w");
    if (errorLogfile) {
    	fwrite(text, 1, strlen(text), errorLogfile);
    	fflush(errorLogfile);
        fclose(errorLogfile);
    }

	Sys_Quit(EXIT_FAILURE);
}

double Sys_FloatTime()
{
    return SDL_GetTicks() / 1000.0;
}

void Sys_SendKeyEvents()
{
	SDL_Event event;

	while (SDL_PollEvent(&event))
	{
		switch (event.type) {
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				Key_Event(&event.key);
				break;

			case SDL_MOUSEMOTION:
                if (mouseCaptured) {
			         IN_MouseMove(event.motion.xrel, event.motion.yrel);
                }
			  break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            {
            	if (mouseCaptured) {
                    Button_Event(&event.button);
                }
                break;
            }

			case SDL_QUIT:
				Sys_Quit(EXIT_SUCCESS);
				break;
		}
	}
}

static void Sys_Init(void)
{
    int p;
    int hunksize = DEFAULT_HUNK_SIZE;

    p = COM_CheckParm ("-hunksize");
    if (p)
	{
		if (p < com_argc-1) {
			hunksize = Q_atoi(com_argv[p+1]);
        }
    }

    // Hunk memory size
    quakeparms.memsize = hunksize * 1024 * 1024;

    // 16-byte align the memory
    quakeparms.memsize = (quakeparms.memsize+15)&~15;
	quakeparms.membase = malloc(quakeparms.memsize);
	if (!quakeparms.membase) {
		Sys_Error ("Not enough memory free, cannot execute the game");
    }
}

void Sys_Quit(int exitCode)
{
	// NOVA: the log is buffered now; write it out before shutting down, in
	// case the shutdown itself goes wrong
	if (logfile) {
		fflush(logfile);
	}

	Host_Shutdown();

	if (quakeparms.membase) {
        free(quakeparms.membase);
        quakeparms.membase = NULL;
    }

	if (logfile) {
		fclose(logfile);
		logfile = NULL;
	}

	exit(exitCode);
}
 
//=============================================================================

static void Sys_RunGameLoop(void)
{
    double newtime;
    double oldtime;
        
    // Never exits
    oldtime = Sys_FloatTime();
    
    while(true) {
        newtime = Sys_FloatTime();
        Host_Frame(newtime - oldtime);
        oldtime = newtime;
	}
}

void quake_main(int argcWb, char *argvWb[], char* homePath)
{
   	quakeparms.basedir = homePath;
    quakeparms.cachedir = NULL;
    quakeparms.argc = argcWb;
	quakeparms.argv = argvWb;

    COM_InitArgv(quakeparms.argc, quakeparms.argv);

    // Setup the system.
    Sys_Init();

    // Setup the host.
	Host_Init(&quakeparms);

    // Never returns.
    Sys_RunGameLoop();
}

