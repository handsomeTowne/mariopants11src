// main.cpp
//   main entry point

#include "SDL.h"
#include "os.h"
#include "editor.h"

// global state of main

const unsigned int SAMPLERATE = 32000;
const unsigned int WIN_W = 256 * 2;
const unsigned int WIN_H = 224 * 2;

const int MAX_ICONS = 128;
static SDL_Surface* icon_bank[MAX_ICONS];
static SDL_Surface* screen;

static unsigned char ascii_map[256];
static SDL_Surface* ascii_tile[256];
static int ascii_w, ascii_h;

static bool do_quit = false;


// audio callback wrapper

static void (*audio_callback)(sint16* buffer, int len);

void sdl_audio_callback(void* userdata, Uint8* stream, int len)
{
	if (audio_callback)
		audio_callback((sint16*)stream,len/2);
	else
		memset(stream,0,len);
}

// entry point
int main(int argc, char** argv)
{
	if (0 != SDL_Init(
		SDL_INIT_TIMER |
		SDL_INIT_AUDIO |
		SDL_INIT_VIDEO ))
	{
		os::alert("Unable to initialize SDL!");
		return 1;
	}

	screen = SDL_SetVideoMode(WIN_W, WIN_H, 32, SDL_DOUBLEBUF);
	if (screen == NULL)
	{
		os::alert("Unable to create SDL video surface!");
		return 2;
	}

	audio_callback = NULL;

	SDL_AudioSpec audio_spec;
	memset(&audio_spec,0,sizeof(audio_spec));
	audio_spec.freq = SAMPLERATE;
	audio_spec.format = AUDIO_S16;
	audio_spec.channels = 1;
	audio_spec.silence = 0;
	audio_spec.samples = 8192;
	audio_spec.size = audio_spec.samples * 2;
	audio_spec.callback = sdl_audio_callback;
	audio_spec.userdata = NULL;

	if ( 0 != SDL_OpenAudio(&audio_spec,NULL))
	{
		os::alert("Unable to open SDL audio!");
		// not a  fatal error
	}

	for (int i=0; i < MAX_ICONS; ++i)
	{
		icon_bank[i] = NULL;
	}

	editor::setup(SAMPLERATE,argc,argv);

	SDL_WM_SetCaption("mariopants", "mariopants");
	int icon = editor::get_icon();
	if (icon >= 0)
	{
		SDL_WM_SetIcon(icon_bank[icon], NULL);
	}
	SDL_ShowCursor(0); // hide the system mouse cursor
	SDL_EnableKeyRepeat(650,100);
	SDL_EnableUNICODE(1);

	// draw the screen for the first time
	editor::draw();
	SDL_Flip(screen);

	Uint32 last_time = SDL_GetTicks();
	while(true)
	{
		// calculate time since last frame
		Uint32 time = SDL_GetTicks();
		Uint32 delta = time - last_time;
		last_time = time;

		SDL_Event event;
		while (SDL_PollEvent(&event) > 0)
		{
			switch (event.type)
			{
				case SDL_MOUSEBUTTONDOWN:
				case SDL_MOUSEBUTTONUP:
					{
						// right click or ctrl+left click are equivalent
						if (event.button.button == SDL_BUTTON_RIGHT)
						{
							if (event.button.state==SDL_PRESSED)
								editor::mouse_rbutton(event.button.x>>1, event.button.y>>1);
						}
						else if (event.button.button == SDL_BUTTON_LEFT)
						{
							editor::mouse_button(event.button.x>>1, event.button.y>>1, (event.button.state==SDL_PRESSED));
						}
						break;
					}
				case SDL_MOUSEMOTION:
					editor::mouse_move(event.button.x>>1, event.button.y>>1);
					break;
				case SDL_KEYDOWN:
					{
						if (     event.key.keysym.sym == SDLK_LCTRL ||
						         event.key.keysym.sym == SDLK_RCTRL)
							editor::ctrl_held(true);
						else if (event.key.keysym.sym == SDLK_LSHIFT ||
						         event.key.keysym.sym == SDLK_RSHIFT)
							editor::shift_held(true);

						char ascii = (event.key.keysym.unicode < 128) ? event.key.keysym.unicode : 0;
						editor::key(event.key.keysym.sym, ascii);
					}
					break;
				case SDL_KEYUP:
					if (     event.key.keysym.sym == SDLK_LCTRL ||
						        event.key.keysym.sym == SDLK_RCTRL)
						editor::ctrl_held(false);
					else if (event.key.keysym.sym == SDLK_LSHIFT ||
						        event.key.keysym.sym == SDLK_RSHIFT)
						editor::shift_held(false);
					break;
				case SDL_QUIT:
					os::try_quit(editor::get_changed());
					break;
				default:
					break;
			}
			if (do_quit) goto quit;
		}

		editor::update(delta);
		if (editor::do_redraw())
		{
			editor::draw();
			SDL_Flip(screen);
		}

		// figure out how much time this frame really used
		time = SDL_GetTicks();
		delta = time - last_time;

		// sleep to the end of a ~60hz frame
		const Uint32 MS_UPDATE = 14;
		Uint32 delay = (delta < MS_UPDATE) ? (MS_UPDATE - delta) : 1;
		SDL_Delay(delay);
	}

quit:
	SDL_Quit();
	return 0;
}

// abstraction layer to reduce visibility of SDL/OS stuff in code

namespace os
{

// convenient function for creating a new icon in icon_bank
void add_icon(int index, int w, int h, const void* data)
{
	if (index >= MAX_ICONS || icon_bank[index] != NULL)
	{
		os::alert("Invalid icon index!");
		exit(3);
	}

	// double size the image (enough to fit the largest icon I have)
	const unsigned char* source = (const unsigned char*)data;
	static unsigned char resized[256 * 288 * 4 * 4];
	for (int y=0; y<h; ++y)
	for (int x=0; x<w; ++x)
	{
		int spos = (((y*w)+x)*4);
		uint32 pixel;
		memcpy(&pixel,source+spos,4);

		for (int dy=0; dy<2; ++dy)
		for (int dx=0; dx<2; ++dx)
		{
			int ry = (y * 2) + dy;
			int rx = (x * 2) + dx;
			int rpos = (((ry*w*2)+rx)*4);
			memcpy(resized+rpos,&pixel,4);
		}
	}
	w <<= 1;
	h <<= 1;

	// NOTE the rgba masks presume little endian
	icon_bank[index] =
		SDL_CreateRGBSurfaceFrom(
			resized, w, h,
			32,  // bpp
			4*w, // pitch
			#if SDL_BYTEORDER == SDL_BIG_ENDIAN
				0xFF000000,
				0x00FF0000,
				0x0000FF00,
				0x000000FF
			#else
				0x000000FF,
				0x0000FF00,
				0x00FF0000,
				0xFF000000
			#endif
			);
	
	// optimize for this display
	if (icon_bank[index] != NULL)
	{
		bool has_alpha = false;
		for (int i=0; i < (w*h); ++i)
		{
			const unsigned char* p = (const unsigned char*)data;
			if (p[(i*4)+3] != 0xFF)
			{
				has_alpha = true;
				break;
			}
		}

		SDL_Surface* old = icon_bank[index];
		if (has_alpha)
			icon_bank[index] = SDL_DisplayFormatAlpha(old);
		else
			icon_bank[index] = SDL_DisplayFormat(old);
		SDL_FreeSurface(old);
	}

	if (icon_bank[index] == NULL)
	{
		os::alert("Unable to create SDL icon surface!");
		exit(5);
	}
}

// convenient function for drawing icons from icon_bank
void draw_icon(int x, int y, int icon)
{
	SDL_Surface* img = icon_bank[icon];
	SDL_Rect dst = { x<<1, y<<1, 0, 0 }; // note all pixels are scaled 2x2
	SDL_BlitSurface(img, NULL, screen, &dst);
}

// font state

// builds an ASCII font
// * icon - use add_icon to create font sheet first
// * w/h - size of font tile
// * tx/ty - top left of first tile
// * xs/ys - offset between tiles
// * r - number of tiles in row
// * map - string listing of characters in the tile,
//         space means unused
//         128 means use this for space
//         129 means use this for unused
//         130+
//         0 terminates the string (0 does not get a tile)
void add_font(int icon, int w, int h, int tx, int ty, int xs, int ys, int r, const char* map)
{
	// note all pixels are scaled 2x2
	w  <<= 1;
	h  <<= 1;
	tx <<= 1;
	ty <<= 1;
	xs <<= 1;
	ys <<= 1;

	SDL_Surface* font_sheet = icon_bank[icon];

	ascii_w = w;
	ascii_h = h;
	for (int i=0; i<256; ++i)
	{
		ascii_map[i] = 0;
		ascii_tile[i] = NULL;
	}

	unsigned char count = 1; // the blank tile will be placed at 0
	int rx = tx;
	int rc = 0;
	while(*map)
	{
		unsigned char c = *map;
		if (c !=' ') // if space, leave ascii_map at 0
		{
			if      (c == 128) c = ' '; // assign space
			else if (c == 129) c = 0;   // assign unused

			// create tile
			SDL_Surface* tile = SDL_CreateRGBSurface(
				font_sheet->flags,
				w, h,
				font_sheet->format->BitsPerPixel,
				font_sheet->format->Rmask,
				font_sheet->format->Gmask,
				font_sheet->format->Bmask,
				font_sheet->format->Amask);
			if (tile == NULL)
			{
				os::alert("Unable to create SDL tile surface!");
				exit(6);
			}

			// build tile from font sheet
			SDL_Rect rect = { rx, ty, w, h };
			if (0 != SDL_BlitSurface(font_sheet, &rect, tile, NULL))
			{
				os::alert("Unable to copy SDL font tiles!");
				exit(7);
			}

			// optimize
			SDL_Surface* old = tile;
			tile = SDL_DisplayFormat(old);
			SDL_FreeSurface(old);
			if (tile == NULL)
			{
				os::alert("Unable to rebuild SDL font tile!");
				exit(8);
			}

			// store tile
			if (c != 0)
			{
				ascii_tile[count] = tile;
				ascii_map[c] = count;
				++count;
			}
			else // "unused" default tile
				ascii_tile[0] = tile;
		}

		// advance to next tile
		++map;
		rx += xs;
		++rc;
		if (rc >= r) // next row
		{
			rx = tx;
			ty += ys;
			rc = 0;
		}
	}
}

// replaces "duplicate" character with "original"
// useful for when you want two characters to map
// to the same tile.
void dupe_font(char duplicate, char original)
{
	ascii_map[duplicate] = ascii_map[original];
}

void draw_font(int x, int y, const char* msg)
{
	// note all pixels are scaled 2x2
	x <<= 1;
	y <<= 1;

	int rx = x;
	int ry = y;
	while (*msg)
	{
		unsigned char c = *msg;

		if (c == '\n')
		{
			rx = x;
			ry += ascii_h;
		}
		else
		{
			SDL_Rect rect = { rx, ry, 0, 0 };
			SDL_BlitSurface(ascii_tile[ascii_map[c]],NULL,screen,&rect);
			rx += ascii_w;
		}

		++msg;
	}
}

// quit with confirm
bool try_quit(bool confirm)
{
	if (confirm)
	{
		// wait for escape to be released if it was pressed
		Uint8* keystate = SDL_GetKeyState(NULL);
		while (keystate[SDLK_ESCAPE])
		{
			SDL_Delay(15);
			SDL_Event event;
			while (SDL_PollEvent(&event) > 0) {} // discard any events while escape is held
			keystate = SDL_GetKeyState(NULL);
		}

		do_quit = os::yesno("You have unsaved changes.\n"
		                    "Are you sure you want to leave?");
	}
	else
	{
		do_quit = true;
	}

	return do_quit;
}

void set_caption(const char* caption)
{
	SDL_WM_SetCaption(caption,"mariopants");
}

// audio setup

void set_audio_callback(void (*callback)(sint16*,int))
{
	SDL_PauseAudio(1); // ensure pause
	audio_callback = callback;
	SDL_PauseAudio(0);
}

void pause_audio(bool pause)
{
	SDL_PauseAudio(pause ? 1 : 0);
}

void lock_audio(bool lock)
{
	if (lock) SDL_LockAudio();
	else      SDL_UnlockAudio();
}

} // namespace os

// end of file
