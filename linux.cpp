// linux.cpp
//   linux specific implementation of os.h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tcl.h"
#include "tk.h"
#include "os.h"

void tcl_do(Tcl_Interp *interp, const char* cmd) {
    if (Tcl_Eval(interp, cmd) == TCL_ERROR) {
        fprintf(stderr, "Tcl_Eval: %s: %s\n", cmd, interp->result);
        exit(1);
    }
}

namespace os
{

void alert(const char* message)
{
    Tcl_Interp *interp = Tcl_CreateInterp();
    Tcl_Init(interp);
    Tk_Init(interp);

    tcl_do(interp, "wm withdraw .");
    char cmd[1024];
    sprintf(cmd, "tk_messageBox -icon warning -message \"%s\" "
                 "-title \"Alert!\" -type ok", message);
    tcl_do(interp, cmd);

    Tcl_DeleteInterp(interp);
}

bool yesno(const char* message)
{
    Tcl_Interp *interp = Tcl_CreateInterp();
    Tcl_Init(interp);
    Tk_Init(interp);

    tcl_do(interp, "wm withdraw .");
    char cmd[1024];
    sprintf(cmd, "set answer [tk_messageBox -icon question -message \"%s\" "
                 "-title \"Question!\" -type okcancel]", message);
    tcl_do(interp, cmd);
    const char *answer = Tcl_GetVar(interp, "answer", 0);

    Tcl_DeleteInterp(interp);

    return strcmp(answer, "cancel");
}

const char* file_load(const char* default_name, int mask_count, const char** masks)
{
    Tcl_Interp *interp = Tcl_CreateInterp();
    Tcl_Init(interp);
    Tk_Init(interp);

    tcl_do(interp, "wm withdraw .");
    char cmd[1024];
    sprintf(cmd, "set filename [tk_getOpenFile -initialfile \"%s\" "
                 "-filetypes {"
                 "{{Shroom Player file} {.sho}} "
                 "{{ZSNES savestate} {.zst}} "
                 "{{SNES9x savestate} {.000}} "
                 "{{All files} *} "
                 "}]",
                 default_name);
    tcl_do(interp, cmd);
    const char *filename = Tcl_GetVar(interp, "filename", 0);

    Tcl_DeleteInterp(interp);

    if (!strlen(filename))
        return NULL;
    return filename;
}

const char* file_save(const char* default_name, int mask_count, const char** masks)
{
    Tcl_Interp *interp = Tcl_CreateInterp();
    Tcl_Init(interp);
    Tk_Init(interp);

    tcl_do(interp, "wm withdraw .");
    char cmd[1024];
    sprintf(cmd, "set filename [tk_getSaveFile -initialfile \"%s\" "
                 "-filetypes {"
                 "{{Shroom Player file} {.sho}} "
                 "{{ZSNES savestate} {.zst}} "
                 "{{SNES9x savestate} {.000}} "
                 "{{WAV render} {.wav}} "
                 "{{All files} *} "
                 "}]",
                 default_name);
    tcl_do(interp, cmd);
    const char *filename = Tcl_GetVar(interp, "filename", 0);

    Tcl_DeleteInterp(interp);

    if (!strlen(filename))
        return NULL;
    return filename;
}

} // namespace os

// end of file
