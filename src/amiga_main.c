#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Amiga includes.
#include <proto/exec.h>
#include <proto/dos.h>
#include <clib/icon_protos.h>
#include <workbench/startup.h>

const char* ID = "$VER: FitzQuake 0.80.01\r\n";

/**
 * Force GCC to emit a symbol named EXACTLY "errno" in the final object file.
 * This satisfies libvorbis without colliding with libnix's "__errno".
 */
int vorbis_compatibility_errno __asm__("_errno");

// the startup message from workbench or 0 if started via cli
extern struct WBStartup* _WBenchMsg;

#define MAX_SYSTEM_PATH 255
#define MAX_ARGVS 100
#define MIN_STACK_BYTES 300000

void quake_main(int argc, char** argv, char* homePath);

static int myargc = 0;
static char* myargv[MAX_ARGVS];
static BPTR lock = 0;

static void amiga_exit(void) {
	free(myargv[0]);

	if (lock) {
		CurrentDir(lock);
	}
}

int main(int argc, char** argv) {
	char path[MAX_SYSTEM_PATH];
	struct Process* thisProcess;
	ULONG stackSize = 0;

	thisProcess = (struct Process*)FindTask(NULL);

	if (thisProcess) {
		stackSize = thisProcess->pr_Task.tc_SPUpper - thisProcess->pr_Task.tc_SPLower;
	}

	if (stackSize < MIN_STACK_BYTES) {
		printf("Stack size (%ld bytes) is too small...\n", stackSize);
		return EXIT_FAILURE;
	}

	if (!_WBenchMsg) {
		// Started from the shell.
		GetCurrentDirName((STRPTR )path, MAX_SYSTEM_PATH);

		// Never returns.
		quake_main(argc, argv, path);
	} else {
		// Started from WB.
		struct DiskObject *diskObject;
		char *toolType;
		int i;

        // These command line arguments are flags
        char *flags[] = {
            "-nosound",
            "-nocdaudio",
            "-rogue",
            "-hipnotic"
        };

        // These command line arguments each take a value
        char *settings[] = {
            "-game", "-hunksize", "-zone"
        };

		// Get path
		NameFromLock(_WBenchMsg->sm_ArgList[0].wa_Lock, (STRPTR )path, MAX_SYSTEM_PATH);

		// Set working directory.
		lock = CurrentDir(_WBenchMsg->sm_ArgList[0].wa_Lock);

		// Setup command line arguments.
		myargv[myargc] = (char*)malloc(strlen("FitzQuake") + 1);
		strcpy(myargv[myargc++], "FitzQuake");

        // Process Tooltypes.
        diskObject = GetDiskObject((char*)_WBenchMsg->sm_ArgList[0].wa_Name);

        if (diskObject != NULL) {
    		// Process DOS command line flags.
    		for (i = 0; i < sizeof(flags)/sizeof(flags[0]); i++) {
    			if (FindToolType(diskObject->do_ToolTypes, &flags[i][1]) != NULL) {
    				myargv[myargc] = (char *)malloc(strlen(flags[i])+1);
                    strcpy(myargv[myargc++], flags[i]);
    			}
    		}

    		// Process DOS command line settings.
    		for (i = 0; i < sizeof(settings)/sizeof(settings[0]); i++) {
    			if ((toolType = FindToolType(diskObject->do_ToolTypes, &settings[i][1])) != NULL) {
    				myargv[myargc] = (char *)malloc(strlen(settings[i])+1);
    				strcpy(myargv[myargc++], settings[i]);
                    myargv[myargc] = (char *)malloc(strlen(toolType)+1);
    				strcpy(myargv[myargc++], toolType);
    			}
    		}
        }

		// Clean up on exit.
		atexit(amiga_exit);

		// Never returns.
		quake_main(myargc, myargv, path);
	}

    // Keep compiler happy!
    return EXIT_SUCCESS;
}
