// Atrapa podzbioru Tk 8.6 C API (tylko kompilacja)
#ifndef _TK
#define _TK
#include <tcl.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void *Tk_PhotoHandle;
typedef struct Tk_PhotoImageBlock { unsigned char *pixelPtr; int width, height, pitch, pixelSize; int offset[4]; } Tk_PhotoImageBlock;
#define TK_PHOTO_COMPOSITE_SET 1
int Tk_Init(Tcl_Interp *interp);
void Tk_MainLoop(void);
Tk_PhotoHandle Tk_FindPhoto(Tcl_Interp *interp, const char *imageName);
int Tk_PhotoPutBlock(Tcl_Interp *interp, Tk_PhotoHandle handle, Tk_PhotoImageBlock *blockPtr, int x, int y, int width, int height, int compRule);
/* jak w prawdziwym tk.h (X11/X.h) */
#define None 0L
#define Bool int
#define Status int
#ifdef __cplusplus
}
#endif
#endif
