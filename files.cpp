// files.cpp
//   saving and loading

#include <cstring>
#include <cstdio>
#include "files.h"
#include "data.h"
#include "os.h"
#include "player.h"

#ifdef __unix__
	#include "zlib.h"
#else
	#include <zlib/zlib.h>
#endif

#ifdef __GNUC__
int stricmp(const char* s1, const char* s2)
{
	return strcasecmp(s1,s2);
}
#endif

// file helpers

// internal buffer for file loading, must be big enough for all expected files
const int FBUF_SIZE = 2 * 1024 * 1024;
static unsigned char fbuf[FBUF_SIZE];
static const char* fmsg = "No error.";

// reads file into fbuf, returns file size
unsigned int read_file(const char* filename)
{
	FILE* f = fopen(filename, "rb");
	if (f == NULL) return 0;
	unsigned int length = fread(fbuf,1,FBUF_SIZE,f);
	fclose(f);
	return length;
}

// little endian read/write

uint16 read_short(const void* buffer)
{
	const unsigned char* b = (const unsigned char*)buffer;
	return b[0] | (b[1] << 8);
}

uint32 read_long(const void* buffer)
{
	const unsigned char* b = (const unsigned char*)buffer;
	return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

void write_short(unsigned char* buffer, uint16 x)
{
	buffer[0] =  x       & 0xFF;
	buffer[1] = (x >> 8) & 0xFF;
}

void write_long(unsigned char* buffer, uint32 x)
{
	buffer[0] =  x        & 0xFF;
	buffer[1] = (x >> 8 ) & 0xFF;
	buffer[2] = (x >> 16) & 0xFF;
	buffer[3] = (x >> 24) & 0xFF;
}

// convert buffer of uint16 to little endian
void flip_short(uint16* buffer, unsigned int len)
{
	while(len)
	{
		unsigned char* ub = (unsigned char*)buffer;
		uint16 x = *buffer;
		ub[0] =  x       & 0xFF;
		ub[1] = (x >> 8) & 0xFF;
		++buffer;
		--len;
	}
}

// clean after load
void clean_song(Song* song)
{
	if (song->length < 1)
	{
		song->length = 1;
		song->changed = true;
	}
	if (song->length > 96)
	{
		song->limit = 96 * EXTRA_SIZE;
		if (song->length > song->limit)
		{
			song->length = song->limit;
			song->changed = true;
		}
	}

	if (song->tempo > 0x9F)
	{
		song->tempo = 0x9F;
		song->changed = true;
	}

	if (song->metre < 3 || song->metre > 4)
	{
		song->metre = 4;
		song->changed = true;
	}

	for (int i=0; i < (576 * EXTRA_SIZE); i+=2)
	{
		unsigned char note = song->notes[i+0];
		unsigned char inst = song->notes[i+1];

		if ((note < 1) ||
		    (note > 13 && note != 0xFF) ||
			(inst > 14 && inst != 0xDF))
		{
			song->notes[i+0] = 0xFF;
			song->notes[i+1] = 0xDF;
			song->changed = true;
		}
	}
}

// file helpers

bool load_sho(const char* filename, Song* song)
{
	unsigned int length = read_file(filename);
	if (length < 1) { fmsg = "Empty file."; return false; }
	if (length >= FBUF_SIZE) { fmsg = "File is unexpectedly large."; return false; }

	if (read_long( fbuf+0) != read_long("shro"))
	{
		fmsg = "Not a valid .sho file.";
		return false;
	}

	int version = read_short(fbuf+4);
	if (version != 2 && version != 3)
	{
		fmsg = "Unknown .sho version.";
		return false;
	}

	if (fbuf[6] != 0)
	{
		fmsg = "Unkonwn .sho compression type.";
		return false;
	}

	if (version == 2 && length < 680)
	{
		fmsg = "Not enough data in file.";
		return false;
	}

	memcpy(song->title,  fbuf+7   , 32);
	memcpy(song->author, fbuf+7+32, 32);
	song->title[ 31] = 0;
	song->author[31] = 0;
	// 32 byte .shi specification ignored

	const int NOTE_POS = 7+32+32+32;

	if (version == 2)
	{
		memcpy(song->notes, fbuf+NOTE_POS, 576);
		song->tempo = fbuf[NOTE_POS+576];

		song->limit = 96;
		if (length >= 683) // extended data
		{
			song->length = fbuf[680];
			song->loop =  (fbuf[681] != 0);
			song->metre = (fbuf[682] == 0) ? 3 : 4;
		}
		else // default if no extended data
		{
			song->length = 96;
			song->loop = 1;
			song->metre = 4;
		}
	}
	else if (version == 3)
	{
		song->length = read_short(fbuf+NOTE_POS);
		if (song->length > (96 * EXTRA_SIZE))
		{
			fmsg = "Song is too long.";
			return false;
		}

		song->loop = (fbuf[NOTE_POS+2] != 0);
		song->metre = (fbuf[NOTE_POS+3] == 0) ? 3 : 4;
		song->tempo = fbuf[NOTE_POS+4];
		memcpy(song->notes,fbuf+NOTE_POS+5,6*song->length);

		for (int i=(6*song->length); i < (576 * EXTRA_SIZE); i+=2)
		{
			song->notes[i+0] = 0xFF;
			song->notes[i+1] = 0xDF;
		}
	}
	else
	{
		fmsg = "Unknown version.";
		return false;
	}

	song->changed = false;
	clean_song(song);
	return true;
}

void read_ram(const unsigned char* ram, Song* song)
{
	// positions relative to original zst savestate at 0x15F7
	const int POS_NOTES  = 0x15F7 - 0x15F7;
	const int POS_LENGTH = 0x1837 - 0x15F7;
	const int POS_LOOP   = 0x1839 - 0x15F7;
	const int POS_TEMPO  = 0x183B - 0x15F7;
	const int POS_SPEED  = 0x183D - 0x15F7;
	const int POS_METRE  = 0x1845 - 0x15F7;

	// extract data
	memcpy(song->notes, ram+POS_NOTES, 576);
	song->length = (read_short(ram+POS_LENGTH) >> 3) - 2;
	song->tempo  =  ram[POS_TEMPO];
	song->loop   = (ram[POS_LOOP ] == 1) ? 1 : 0;
	song->metre  = (ram[POS_METRE] == 0) ? 3 : 4;
}

bool load_zst(const char* filename, Song* song)
{
	unsigned int length = read_file(filename);
	if (length < 1) { fmsg = "Empty file."; return false; }
	if (length < 0x1846) { fmsg = "File too small."; return false; }
	if (length >= FBUF_SIZE) { fmsg = "File is unexpectedly large."; return false; }

	strcpy(song->title,   "ZST import");
	strcpy(song->author, "Mario Paint");

	read_ram(fbuf+0x15F7, song);

	song->changed = false;
	clean_song(song);
	return true;
}

bool load_s9x(const char* filename, Song* song)
{
	const int POS_DATA = 0x115BF;

	// read existing data
	gzFile gf = gzopen(filename,"rb");
	if (gf == NULL) { fmsg = "Unable to open gz compressed file."; return false; }

	int length = gzread(gf,fbuf,FBUF_SIZE);
	gzclose(gf);
	if (length < POS_DATA + 1024) { fmsg = "File too small."; return false; }
	else if (length >= FBUF_SIZE) { fmsg = "File is unexpectedly large."; return false; }

	// read data
	read_ram(fbuf+POS_DATA, song);

	song->changed = false;
	clean_song(song);
	return true;
}

bool save_sho(const char* filename, const Song* song)
{
	uint16 version = (song->length <= 96) ? 2 : 3;

	memcpy(fbuf+0,"shro",4);
	write_short(fbuf+4,version);
	fbuf[6] = 0; // uncompressed

	const char shi[32] = "default.shi";
	memcpy(fbuf+7      ,song->title, 32);
	memcpy(fbuf+7+32   ,song->author,32);
	memcpy(fbuf+7+32+32,shi,         32);

	const int NOTE_POS = 7+32+32+32;
	int fsize = 0;

	if (version == 2)
	{
		memcpy(fbuf+NOTE_POS,song->notes,576);
		fbuf[NOTE_POS+576] = song->tempo;

		// extended data
		fbuf[680] = song->length;
		fbuf[681] = song->loop ? 1 : 0;
		fbuf[682] = (song->metre != 4) ? 0 : 1;
		fsize = 683;
	}
	else if (version == 3)
	{
		write_short(fbuf+NOTE_POS+0,song->length);
		fbuf[NOTE_POS+2] = song->loop ? 1 : 0;
		fbuf[NOTE_POS+3] = (song->metre != 4) ? 0 : 1;
		fbuf[NOTE_POS+4] = song->tempo;
		memcpy(fbuf+NOTE_POS+5,song->notes,6*song->length);
		fsize = NOTE_POS + 5 + (6 * song->length);
	}

	// save file
	FILE* f = fopen(filename, "wb");
	if (f == NULL) { fmsg = "Could not open file for write."; return false; }

	fwrite(fbuf,1,fsize,f);
	fclose(f);
	return true;
}

void save_ram(unsigned char* ram, const Song* song)
{
	// positions relative to original zst savestate at 0x15F7
	const int POS_NOTES  = 0x15F7 - 0x15F7;
	const int POS_LENGTH = 0x1837 - 0x15F7;
	const int POS_LOOP   = 0x1839 - 0x15F7;
	const int POS_TEMPO  = 0x183B - 0x15F7;
	const int POS_SPEED  = 0x183D - 0x15F7;
	const int POS_METRE  = 0x1845 - 0x15F7;

	// playback tempo is in a different spot than the editor's tempo
	// don't ask me why it's +14 and *3291161, this is just how it is
	uint32 play_tempo = (song->tempo + 14) * 3291161;

	// insert data
	memcpy(     ram+POS_NOTES,  song->notes, 576);
	write_short(ram+POS_LENGTH, (song->length + 2) << 3);
	write_long( ram+POS_SPEED,  play_tempo);
	ram[POS_LOOP ] = (song->loop) ? 1 : 0;
	ram[POS_TEMPO] = song->tempo;
	ram[POS_METRE] = (song->metre == 3) ? 0 : 1;
}

bool save_zst(const char* filename, const Song* song)
{
	if (song->length > 96)
	{
		fmsg = "Song too long for savestate. Use F10 for multi-export.";
		return false;
	}

	// reuse specified file if it exists
	unsigned int length = read_file(filename);
	if (length < 1)
	{
		// just build a new ZST if the file doesn't exist
		length = ZST_SIZE;
		memcpy(fbuf,zst_block,ZST_SIZE);
	}
	else if (length < 0x1846) { fmsg = "File too small."; return false; }
	else if (length >= FBUF_SIZE) { fmsg = "File is unexpectedly large."; return false; }

	save_ram(fbuf+0x15F7, song);

	// save file
	FILE* f = fopen(filename, "wb");
	if (f == NULL) { fmsg = "Could not open file for write."; return false; }

	fwrite(fbuf,1,length,f);
	fclose(f);
	return true;
}

bool save_s9x(const char* filename, const Song* song)
{
	if (song->length > 96)
	{
		fmsg = "Song too long for savestate. Use F10 for multi-export.";
		return false;
	}

	const int POS_DATA = 0x115BF;

	// if a ready savestate file does not exist, create it from the default file
	FILE* f = fopen(filename,"rb");
	if (f == NULL)
	{
		f = fopen(filename,"wb");
		if (f == NULL)
		{
			fmsg = "Unable to create default SNES9X savestate file.";
			return false;
		}
		fwrite(s9x_block,1,S9X_SIZE,f);
		fclose(f);
	}
	else // file already exists, will insert data into it
		fclose(f);

	// read existing data
	gzFile gf = gzopen(filename,"rb");
	if (gf == NULL) { fmsg = "Unable to open gz compressed file."; return false; }


	int length = gzread(gf,fbuf,FBUF_SIZE);
	gzclose(gf);
	if (length < POS_DATA + 1024) { fmsg = "File too small."; return false; }
	else if (length >= FBUF_SIZE) { fmsg = "File is unexpectedly large."; return false; }

	// insert data
	save_ram(fbuf+POS_DATA, song);

	// save data
	gf = gzopen(filename, "wb");
	if (gf == NULL) { fmsg = "Could not open file for write."; return false; }
	gzwrite(gf,fbuf,length);
	gzclose(gf);
	return true;
}

bool save_wav(const char* filename, const Song* song)
{
	const unsigned int SAMPLERATE = 32000;

	FILE* f = fopen(filename, "wb");
	if (f == NULL) { fmsg = "Could not open file for write."; return false; }

	const unsigned int LEADER = SAMPLERATE / 4; // silent leader
	const unsigned int TAIL   = SAMPLERATE * 3; // extra at end (if looped, fade it out)

	player::stop_song();
	player::silence();
	player::set_samplerate(SAMPLERATE);

	player::play_song();
	unsigned int beat_length = player::get_beat_length();
	unsigned int body_length = song->length * beat_length;

	unsigned int total_length =
		LEADER +
		body_length +
		(song->loop ? body_length : 0) + // loop plays body 2x
		TAIL;

	unsigned int total_size =
		36 + // WAV header size
		(total_length * 2) + // WAV data size
		(song->loop ? 68 : 0); // smpl chunk size

	// WAV header
	memcpy(     fbuf + 0x000, "RIFF", 4);
	write_long( fbuf + 0x004, total_size);
	memcpy(     fbuf + 0x008, "WAVE", 4);
	memcpy(     fbuf + 0x00C, "fmt ", 4);
	write_long( fbuf + 0x010, 16); // fmt chunk size
	write_short(fbuf + 0x014, 1); // uncompressed
	write_short(fbuf + 0x016, 1); // channels
	write_long( fbuf + 0x018, SAMPLERATE);
	write_long( fbuf + 0x01C, SAMPLERATE * 2);
	write_short(fbuf + 0x020, 2); // bytes per sample
	write_short(fbuf + 0x022, 16); // bits per sample
	memcpy(     fbuf + 0x024, "data", 4);
	write_long( fbuf + 0x028, (total_length * 2));
	fwrite(fbuf,1,0x2C,f);

	// silent leader
	memset(fbuf,0,LEADER*2);
	fwrite(fbuf,1,LEADER*2,f);

	sint16* wbuf = (sint16*)fbuf;

	for (int l=0; l<=(song->loop?1:0); ++l)
	{
		const unsigned int BLOCK_SIZE = 1024 * 8;
		unsigned int left = body_length;
		while (left)
		{
			unsigned int block = (left > BLOCK_SIZE) ? BLOCK_SIZE : left;
			player::render(wbuf,block);
			flip_short((uint16*)wbuf,block);
			fwrite(wbuf,1,block*2,f);
			left -= block;
		}
	}

	// render tail
	player::render(wbuf,TAIL);
	if (song->loop) // tail fades out if looped
	{
		for (int i=0; i<TAIL; ++i)
		{
			double fade = (double(TAIL-i) / double(TAIL));
			wbuf[i] = sint32(fade * double(wbuf[i]));
		}
	}
	flip_short((uint16*)wbuf, TAIL);
	fwrite(wbuf,1,TAIL*2,f);

	// if looped add sampler chunk
	if (song->loop)
	{
		memcpy(     fbuf + 0x000, "smpl", 4);
		write_long( fbuf + 0x004, 60); // fmt chunk size
		write_long( fbuf + 0x008, 0); // manufacturer
		write_long( fbuf + 0x00C, 0); // product
		write_long( fbuf + 0x010, int(1000000000.0 / double(SAMPLERATE))); // nanoseconds/sample
		write_long( fbuf + 0x014, 60); // midi note
		write_long( fbuf + 0x018, 0); // fine pitch
		write_long( fbuf + 0x01C, 0); // SMPTE format
		write_long( fbuf + 0x020, 0); // SMPTE offset
		write_long( fbuf + 0x024, 1); // loops
		write_long( fbuf + 0x028, 0); // extra data size
		write_long( fbuf + 0x02C, 0); // loop identifier
		write_long( fbuf + 0x030, 0); // loop direction
		write_long( fbuf + 0x034, (LEADER+body_length)); // loop start
		write_long( fbuf + 0x038, (LEADER+body_length+body_length-1)); // loop end
		write_long( fbuf + 0x03C, 0); // loop fractional tuning
		write_long( fbuf + 0x040, 0); // loop play count
		fwrite(fbuf,1,0x044,f);
	}

	// finish file
	fclose(f);

	player::stop_song();
	player::silence();
	return true;
}

// public interface

namespace files
{

bool load_file(const char* filename, Song* song)
{
	const char* ext = strrchr(filename, '.');
	if (ext == NULL) { fmsg = "Unknown extension."; return false; }
	if      (!stricmp(ext, ".sho")) return load_sho(filename,song);
	else if (!stricmp(ext, ".zst")) return load_zst(filename,song);
	else if (!stricmp(ext, ".zs0")) return load_zst(filename,song);
	else if (!stricmp(ext, ".zs1")) return load_zst(filename,song);
	else if (!stricmp(ext, ".zs2")) return load_zst(filename,song);
	else if (!stricmp(ext, ".zs3")) return load_zst(filename,song);
	else if (!stricmp(ext, ".zs4")) return load_zst(filename,song);
	else if (!stricmp(ext, ".zs5")) return load_zst(filename,song);
	else if (!stricmp(ext, ".zs6")) return load_zst(filename,song);
	else if (!stricmp(ext, ".zs7")) return load_zst(filename,song);
	else if (!stricmp(ext, ".zs8")) return load_zst(filename,song);
	else if (!stricmp(ext, ".zs9")) return load_zst(filename,song);
	else if (!stricmp(ext, ".000")) return load_s9x(filename,song);
	else if (!stricmp(ext, ".001")) return load_s9x(filename,song);
	else if (!stricmp(ext, ".002")) return load_s9x(filename,song);
	else if (!stricmp(ext, ".003")) return load_s9x(filename,song);
	else if (!stricmp(ext, ".004")) return load_s9x(filename,song);
	else if (!stricmp(ext, ".005")) return load_s9x(filename,song);
	else if (!stricmp(ext, ".006")) return load_s9x(filename,song);
	else if (!stricmp(ext, ".007")) return load_s9x(filename,song);
	else if (!stricmp(ext, ".008")) return load_s9x(filename,song);
	else if (strlen(ext) == 4 &&
		ext[0] == '.' &&
		(ext[1] == 'z' || ext[1] == 'Z') &&
		(ext[2] >= '0' && ext[2] <= '9') &&
		(ext[3] >= '0' && ext[3] <= '9'))
	{
		return load_zst(filename,song);
	}
	else if (strlen(ext) == 4 &&
		ext[0] == '.' &&
		(ext[1] == '0' && ext[1] <= '9') &&
		(ext[2] >= '0' && ext[2] <= '9') &&
		(ext[3] >= '0' && ext[3] <= '9'))
	{
		return load_s9x(filename,song);
	}

	fmsg = "Unknown extension.";
	return false;
}

bool save_file(const char* filename, const Song* song)
{
	const char* ext = strrchr(filename, '.');
	if (ext == NULL) { fmsg = "Unknown extension."; return false; }
	if      (!stricmp(ext, ".sho")) return save_sho(filename,song);
	else if (!stricmp(ext, ".zst")) return save_zst(filename,song);
	else if (!stricmp(ext, ".zs0")) return save_zst(filename,song);
	else if (!stricmp(ext, ".zs1")) return save_zst(filename,song);
	else if (!stricmp(ext, ".zs2")) return save_zst(filename,song);
	else if (!stricmp(ext, ".zs3")) return save_zst(filename,song);
	else if (!stricmp(ext, ".zs4")) return save_zst(filename,song);
	else if (!stricmp(ext, ".zs5")) return save_zst(filename,song);
	else if (!stricmp(ext, ".zs6")) return save_zst(filename,song);
	else if (!stricmp(ext, ".zs7")) return save_zst(filename,song);
	else if (!stricmp(ext, ".zs8")) return save_zst(filename,song);
	else if (!stricmp(ext, ".zs9")) return save_zst(filename,song);
	else if (!stricmp(ext, ".000")) return save_s9x(filename,song);
	else if (!stricmp(ext, ".001")) return save_s9x(filename,song);
	else if (!stricmp(ext, ".002")) return save_s9x(filename,song);
	else if (!stricmp(ext, ".003")) return save_s9x(filename,song);
	else if (!stricmp(ext, ".004")) return save_s9x(filename,song);
	else if (!stricmp(ext, ".005")) return save_s9x(filename,song);
	else if (!stricmp(ext, ".006")) return save_s9x(filename,song);
	else if (!stricmp(ext, ".007")) return save_s9x(filename,song);
	else if (!stricmp(ext, ".008")) return save_s9x(filename,song);
	else if (!stricmp(ext, ".wav")) return save_wav(filename,song);
	else if (strlen(ext) == 4 &&
		ext[0] == '.' &&
		(ext[1] == 'z' || ext[1] == 'Z') &&
		(ext[2] >= '0' && ext[2] <= '9') &&
		(ext[3] >= '0' && ext[3] <= '9'))
	{
		return save_zst(filename,song);
	}
	else if (strlen(ext) == 4 &&
		ext[0] == '.' &&
		(ext[1] >= '0' && ext[1] <= '9') &&
		(ext[2] >= '0' && ext[2] <= '9') &&
		(ext[3] >= '0' && ext[3] <= '9'))
	{
		return save_s9x(filename,song);
	}

	fmsg = "Unknown extension.";
	return false;
}

bool save_multi_file(const char* filename, const Song* song)
{
	static char file_temp[1024];
	if (strlen(filename) >= 1023)
	{
		fmsg = "Filename too long.";
		return false;
	}
	strcpy(file_temp,filename);

	static Song song_temp;
	memcpy(&song_temp,song,sizeof(Song));

	char* ext = strrchr(file_temp, '.');
	if (ext == NULL) { fmsg = "Unknown extension."; return false; }
	else if (!stricmp(ext, ".zs0") || !stricmp(ext, ".000"))
	{
		int count = 0;
		int slen = song_temp.length;
		while (slen > 0)
		{
			ext[3] = (count % 10) + '0';
			if (count >= 10)
				ext[2] = (count / 10) + '0';

			int rem = (slen > 96) ? 96 : slen;
			song_temp.length = rem;

			if (!save_file(file_temp, &song_temp))
				return false;

			for (int i=96; i<(song_temp.limit); ++i)
				memcpy(song_temp.notes+((i-96)*6),song_temp.notes+(i*6),6);

			slen -= rem;
			++count;
		}
	}
	else
	{
		fmsg = "Unknown extension.";
		return false;
	}
	return true;
}

const char* get_file_error()
{
	return fmsg;
}

bool match_extension(const char* filename, const char* extension)
{
	const char* ext = strrchr(filename, '.');
	if (ext == NULL) return false;
	return (0 == stricmp(ext,extension));
}

} // namespace files
