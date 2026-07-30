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
extern cvar_t logfile_active;


void Sys_Printf(char *message, ...)
{
	va_list argptr;
	char text[MAX_MESSAGE_SIZE];

    if (logfile_active.value) {
        va_start (argptr, message);
        vsprintf (text, message, argptr);
        va_end (argptr);
    
        if (!logfile) {
            logfile = fopen("q_console.txt", "w");
            if (logfile) {
                setbuf(logfile, NULL);
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
    vsprintf (text, message, argptr);
    va_end (argptr);

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

