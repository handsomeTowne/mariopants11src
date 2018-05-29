#pragma once

// os.h
//   OS/SDL specific stuff
//   Existing implementations:
//     win32.cpp

#include "version.h"

// size specific types

#ifdef WIN32
	typedef unsigned __int16 uint16;
	typedef   signed __int16 sint16;
	typedef unsigned __int32 uint32;
	typedef   signed __int32 sint32;
#else
	typedef unsigned short uint16;
	typedef   signed short sint16;
	typedef unsigned long  uint32;
	typedef   signed long  sint32;
#endif

namespace os
{

// in platform implementation
extern void alert(const char* message);
extern bool yesno(const char* message);
extern const char* file_load(const char* default_name, int mask_count, const char** masks);
extern const char* file_save(const char* default_name, int mask_count, const char** masks);

// in main.cpp

extern void add_icon(int index, int w, int h, const void* data);
extern void draw_icon(int x, int y, int icon);

extern void add_font(int icon, int w, int h, int tx, int ty, int xs, int ys, int r, const char* map);
extern void dupe_font(char duplicate, char original);
extern void draw_font(int x, int y, const char* msg);

extern bool try_quit(bool confirm);
extern void set_caption(const char* caption);

extern void set_audio_callback(void (*callback)(sint16*,int));
extern void pause_audio(bool);
void lock_audio(bool lock); // mutex for audio thread

}

// end of file
