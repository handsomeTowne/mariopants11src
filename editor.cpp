// editor.cpp
//   the mariopants editor

#include <cstring> // memset, strlen
#include <cstdio> // sprintf
#include <cmath> // sin
#include "editor.h"
#include "files.h"
#include "data.h"
#include "os.h"
#include "gui.h"
#include "player.h"

namespace editor
{

// constants

const double LATENCY = 372.0; // ms in audio latency for scrolling playback
const unsigned int END_STOP = 2000; // ms to stop after end of song reached

const int MAX_TEMPO = 0x9F;

const int UNDO_SIZE = 2048;

const int INFO_TITLE_Y = 64;
const int INFO_AUTHOR_Y = INFO_TITLE_Y + 24;

const int inst_icon[17] = {
	ICON_INST0, ICON_INST1, ICON_INST2, ICON_INST3, ICON_INST4, ICON_INST5, ICON_INST6, ICON_INST7,
	ICON_INST8, ICON_INST9, ICON_INSTA, ICON_INSTB, ICON_INSTC, ICON_INSTD, ICON_INSTE, ICON_INSTF,
	ICON_INSTX };

const int num_icon[10] = {
	ICON_NUMBER0, ICON_NUMBER1, ICON_NUMBER2, ICON_NUMBER3, ICON_NUMBER4,
	ICON_NUMBER5, ICON_NUMBER6, ICON_NUMBER7, ICON_NUMBER8, ICON_NUMBER9 };

enum { // undo actions
	UNDO_COL, // column of notes
	UNDO_TEMPO,
	UNDO_METRE,
	UNDO_LENGTH,
	UNDO_LOOP,
	UNDO_TITLE,
	UNDO_AUTHOR,
	UNDO_DELETE, // delete a column on undo
	UNDO_INSERT, // insert a column on undo
	UNDO_MULTI, // causes previous UNDO actions to be undone as a group
	UNDO_CLEAR, // not undoable, this marks the beginning of the list
	UNDO_BLANK, // do nothing operation for initialization
};

enum { // editor sounds, all play on "instrument 15"
       // note: index starts at 1, because there is an internal -1 on note value
	SOUND_STARTUP = 1,
	SOUND_CLICK,
	SOUND_ERASE,
	SOUND_UNDO,
	SOUND_CLEAR_SONG,
	SOUND_LENGTH_SELECT,
	SOUND_LENGTH,
	SOUND_BOMB,
};

enum { // right click behaviour
	RCLICK_LISTEN = 0,
	RCLICK_ERASER,
	RCLICK_MODE_MAX
};

// editor state

static Song song;

static int instrument;
static bool play;
static int scroll;
static int scroll_fine;
static bool info;

static int info_focus; // selected infobox field
static int channel_select; // 0 = all, 1-3 = A,B,C
static bool mouse_listen;
static bool erase_override;
static int right_click_mode;
static bool show_channels; // shows channel numbers
static bool show_update; // debug shows ms in corner
static int update_time; // last update time (for show_update)
static bool redraw;
static unsigned int samplerate;

// scrolling playback
double playback_beat_len;
double playback_beat_pos;
int playback_beat;
unsigned int playback_end;
int playback_tempo;

typedef struct {
	unsigned char action;
	unsigned char param[8];
} UndoEntry;

UndoEntry undo_buffer[UNDO_SIZE];
int undo_index;
int undo_change;

unsigned char clipboard[576 * EXTRA_SIZE];
int clip_left;
int clip_right;
int clip_len;

static bool mouseb;
static int mousex, mousey;

char current_file[1024] = "";

// *--------------------------------------------------------------------------*
// editor state actions

// forward declarations
void push_undo(unsigned char action, int param);
void preview_note(int note, int inst);
void preview_column(int sx);
void refresh_limit();

// song modifiers

void clear_song(bool confirm)
{
	if (confirm)
	{
		if (!os::yesno("Are you sure you want to clear the song?\n"
		               "This action cannot be undone."))
			return;
	}

	for (int i=0; i < sizeof(song.notes); i+=2)
	{
		song.notes[i+0] = 0xFF;
		song.notes[i+1] = 0xDF;
	}
	song.tempo = 80;
	song.metre = 4;
	song.length = 96;
	song.limit = 96;
	song.loop = false;
	memset(song.title,0,sizeof(song.title));
	memset(song.author,0,sizeof(song.author));

	song.changed = false;
	push_undo(UNDO_CLEAR,0);
	clip_left = -10;
	clip_right = -11;
	clip_len = 0;

	scroll = 0;

	current_file[0] = 0;
	os::set_caption("mariopants");
}

void collapse_col(int sx)
{
	// move empty space to top of column

	if (sx < 0) return;
	if (sx > song.limit) return;

	int index = 6*sx;

	if (song.notes[index+0] != 0xFF && song.notes[index+2] == 0xFF)
	{
		song.notes[index+2] = song.notes[index+0];
		song.notes[index+3] = song.notes[index+1];
		song.notes[index+0] = 0xFF;
		song.notes[index+1] = 0xDF;
	}
	if (song.notes[index+2] != 0xFF && song.notes[index+4] == 0xFF)
	{
		song.notes[index+4] = song.notes[index+2];
		song.notes[index+5] = song.notes[index+3];
		song.notes[index+2] = 0xFF;
		song.notes[index+3] = 0xDF;
	}
}

void set_note(int sx, int note, int inst)
{
	if (sx < 0) return;

	if (mouse_listen)
	{
		preview_column(sx);
		return;
	}
	else if (inst == 15) // length tool
	{
		if (sx < 1) return;
		if (sx > song.limit) sx = song.limit;
		push_undo(UNDO_LENGTH,0);
		song.length = sx;
		song.changed = true;
		preview_note(SOUND_LENGTH,15);
		return;
	}
	else if (sx >= song.length) // only length tool can edit past end
	{
		return;
	}
	else if (inst == 16 || erase_override) // erase tool
	{
		bool erased = false;

		int note_to_erase = -1;
		for (int i=0; i<3; ++i)
		{
			int index = (6*sx)+(2*i);
			if(song.notes[index] == note)
			{
				if ( note_to_erase == -1 || // take first note found
				   (i == (3 - channel_select))) // or give this channel priority if selected
					note_to_erase = i;
			}
		}
		if (note_to_erase >= 0)
		{
			int index = (6*sx)+(2*note_to_erase);
			push_undo(UNDO_COL,sx);
			song.notes[index+0] = 0xFF;
			song.notes[index+1] = 0xDF;
			if(channel_select==0) collapse_col(sx);
			song.changed = true;
			preview_note(SOUND_ERASE,15);
		}
		return;
	}
	else // instrument
	{
		bool noted = false;
		preview_note(note,inst);

		if (channel_select > 0) // note on specific channel
		{
			int i = 3 - channel_select;
			int index = (6*sx)+(2*i);
			push_undo(UNDO_COL,sx);
			song.notes[index+0] = note;
			song.notes[index+1] = inst;
			song.changed = true;
			noted = true;
		}
		else // find first free channel
		{
			for (int i=2; i>=0; --i)
			{
				int index = (6*sx)+(2*i);
				if (song.notes[index] == 0xFF)
				{
					push_undo(UNDO_COL,sx);
					song.notes[index+0] = note;
					song.notes[index+1] = inst;
					song.changed = true;
					noted = true;
					break;
				}
			}
		}
		if (noted && (sx == scroll+5)) // note on last column auto-scrolls
		{
			scroll = scroll + 1;
			if (scroll >= song.limit) scroll = song.limit-1;
		}
		return;
	}
}

bool delete_column(int sx, bool undo)
{
	if (sx < 0 || sx >= song.length) return false;
	if (undo) push_undo(UNDO_INSERT,sx);
	for (int i=sx; (i+1) < (96*EXTRA_SIZE); ++i)
		memcpy(song.notes+(i*6),song.notes+((i+1)*6),6);
	--song.length;
	song.notes[(((96*EXTRA_SIZE)-1)*6)+0] = 0xFF;
	song.notes[(((96*EXTRA_SIZE)-1)*6)+1] = 0xDF;
	song.notes[(((96*EXTRA_SIZE)-1)*6)+2] = 0xFF;
	song.notes[(((96*EXTRA_SIZE)-1)*6)+3] = 0xDF;
	song.notes[(((96*EXTRA_SIZE)-1)*6)+4] = 0xFF;
	song.notes[(((96*EXTRA_SIZE)-1)*6)+5] = 0xDF;
	return true;
}

bool insert_column(int sx, const unsigned char* col, bool undo)
{
	if (sx < 0 || sx > song.length) return false;
	if (undo) push_undo(UNDO_DELETE,sx);
	for (int i=((96*EXTRA_SIZE)-1); i > sx; --i)
		memcpy(song.notes+(i*6),song.notes+((i-1)*6),6);
	memcpy(song.notes+(sx*6),col,6);
	if (song.length < song.limit) ++song.length;
	return true;
}

bool set_column(int sx, const unsigned char* col)
{
	if (sx < 0 || sx >= song.length) return false;
	push_undo(UNDO_COL,sx);
	memcpy(song.notes+(sx*6),col,6);
	return true;
}

void set_tempo(int tempo)
{
	push_undo(UNDO_TEMPO,song.tempo);
	song.tempo = tempo;
	if      (song.tempo < 0        ) song.tempo = 0;
	else if (song.tempo > MAX_TEMPO) song.tempo = MAX_TEMPO;
	song.changed = true;
}

void tempo_up()   { set_tempo(song.tempo+1); }
void tempo_down() { set_tempo(song.tempo-1); }

void set_metre(int metre)
{
	push_undo(UNDO_METRE,song.metre);
	song.metre = metre;
	song.changed = true;
	preview_note(SOUND_CLICK,15);
}

void set_34() { set_metre(3); }
void set_44() { set_metre(4); }
void toggle_metre() { set_metre((song.metre==4)?3:4); }

void set_loop(bool loop)
{
	push_undo(UNDO_LOOP,song.loop);
	song.loop = loop;
	song.changed = true;
	preview_note(SOUND_CLICK,15);
}

// editor exclusive

void set_instrument(int instrument_)
{
	instrument = instrument_;
	if      (instrument <  15) preview_note(5,instrument);
	else if (instrument == 15) preview_note(SOUND_LENGTH_SELECT,15);
	else if (instrument == 16) preview_note(SOUND_CLICK,15);
}

void set_eraser() { set_instrument(16); }
void set_lengthtool() { set_instrument(15); }

void set_scroll(int scroll_)
{
	scroll = scroll_;
	if (scroll < 0 ) scroll = 0;
	if (scroll > (song.limit-4)) scroll = (song.limit-4);
}

void scroll_up()   { set_scroll(scroll+1); }
void scroll_down() { set_scroll(scroll-1); }

void stop()
{
	player::stop_song();
	play = false;
	scroll_fine = 0;
}

void begin_play(bool from_start)
{
	os::pause_audio(true); // pause to make sure we can get the beat length before it starts

	player::play_song();
	info = false;
	play = true;


	playback_tempo = song.tempo;
	playback_beat_len = double(player::get_beat_length()) * 1000.0 / double(samplerate);
	playback_beat_pos = -LATENCY;
	playback_end = 0;
	scroll_fine = 0;

	if (from_start)
	{
		scroll = 0;
		playback_beat = 0;
	}
	else
	{
		if (scroll >= song.length) scroll = song.length-1;
		player::set_beat(scroll);
		playback_beat = scroll;
	}

	os::pause_audio(false);
}

void click_play()
{
	begin_play(true);
}

void preview_note(int note, int inst)
{
	player::play_note_immediate(note,inst);
}

void preview_column(int sx)
{
	player::play_beat_immediate(sx);
}

void toggle_loop()
{
	set_loop(!song.loop);
}

void cycle_channel()
{
	preview_note(SOUND_CLICK,15);
	channel_select = (channel_select + 1) % 4;
}

void save()
{
	stop();
	preview_note(SOUND_CLICK,15);
	os::pause_audio(true);

	const char* SAVE_MASKS[5] = { "*.sho", "*.zst", "*.000", "*.wav", "*.*" };
	const char* filename = os::file_save(current_file,5,SAVE_MASKS);
	if (filename)
	{
		if (!files::save_file(filename, &song))
		{
			os::alert(files::get_file_error());
		}
		else
		{
			strncpy(current_file,filename,sizeof(current_file));
			current_file[sizeof(current_file)-1] = 0;
			if (!files::match_extension(filename,".wav"))
			{
				song.changed = false;
				undo_change = undo_index;
				os::set_caption(current_file);
			}
		}
	}

	player::set_samplerate(samplerate); // .wav export changes samplerate
	os::pause_audio(false);
}

void quick_save()
{
	if (current_file[0] == 0)
	{
		save();
		return;
	}

	stop();
	preview_note(SOUND_CLICK,15);
	os::pause_audio(true);

	if (!files::save_file(current_file, &song))
		os::alert(files::get_file_error());
	else
	{
		if (!files::match_extension(current_file,".wav"))
		{
			song.changed = false;
			undo_change = undo_index;
		}
	}

	player::set_samplerate(samplerate); // .wav export changes samplerate
	os::pause_audio(false);
}

void multi_save()
{
	stop();
	preview_note(SOUND_CLICK,15);
	os::pause_audio(true);

	const char* SAVE_MASKS[2] = { "*.000", "*.zs0" };
	const char* filename = os::file_save(current_file,2,SAVE_MASKS);
	if (filename)
	{
		if (!files::save_multi_file(filename, &song))
		{
			os::alert(files::get_file_error());
		}
	}

	os::pause_audio(false);
}

void load()
{
	preview_note(SOUND_CLICK,15);

	if (song.changed)
	{
		bool yesno = os::yesno("You have unsaved changes.\nProceed?");
		if (!yesno) return;
	}

	const char* LOAD_MASKS[4] = { "*.sho", "*.zst", "*.000", "*.*" };
	const char* filename = os::file_load(current_file,4,LOAD_MASKS);
	if (filename)
	{
		if (!files::load_file(filename, &song))
		{
			os::alert(files::get_file_error());
		}
		else
		{
			strncpy(current_file,filename,sizeof(current_file));
			current_file[sizeof(current_file)-1] = 0;
			if (song.changed)
				os::alert("Errors were found in the file.\n"
				          "These have been automatically corrected.");
			push_undo(UNDO_CLEAR,0);
			scroll = 0;
			os::set_caption(current_file);
		}
	}
	refresh_limit();
}

void clear()
{
	preview_note(SOUND_CLEAR_SONG,15);
	clear_song(true);
}

void toggle_info()
{
	preview_note(SOUND_CLICK,15);
	info = !info;
}

void push_undo(unsigned char action, int param)
{
	// UNDO_CLEAR resets the undo entirely
	if (action == UNDO_CLEAR)
	{
		undo_buffer[0].action = UNDO_CLEAR;
		undo_index = 0;

		if (!song.changed)
			undo_change = 0;
		else // newly loaded song may have corrected data
			undo_change = -1;

		for (int i=1; i < UNDO_SIZE; ++i)
			undo_buffer[i].action = UNDO_BLANK;
		return;
	}

	// move to next space in undo buffer
	undo_index = (undo_index + 1) % UNDO_SIZE;

	// if we've hit the clear point, need to move it forward
	if (undo_buffer[undo_index].action == UNDO_CLEAR)
	{
		int next_index = (undo_index+1)%UNDO_SIZE;
		undo_buffer[next_index].action = UNDO_CLEAR;
		if (next_index == undo_change)
			undo_change = -1; // no longer possible to undo to unchanged state
	}
	if (undo_index == undo_change)
		undo_change = -1; // no longer possible to undo to unchanged state

	UndoEntry& u = undo_buffer[undo_index];
	u.action = action;
	switch(action)
	{
		case UNDO_COL:
			{
				int sx = param;
				u.param[0] = sx & 0xFF;
				u.param[1] = (sx >> 8) & 0xFF;
				for (int i=0; i < 6; ++i)
					u.param[i+2] = song.notes[(sx*6)+i];
			}
			break;
		case UNDO_TEMPO:  u.param[0] = song.tempo;  break;
		case UNDO_METRE:  u.param[0] = song.metre;  break;
		case UNDO_LENGTH:
			u.param[0] = song.length & 0xFF;
			u.param[1] = (song.length >> 8) & 0xFF;
			break;
		case UNDO_LOOP:   u.param[0] = song.loop;   break;
		// UNDO_TITLE / UNDO_AUTHOR save two characters at end of string,
		// restoring both of these will undo a character typed or deleted
		case UNDO_TITLE:
			u.param[0] = param;
			u.param[1] = song.title[ param+0];
			u.param[2] = song.title[ param+1];
			break;
		case UNDO_AUTHOR:
			u.param[0] = param;
			u.param[1] = song.author[param+0];
			u.param[2] = song.author[param+1];
			break;
		case UNDO_DELETE:
			{
				u.param[0] = param & 0xFF;
				u.param[1] = (param >> 8) & 0x7F;
				if (song.length == song.limit) // do not decrease length on undo
					u.param[1] |= 0x80;
				for (int i=0; i < 6; ++i)
					u.param[i+2] = song.notes[((song.limit-1)*6)+i];
			}
			break;
		case UNDO_INSERT:
			{
				int sx = param;
				u.param[0] = sx & 0xFF;
				u.param[1] = (sx >> 8) & 0xFF;
				for (int i=0; i < 6; ++i)
					u.param[i+2] = song.notes[(sx*6)+i];
			}
			break;
		case UNDO_MULTI:
			u.param[0] = param & 0xFF;
			u.param[1] = (param >> 8) & 0xFF;
			break;
		default:
			os::alert("Improper undo action!");
			break;
	}
}

void undo()
{
	int multi_count = 0;

	preview_note(SOUND_UNDO,15);

	UndoEntry& u = undo_buffer[undo_index];
	switch (u.action)
	{
		case UNDO_COL: // column of notes
			{
				int sx = u.param[0] + (u.param[1] << 8);
				for (int i=0; i < 6; ++i)
				{
					song.notes[(sx*6)+i] = u.param[i+2];
				}
			}
			break;
		case UNDO_TEMPO:  song.tempo = u.param[0];          break;
		case UNDO_METRE:  song.metre = u.param[0];          break;
		case UNDO_LENGTH:
			{
				song.length = u.param[0] + (u.param[1] << 8);
				if (song.length > 96)
				{
					song.limit = (96 * EXTRA_SIZE);
					refresh_limit();
				}
			}
			break;
		case UNDO_LOOP:   song.loop = (u.param[0] ? 1 : 0); break;
		case UNDO_TITLE:
			song.title[ u.param[0]+0] = u.param[1];
			song.title[ u.param[0]+1] = u.param[2];
			break;
		case UNDO_AUTHOR:
			song.author[u.param[0]+0] = u.param[1];
			song.author[u.param[0]+1] = u.param[2];
			break;
		case UNDO_DELETE:
			{
				int sx = u.param[0] + ((u.param[1] & 0x7F) << 8);
				delete_column(sx,false);
				memcpy(song.notes+(((96*EXTRA_SIZE)-1)*6),u.param+2,6);
				if ((u.param[1] & 0x80) != 0 && song.length < song.limit)
					++song.length; // undo length contraction
			}
			break;
		case UNDO_INSERT:
			{
				int sx = u.param[0] + (u.param[1] << 8);
				insert_column(sx,u.param+2,false);
			}
			break;
		case UNDO_MULTI:
			multi_count = u.param[0] + (u.param[1] << 8);
			break;
		case UNDO_CLEAR: // bottom of undo stack, do not erase
			return;
		case UNDO_BLANK: // shouldn't encounter these
		default:
			os::alert("Undo error! Blank undo action?");
			break;
	}
	// mark unused
	u.action = UNDO_BLANK;
	// return to previous undo point
	undo_index = (undo_index - 1);
	while (undo_index < 0) undo_index += UNDO_SIZE;

	if (undo_change == undo_index)
		song.changed = false; // have undone all changes
	else
		song.changed = true; // have undone producing a change

	while (multi_count > 0)
	{
		undo();
		--multi_count;
	}
}

void quit()
{
	preview_note(SOUND_BOMB,15);
	os::try_quit(song.changed);
}

void instrument_panel(int x, int y)
{
	int s = x / 14;
	int p = x % 14;
	if (p == 13) return; // in between instruments
	set_instrument(s);
}

void pixel_to_edit_coord(int x, int y, int* ox, int* oy)
{
	int sx = (x / 32) + scroll - 2;
	int sy = (143 - (y - 12)) / 8;
	if      (sy < 1 ) sy = 1;
	else if (sy > 13) sy = 13;
	if (ox) *ox = sx;
	if (oy) *oy = sy;
}

void edit_coord_to_pixel(int sx, int sy, int* ox, int* oy)
{
	int x = ((sx + 2 - scroll) * 32) + 8;
	int y = 143 - (8 * sy);
	if (ox) *ox = x;
	if (oy) *oy = y;
}

void pattern_panel(int x, int y)
{
	if (!info)
	{
		int sx,sy;
		pixel_to_edit_coord(mousex,mousey,&sx,&sy);
		if (sx <  0) return;
		set_note(sx,sy,instrument);
	}
	else
	{
		if      (y >= INFO_TITLE_Y && y < (INFO_TITLE_Y+18))
			info_focus = 1;
		else if (y >= INFO_AUTHOR_Y && y < (INFO_AUTHOR_Y+18))
			info_focus = 2;
		else
			info_focus = 0;
	}
}

void pattern_panel_rclick(int x, int y)
{
	bool mouse_listen_temp = mouse_listen;
	bool erase_override_temp = erase_override;
	
	switch (right_click_mode)
	{
		case RCLICK_LISTEN: mouse_listen   = true; break;
		case RCLICK_ERASER: erase_override = true; break;
		default: break;
	}

	pattern_panel(x,y);

	mouse_listen = mouse_listen_temp;
	erase_override = erase_override_temp;
}

void key_move(int dx, int dy)
{
	// the mouse is centred on the icon, hence the 8 pixel offset correction

	int sx,sy,mx,my;
	pixel_to_edit_coord(mousex,mousey,&sx,&sy);
	if      (sx <  0) sx = 0;
	else if (sx >  song.limit) sx = song.limit;
	edit_coord_to_pixel(sx,sy,&mx,&my);
	mx += 8;
	my += 8;

	// if not already snapped to grid, don't move, just snap
	if (mx != mousex || my != mousey)
	{
		mousex = mx;
		mousey = my;
		return;
	}

	// move edit position on grid
	sx += dx;
	if      (sx < 0) sx = 0;
	else if (sx > song.limit) sx = song.limit;
	sy += dy;
	if      (sy <  1) sy = 1;
	else if (sy > 13) sy = 13;

	// update scroll if we've gone offscreen
	if (sx < (scroll-2)) set_scroll(scroll-1);
	if (sx > (scroll+4)) set_scroll(scroll+1);

	// move the mouse to the new position
	edit_coord_to_pixel(sx,sy,&mx,&my);
	mousex = mx+8;
	mousey = my+8;
}

void select_left(int x, int y)
{
	int sx,sy;
	pixel_to_edit_coord(mousex,mousey,&sx,&sy);
	if (sx >= 0 && sx < song.limit)
	{
		clip_left = sx;
		if (clip_right < 0)
			clip_right = clip_left;
	}
}

void select_right(int x, int y)
{
	int sx,sy;
	pixel_to_edit_coord(mousex,mousey,&sx,&sy);
	if (sx >= 0 && sx < song.limit)
	{
		clip_right = sx;
		if (clip_left < 0)
			clip_left = clip_right;
	}
}

void select_clear()
{
	clip_left = -10;
	clip_right = -11;
}

void select_erase()
{
	if (clip_right < clip_left) return;
	int pastes = 0;
	for (int i=clip_left; i<=clip_right; ++i)
	{
		unsigned char blank[6] = { 0xFF, 0xDF, 0xFF, 0xDF, 0xFF, 0xDF };
		if (set_column(i,blank))
			++pastes;
	}
	if (pastes > 0)
		push_undo(UNDO_MULTI,pastes);
}

void select_cut() // remove selection pulling later entries back
{
	if (clip_right < clip_left) return;
	clip_len = (clip_right + 1) - clip_left;
	memcpy(clipboard,song.notes + (clip_left * 6), 6 * clip_len);
	int cuts = 0;
	for (int i=0; i < clip_len; ++i)
	{
		if (delete_column(clip_left,true))
			++cuts;
	}
	if (cuts > 0)
		push_undo(UNDO_MULTI,cuts);
}

void select_copy() // copy selection, no change
{
	if (clip_right < clip_left) return;
	clip_len = (clip_right + 1) - clip_left;
	memcpy(clipboard,song.notes + (clip_left * 6), 6 * clip_len);
}

void select_paste(int x, int y) // paste overwrite
{
	int sx,sy;
	pixel_to_edit_coord(mousex,mousey,&sx,&sy);
	if (sx < 0) return;
	if (sx >= song.limit) return;

	int pastes = 0;
	for (int i=0; i<clip_len; ++i)
	{
		if (set_column(sx+i,clipboard+(i*6)))
			++pastes;
	}
	if (pastes > 0)
		push_undo(UNDO_MULTI,pastes);
}

void select_insert(int x, int y) // paste insert
{
	int sx,sy;
	pixel_to_edit_coord(mousex,mousey,&sx,&sy);
	if (sx < 0) return;
	if (sx >= song.limit) return;

	int pastes = 0;
	for (int i=0; i<clip_len; ++i)
	{
		if (insert_column(sx+i,clipboard+(i*6),true))
			++pastes;
	}
	if (pastes > 0)
		push_undo(UNDO_MULTI,pastes);
}

// *--------------------------------------------------------------------------*
// gui Bob instances

// top row
gui::BobPoker  bob_ipanel(  24,  8,209, 14, &instrument_panel);
gui::BobPoke   bob_lengtht(235,  8, 13, 14, &set_lengthtool);

// pattern/info
gui::BobPorker bob_pattern(  8, 41,240,108, &pattern_panel   );

// middle panel
gui::BobPoke   bob_stop(    21,168, 14, 14, &stop          );
gui::BobPoke   bob_play(    55,168, 11, 13, &click_play    );
gui::BobPoke   bob_loop(    85,168, 14, 14, &toggle_loop   );
gui::BobSlider bob_tempo(  114,172, 44,  8, 0,MAX_TEMPO, 2,&song.tempo);
gui::BobRepeat bob_tempod( 104,172,  7,  7, &tempo_down,     650, 100);
gui::BobRepeat bob_tempou( 161,172,  7,  7, &tempo_up,       650, 100);
gui::BobSlider bob_scroll( 191,159, 50,  7, 0,96-4,      2,&scroll    );
gui::BobRepeat bob_scrolld(184,158,  7,  9, &scroll_down,    650, 100);
gui::BobRepeat bob_scrollu(241,158,  7,  9, &scroll_up,      650, 100);
gui::BobButton bob_clear(  201,177, 30, 12, ICON_CLEAR, -1, -1,&clear     );

// bottom row
gui::BobButton bob_quit(     8,201, 16, 20, ICON_BOMB,   0,  0,&quit      );
gui::BobPoke   bob_eraser(  40,202, 15, 17, &set_eraser    );
gui::BobPoke   bob_channel( 61,203, 14, 15, &cycle_channel );
gui::BobPoke   bob_34(      81,203, 14, 15, &set_34        );
gui::BobPoke   bob_44(      96,203, 14, 15, &set_44        );
gui::BobButton bob_qsave(  129,202, 15, 17, ICON_QSAVE,  0,  0,&quick_save);
gui::BobButton bob_save(   147,202, 15, 17, ICON_SAVE,   0,  0,&save      );
gui::BobButton bob_load(   165,202, 15, 17, ICON_LOAD,   0,  0,&load      );
gui::BobPoke   bob_info  ( 183,202, 15, 17, &toggle_info   );
gui::BobButton bob_undo(   216,203, 14, 15, ICON_UNDO,   0,  0,&undo      );

// make sure they all go in here
gui::Bob* bobs[] = {
	// top row
	&bob_ipanel,
	&bob_lengtht,
	// pattern/info
	&bob_pattern,
	// middle panel
	&bob_stop,
	&bob_play,
	&bob_loop,
	&bob_tempo,
	&bob_tempod,
	&bob_tempou,
	&bob_scroll,
	&bob_scrolld,
	&bob_scrollu,
	&bob_clear,
	// bottom row
	&bob_quit,
	&bob_eraser,
	&bob_channel,
	&bob_34,
	&bob_44,
	&bob_qsave,
	&bob_save,
	&bob_load,
	&bob_info,
	&bob_undo,
};
const int BOBS = sizeof(bobs) / sizeof(bobs[0]);

// *--------------------------------------------------------------------------*
// drawing sub-functions

void draw_pattern()
{
	using namespace os; // for draw_icon

	// draw background
	int px = 8 - scroll_fine;
	for (int i=0; i<9; ++i)
	{
		int sx = scroll + i - 2;
		bool bar = (0 == (sx % song.metre)) && (sx < song.length);

		// draw staff
		int staff;
		if      (sx == -2)          staff = ICON_PATTERN0; // clef
		else if (sx == -1)          staff = ICON_PATTERN1; // blank
		else if (sx == song.length) staff = ICON_PATTERN4; // end
		else if (bar)               staff = ICON_PATTERN2; // bar
		else                        staff = ICON_PATTERN3; // beat
		if (song.loop)
		{
			if      (staff == ICON_PATTERN1) staff = ICON_PATTERN6; // repeat start
			else if (staff == ICON_PATTERN4) staff = ICON_PATTERN5; // repeat end
		}
		draw_icon(px,41,staff);

		// draw copy selection
		if (sx == clip_left ) draw_icon(px,33,ICON_SEL1);
		if (sx == clip_right) draw_icon(px,33,ICON_SEL2);
		if (sx > clip_left && sx < clip_right) draw_icon(px,33,ICON_SEL0);

		// draw bar numbers
		if (bar)
		{
			int bi = sx / song.metre;
			if (bi < 10)
			{
				draw_icon(px+10, 33,num_icon[bi]);
			}
			else if (bi < 100)
			{
				draw_icon(px+10,33,num_icon[bi/10]);
				draw_icon(px+15,33,num_icon[bi%10]);
			}
			else if (bi < 1000)
			{
				draw_icon(px+10,33,num_icon[bi/100]);
				draw_icon(px+15,33,num_icon[(bi/10)%10]);
				draw_icon(px+20,33,num_icon[bi%10]);
			}
		}
		px += 32;
	}

	// get mouse position to see if it demands ledger lines
	int mx,my;
	pixel_to_edit_coord(mousex,mousey,&mx,&my);
	if (!bob_pattern.bound(mousex,mousey)) // off pattern grid
		my = 15;

	// draw ledger lines
	px = 0 - scroll_fine;
	for (int i=0; i<9; ++i)
	{
		int sx = scroll + i - 2;

		bool ledger = false;

		// draw notes
		const unsigned char* nd = song.notes + (sx * 6);
		if (sx >= 0 && sx < song.length)
		{
			if (mx == sx && my <= 2) // if mouse demands a line
				ledger = true;

			for (int n=2; n>=0; --n)
			{
				unsigned char note = nd[(n*2)+0];
				if (note >= 0x01 && note <= 0x02) // if note demands a line
					ledger = true;
			}
		}

		if (ledger)
			draw_icon(px,134,ICON_LEDGER);

		px += 32;
	}

	// draw notes
	px = 8 - scroll_fine;
	for (int i=0; i<9; ++i)
	{
		int sx = scroll + i - 2;
		int bottom = 143;

		if (play && sx == playback_beat && playback_beat_pos > 0.0) // bounce the notes
		{
			int bob = int(7.0 * sin((playback_beat_pos / playback_beat_len) * 3.14159));
			bottom += bob;
		}

		// draw notes
		const unsigned char* nd = song.notes + (sx * 6);
		if (sx >= 0 && sx < song.length)
		{
			for (int n=2; n>=0; --n)
			{
				unsigned char note = nd[(n*2)+0];
				unsigned char inst = nd[(n*2)+1];
				if (note >= 0x01 && note <= 0x0D && inst >= 0x00 && inst <= 0x0E)
				{
					int stack = 0; // notes in the same spot stack
					for (int m=n-1; m>=0; --m)
					{
						if (nd[(m*2)+0] == note)
							++stack;
					}

					int py = bottom-((8*note)+(stack*2));
					draw_icon((stack*4)+px,py,inst_icon[inst]);

					if (show_channels && stack == 0)
					{
						const char* CHANNEL_LABELS[3] = { "C", "B", "A" };
						draw_font(px-10,py+3,CHANNEL_LABELS[n]);
					}
				}
			}
		}

		px += 32;
	}

	// mask edges of pattern
	draw_icon(0,0,ICON_BG_MASK);
}

void draw_info()
{
	os::draw_icon(2,25,ICON_BG_INFO);

	os::draw_font(0,INFO_TITLE_Y,
		"             TITLE              \n"
		"                                ");
	os::draw_font(0,INFO_AUTHOR_Y,
		"             AUTHOR             \n"
		"                                ");
	os::draw_font(0,INFO_TITLE_Y +9,  song.title);
	os::draw_font(0,INFO_AUTHOR_Y+9, song.author);

	if      (info_focus == 1)
		os::draw_icon((strlen(song.title )*8)+2,INFO_TITLE_Y +12,ICON_DOT);
	else if (info_focus == 2)
		os::draw_icon((strlen(song.author)*8)+2,INFO_AUTHOR_Y+12,ICON_DOT);

	os::draw_font(0,136,"             mariopants " VERSION_STRING " ");
}

void refresh_limit()
{
	bob_scroll.set_range(0,song.limit-4,2);
}

// *--------------------------------------------------------------------------*
// public functions

void setup(unsigned int samplerate_, int argc, char** argv)
{
	clear_song(false);

	samplerate = samplerate_;
	player::setup(&song);
	player::set_samplerate(samplerate);
	os::set_audio_callback(&player::render);
	os::pause_audio(false);
	preview_note(SOUND_STARTUP,15);

	instrument = 0;
	play = false;
	scroll = 0;
	scroll_fine = 0;
	info = false;

	info_focus = 0;
	channel_select = 0;
	mouse_listen = false;
	erase_override = false;
	right_click_mode = RCLICK_ERASER;
	show_channels = false;
	show_update = false;
	update_time = 0;
	redraw = true;

	mouseb = false;
	mousex = -20;
	mousey = -20;

	push_undo(UNDO_CLEAR,0);

	gui::focus = NULL;
	for (int i=0; i < BOBS; ++i)
	{
		bobs[i]->down = false;
	}

	for (int i=0; i < ICON_COUNT; ++i)
	{
		const IconData* icon = icondata[i];
		os::add_icon(i, icon->w, icon->h, (const void*)icon->d);
	}

	os::add_font(ICON_FONT, 8, 9, 26, 11, 14, 15, 15,
		"ABCDEFGHIJKLMNO"
		"PQRSTUVWXYZ \':&"
		"abcdefghijklmno"
		"pqrstuvwxyz.,/ "
		"0123456789+-*\\="
		"_              "
		"             !?"
		"        % $()[]"
		"               "
		"         @   ^ "
		"               "
		"          \"#` |"
		"               "
		"               "
		"            \x81  "
		"              ~"
		"\x80");
	os::dupe_font('{','(');
	os::dupe_font('}',')');
	os::dupe_font('<','(');
	os::dupe_font('>',')');
	os::dupe_font(';',':');

	current_file[0] = 0;
	if (argc > 1)
	{
		const char* filename = argv[1];
		if (filename)
		{
			if (!files::load_file(filename, &song))
			{
				os::alert(files::get_file_error());
			}
			else
			{
				strncpy(current_file,filename,sizeof(current_file));
				current_file[sizeof(current_file)-1] = 0;
				if (song.changed)
					os::alert("Errors were found in the file.\n"
					          "These have been automatically corrected.");
				push_undo(UNDO_CLEAR,0);
				scroll = 0;
			}
		}
	}
}

int get_icon()
{
	return ICON_ICON;
}

bool get_changed()
{
	return song.changed;
}

void update(unsigned int ms)
{
	update_time = ms;
	if (gui::focus)
	{
		if (gui::focus->update(ms))
			redraw = true;
	}

	if (play) // scrolling playback
	{
		// tempo can change during playbaak
		if (song.tempo != playback_tempo)
		{
			playback_tempo = song.tempo;
			player::apply_tempo();

			double new_beat_len = double(player::get_beat_length()) * 1000.0 / double(samplerate);
			if (playback_beat_pos > 0.0) // don't adjust if in latency period
			{
				playback_beat_pos *= (new_beat_len / playback_beat_len);
			}
			playback_beat_len = new_beat_len;
		}

		playback_beat_pos += double(ms);
		while (playback_beat_pos >= playback_beat_len)
		{
			playback_beat_pos -= playback_beat_len;
			++playback_beat;
			if (playback_beat >= song.length)
			{
				if (song.loop) playback_beat = 0;
				else           playback_beat = song.length;
			}
		}

		// stop song when finished
		if (!song.loop && playback_beat >= song.length)
		{
			playback_end += ms;
			if (playback_end >= END_STOP) stop();
		}

		if (playback_beat >= (song.length-4))
		{
			set_scroll(song.length-4);
			scroll_fine = 0;
		}
		else
		{
			set_scroll(playback_beat);
			scroll_fine = int(32.0 * playback_beat_pos / playback_beat_len);
			if (playback_beat_pos < 0.0) scroll_fine = 0; // still in latency period
		}

		redraw = true;
	}
}

void draw()
{
	using namespace os; // for draw_icon

	redraw = false;

	// draw pattern
	draw_icon(0,0,ICON_BG);
	if (!info) draw_pattern();
	else draw_info();

	// indicate current instrument
	draw_icon(4,7,inst_icon[instrument]);

	// draw scroll bars
	int scroll_pos = (scroll * (236-191)) / (song.limit-4);
	draw_icon(191+scroll_pos,159,ICON_SCROLL);

	int tempo_pos = (song.tempo * (153-114)) / MAX_TEMPO;
	draw_icon(114+tempo_pos,172,ICON_TEMPO);

	// draw bobs
	for (int i=0; i < BOBS; ++i)
	{
		bobs[i]->draw();
	}
	
	// a few more buttons
	if              (!play) draw_icon( 21,167,ICON_STOP   );
	else                    draw_icon( 53,167,ICON_PLAY   );
	if          (song.loop) draw_icon( 85,167,ICON_LOOP   );
	if     (instrument==16) draw_icon( 40,202,ICON_ERASE  );
	if      (song.metre==3) draw_icon( 81,203,ICON_METRE3 );
	else if (song.metre==4) draw_icon( 96,203,ICON_METRE4 );
	if       (song.changed) draw_icon(129,202,ICON_CHANGED);
	if               (info) draw_icon(183,202,ICON_INFO   );
	if   (song.limit != 96) draw_icon(184,158,ICON_SCROLL_UNLIMITED);

	if (channel_select > 0)
	{
		const int CS_ICON[4] = { 0, ICON_CHANNELA, ICON_CHANNELB, ICON_CHANNELC };
		draw_icon(61,203,CS_ICON[channel_select]);
	}

	if (play) // bouncing mario to indicate position
	{
		bool jump = false;
		if ((playback_beat+1) < song.length)
		{
			int ni = (playback_beat+1)*6;
			jump =
				(song.notes[ni+0] != 0xFF) |
				(song.notes[ni+2] != 0xFF) |
				(song.notes[ni+4] != 0xFF) ;
		}

		int px = 73;
		if (playback_beat >= (song.length - 4))
		{
			px += 32 * (playback_beat - scroll);
			if (playback_beat < song.length && playback_beat_pos > 0.0)
				px += int(32.0 * playback_beat_pos / playback_beat_len);
		}

		px += int(32.0 * playback_end / playback_beat_len); // keep running past end

		int bob = 0;
		int icon = ICON_MARIO0;

		if (playback_beat_pos < 0.0) // fall to first note
		{
			bob = int(playback_beat_pos * (-128.0 / LATENCY));
			icon = (bob < 6) ? ICON_MARIO0 : ICON_MARIO1;
		}
		else if (jump) // jump to next note
		{
			bob = int(16.0 * sin((playback_beat_pos / playback_beat_len) * 3.14159));
			icon = (bob < 6) ? ICON_MARIO0 : ICON_MARIO1;
		}
		else // run to next note
		{
			int segment = int(4.0 * playback_beat_pos / playback_beat_len);
			icon = (segment & 1) ? ICON_MARIO2 : ICON_MARIO0;
		}

		draw_icon(px,17-bob,icon);
	}

	// debug ms update
	if (show_update)
	{
		char cms[8];
		sprintf(cms,"%03d",update_time);
		draw_font(0,0,cms);
		redraw = true; // force update every frame
	}

	// draw mouse
	if (!info && bob_pattern.bound(mousex,mousey))
	{
		if (mouse_listen || play)
			draw_icon(mousex-8,mousey-8,ICON_LISTEN);
		else if (erase_override || instrument == 16) // eraser is off centre
			draw_icon(mousex-3,mousey-2,inst_icon[16]);
		else
			draw_icon(mousex-8,mousey-8,inst_icon[instrument]);
	}
	else
	{
		draw_icon(mousex,mousey,ICON_MOUSE);
	}
}

bool do_redraw()
{
	return redraw;
}

void mouse_button(int x, int y, bool button)
{
	redraw = true;

	if (button && !mouseb)
	{
		if (play) // when playing any click should stop, unless adjusting tempo
		{
			if      (bob_tempo.bound( x,y)) bob_tempo.mouse_down( x,y);
			else if (bob_tempou.bound(x,y)) bob_tempou.mouse_down(x,y);
			else if (bob_tempod.bound(x,y)) bob_tempod.mouse_down(x,y);
			else
				stop();
		}
		else
		{
			for (int i=0; i < BOBS; ++i)
			{
				// all Bobs can get a mouse down
				if(bobs[i]->bound(x,y))
				{
					// because bob_tempo manipulates song.tempo directly,
					// manually pushing its undo state here
					if (bobs[i] == &bob_tempo)
					{
						push_undo(UNDO_TEMPO, song.tempo);
						song.changed = true;
					}

					// clicking any button will recover keyboard focus from info
					info_focus = 0;

					bobs[i]->mouse_down(x,y);
					break;
				}
			}
		}
	}
	else if (!button && mouseb)
	{
		if (gui::focus) // focused Bobs get a mouse up
			gui::focus->mouse_up(x,y);
	}

	mousex = x;
	mousey = y;
	mouseb = button;
}

void mouse_rbutton(int x, int y)
{
	if (play)
	{
		stop();
		redraw = true;
		return;
	}

	if (bob_pattern.bound(x,y))
	{
		pattern_panel_rclick(x,y);
		return;
	}
}


void mouse_move(int x, int y)
{
	redraw = true;

	if (gui::focus != NULL) // focused Bobs get a mouse move
		gui::focus->mouse_move(x,y);

	mousex = x;
	mousey = y;
}

void key(SDLKey key, char ascii)
{
	redraw = true;

	if (play) // all keys halt playback, except tempo
	{
		switch (key)
		{
			case SDLK_MINUS:  set_tempo(song.tempo-1); break;
			case SDLK_EQUALS: set_tempo(song.tempo+1); break;
			default:
				stop();
				break;
		}
		return;
	}
	else if (gui::focus) // currently clicking a button, don't allow keys
	{
		return;
	}

	// info_focus steals backspace and all valid ascii text characters
	if (info && info_focus != 0)
	{
		char* s = (info_focus == 1) ? song.title : song.author;
		int l = strlen(s);

		if (key == SDLK_BACKSPACE || key == SDLK_DELETE)
		{
			if (l > 0)
			{
				push_undo((info_focus==1)?UNDO_TITLE:UNDO_AUTHOR,l-1);
				s[l-1] = 0;
				song.changed = true;
			}
			return;
		}
		else if (ascii >= 32 && ascii < 127)
		{
			if (l < 31)
			{
				push_undo((info_focus==1)?UNDO_TITLE:UNDO_AUTHOR,l);
				s[l] = ascii;
				song.changed = true;
			}
			return;
		}
	}

	switch (key)
	{
		case SDLK_1: set_instrument(0 ); break;
		case SDLK_2: set_instrument(1 ); break;
		case SDLK_3: set_instrument(2 ); break;
		case SDLK_4: set_instrument(3 ); break;
		case SDLK_5: set_instrument(4 ); break;
		case SDLK_6: set_instrument(5 ); break;
		case SDLK_7: set_instrument(6 ); break;
		case SDLK_8: set_instrument(7 ); break;
		case SDLK_9: set_instrument(8 ); break;
		case SDLK_0: set_instrument(9 ); break;
		case SDLK_q: set_instrument(10); break;
		case SDLK_w: set_instrument(11); break;
		case SDLK_e: set_instrument(12); break;
		case SDLK_r: set_instrument(13); break;
		case SDLK_t: set_instrument(14); break;
		case SDLK_y: set_instrument(15); break;
		case SDLK_u: set_instrument(16); break;

		case SDLK_F5:
		case SDLK_p:
			if (!mouseb) begin_play(true);
			break;

		case SDLK_F6:
			if (!mouseb) begin_play(false);
			break;

		case SDLK_l: toggle_loop(); break;

		case SDLK_LEFT:  key_move(-1, 0); break;
		case SDLK_RIGHT: key_move( 1, 0); break;
		case SDLK_UP:    key_move( 0, 1); break;
		case SDLK_DOWN:  key_move( 0,-1); break;
		case SDLK_SPACE: pattern_panel(mousex,mousey); break;
		case SDLK_b:     pattern_panel_rclick(mousex,mousey); break;

		case SDLK_x:
		case SDLK_DELETE:
			{
				int temp = instrument;
				instrument = 16; // temporarily switch to erase tool
				pattern_panel(mousex,mousey);
				instrument = temp;
				break;
			}

		case SDLK_MINUS:  set_tempo(song.tempo-1); break;
		case SDLK_EQUALS: set_tempo(song.tempo+1); break;

		case SDLK_LEFTBRACKET:  set_scroll(scroll-1     ); break;
		case SDLK_RIGHTBRACKET: set_scroll(scroll+1     ); break;
		case SDLK_PAGEUP:       set_scroll(scroll-7     ); break;
		case SDLK_PAGEDOWN:     set_scroll(scroll+7     ); break;
		case SDLK_HOME:         set_scroll(0            ); break;
		case SDLK_END:          set_scroll(song.length-4); break;

		case SDLK_c: cycle_channel(); break;
		case SDLK_F1: channel_select = 1; break;
		case SDLK_F2: channel_select = 2; break;
		case SDLK_F3: channel_select = 3; break;
		case SDLK_F4: channel_select = 0; break;
		case SDLK_F7: show_channels = !show_channels; break;

		case SDLK_F8:
			right_click_mode = (right_click_mode + 1) % RCLICK_MODE_MAX;
			break;

		case SDLK_F9: // enable
			if (song.limit == 96)
			{
				song.limit = sizeof(song.notes) / 6;
			}
			else
			{
				song.limit = 96;
				if (song.length > 96)
				{
					push_undo(UNDO_LENGTH,0);
					song.length = 96;
				}
			}
			refresh_limit();
			break;
		case SDLK_F10: multi_save();  break;

		case SDLK_m: toggle_metre();  break;
		case SDLK_s: quick_save();    break;
		case SDLK_d: save();          break;
		case SDLK_f: load();          break;
		case SDLK_i: toggle_info();   break;

		case SDLK_TAB:
		case SDLK_RETURN:
			if (info)
				info_focus = (info_focus+1) % 3;
			break;

		case SDLK_BACKSPACE:
		case SDLK_z:
			undo();
			break;

		case SDLK_ESCAPE: os::try_quit(song.changed); break;
		case SDLK_BACKQUOTE: show_update = !show_update; break;

		case SDLK_COMMA:     select_left(mousex,mousey);   break;
		case SDLK_PERIOD:    select_right(mousex,mousey);  break;
		case SDLK_SLASH:     select_clear();               break;
		case SDLK_n:         select_erase();               break;
		case SDLK_g:         select_cut();                 break;
		case SDLK_h:         select_copy();                break;
		case SDLK_j:         select_paste(mousex,mousey);  break;
		case SDLK_k:         select_insert(mousex,mousey); break;

		// unused keys:
		//case SDLK_o:
		//case SDLK_a:
		//case SDLK_d:
		//case SDLK_v:

		default: break;
	}
}

void ctrl_held(bool held)
{
	mouse_listen = held;
	redraw = true;
}

void shift_held(bool held)
{
	erase_override = held;
	redraw = true;
}

} // namespace editor

// end of file
