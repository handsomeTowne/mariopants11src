#pragma once

// editor.h
//   public interface to mariopants editor

#include "SDL_keysym.h" // for SDLKey

const int EXTRA_SIZE = 30;

typedef struct
{
	unsigned char notes[576*EXTRA_SIZE];
	int tempo;
	int metre;
	int length;
	int limit;
	bool loop;
	char title[32];
	char author[32];
	bool changed;
} Song;

namespace editor
{

extern void setup(unsigned int samplerate, int argc, char** argv);
extern int  get_icon(); // gets the index of a 32x32 program icon
extern bool get_changed();
extern void update(unsigned int ms);
extern void draw();
extern bool do_redraw(); // return true if redraw is needed
extern void mouse_button(int x, int y, bool button);
extern void mouse_rbutton(int x, int y);
extern void mouse_move(int x, int y);
extern void key(SDLKey key, char ascii);
extern void ctrl_held(bool held); // for visual indication of listen mode
extern void shift_held(bool held); // for eraser

}

// end of file
