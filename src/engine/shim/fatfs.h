/* ELXAR-Two shim: the plugin has no SD card. Only the type names that the firmware's headers mention. */
#ifndef ELXAR_SHIM_FATFS_H
#define ELXAR_SHIM_FATFS_H
typedef int FRESULT;
typedef struct { int unused; } FIL;
typedef struct { int unused; } DIR;
typedef struct { int unused; } FILINFO;
typedef struct { int unused; } FATFS;
typedef unsigned int UINT;
#endif
