#pragma once

// player.h
//   audio generator for Song

#include "editor.h" // Song
#include "os.h" // sint16

namespace player
{

extern void setup(const Song* song);
extern void set_samplerate(unsigned int sr);

// to control playback
extern void silence(); // immediately stop all notes
extern void play_song(); // begin playback
extern void stop_song(); // stop playback, last played samples will continue to end
extern void apply_tempo(); // update tempo during playback
extern void set_beat(int sx); // set current playback beat

// for preview/editng
extern void play_note_immediate(unsigned char note, unsigned char inst);
extern void play_beat_immediate(int beat);

// for output audio
extern void render(sint16* buffer, int len);

// calculates samples per beat, call after play_song()
extern unsigned int get_beat_length();

}

// end of file
