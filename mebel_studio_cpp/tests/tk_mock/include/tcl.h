// Atrapa podzbioru Tcl 8.6 C API (sygnatury z tcl.h 8.6) — tylko do sprawdzenia kompilacji tkcore.cpp
#ifndef _TCL
#define _TCL
#ifdef __cplusplus
extern "C" {
#endif
typedef void* ClientData;
typedef struct Tcl_Interp Tcl_Interp;
typedef struct Tcl_Obj { int refCount; char* bytes; int length; } Tcl_Obj;
typedef struct Tcl_Command_ *Tcl_Command;
typedef int (Tcl_ObjCmdProc)(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
typedef void (Tcl_CmdDeleteProc)(ClientData clientData);
#define TCL_OK 0
#define TCL_ERROR 1
#define TCL_GLOBAL_ONLY 1
#define TCL_LEAVE_ERR_MSG 0x200
#define TCL_EVAL_GLOBAL 0x20000
void Tcl_FindExecutable(const char *argv0);
Tcl_Interp *Tcl_CreateInterp(void);
int Tcl_Init(Tcl_Interp *interp);
Tcl_Obj *Tcl_NewStringObj(const char *bytes, int length);
void Tcl_IncrRefCount(Tcl_Obj*);
void Tcl_DecrRefCount(Tcl_Obj*);
int Tcl_EvalObjv(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[], int flags);
Tcl_Obj *Tcl_GetObjResult(Tcl_Interp *interp);
const char *Tcl_GetString(Tcl_Obj *objPtr);
void Tcl_SetObjResult(Tcl_Interp *interp, Tcl_Obj *resultObjPtr);
Tcl_Command Tcl_CreateObjCommand(Tcl_Interp *interp, const char *cmdName, Tcl_ObjCmdProc *proc, ClientData clientData, Tcl_CmdDeleteProc *deleteProc);
int Tcl_DeleteCommand(Tcl_Interp *interp, const char *cmdName);
const char *Tcl_GetVar2(Tcl_Interp *interp, const char *part1, const char *part2, int flags);
const char *Tcl_SetVar2(Tcl_Interp *interp, const char *part1, const char *part2, const char *newValue, int flags);
char *Tcl_Merge(int argc, const char *const *argv);
int Tcl_SplitList(Tcl_Interp *interp, const char *listStr, int *argcPtr, const char ***argvPtr);
void Tcl_Free(char *ptr);
#ifdef __cplusplus
}
#endif
#endif
