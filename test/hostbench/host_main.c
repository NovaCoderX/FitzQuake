// Host replacements for amiga_main.c and sys_file_amiga.c (the only two
// source files that include AmigaOS headers). Nothing here is used by the
// Amiga build.
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

void quake_main(int argc, char **argv, char *homePath);
void Sys_Error(char *error, ...);

int main(int argc, char **argv){ quake_main(argc, argv, "."); return 0; }

#define MAX_HANDLES 30
static FILE *sys_handles[MAX_HANDLES];

static int findhandle(void){ int i; for (i = 1; i < MAX_HANDLES; i++) if (!sys_handles[i]) return i; Sys_Error("out of handles"); return -1; }
static int filelength(FILE *f){ int pos = ftell(f), end; fseek(f, 0, SEEK_END); end = ftell(f); fseek(f, pos, SEEK_SET); return end; }

int Sys_FileOpenRead(char *path, int *hndl){ FILE *f; int i = findhandle(); f = fopen(path, "rb"); if (!f) { *hndl = -1; return -1; } sys_handles[i] = f; *hndl = i; return filelength(f); }
int Sys_FileOpenWrite(char *path){ FILE *f; int i = findhandle(); f = fopen(path, "wb"); if (!f) Sys_Error("Error opening %s: %s", path, strerror(errno)); sys_handles[i] = f; return i; }
void Sys_FileClose(int h){ fclose(sys_handles[h]); sys_handles[h] = NULL; }
void Sys_FileSeek(int h, int pos){ fseek(sys_handles[h], pos, SEEK_SET); }
int Sys_FileRead(int h, void *dest, int count){ return fread(dest, 1, count, sys_handles[h]); }
int Sys_FileWrite(int h, void *data, int count){ return fwrite(data, 1, count, sys_handles[h]); }
int Sys_FileTime(char *path){ FILE *f = fopen(path, "r"); if (f) { fclose(f); return 1; } return -1; }
void Sys_Mkdir(char *path){ mkdir(path, 0777); }
