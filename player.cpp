// player.cpp
//   audio generator for Song

#include <cstdlib> // NULL
#include <cmath> // pow
#include "player.h"
#include "data.h"
#include "os.h"

static unsigned int samplerate;
static bool playing;
static int beat;      // current beat
static int next_beat; // samples to next beat
static int beat_length; // samples per beat
static const Song* song;

uint32 tuning; // tuning adjustment for samplerate

// mutex

struct AudioLock
{
	AudioLock() { os::lock_audio(true); }
	~AudioLock() { os::lock_audio(false); }
};

// mixing

struct Sampler
{
	static const unsigned int FADE_POWER = 10; // 1024 sample fade to reduce clicks
	static const unsigned int FADE_LEN = 1 << FADE_POWER;

	// The fade is implemented to hide a "click" when a playing note is interrupted.
	// On the SNES, samples were ADPCM, so an interrupted note would continue on
	// as deltas from the interrupted position, possibly causing distortion in the
	// attack, but obviating the click problem. This implementation is simply PCM
	// samples, so the short fade covers the click normally inherent in an abrupt
	// end of sample.

	uint32 pos0; // 16:16 fixed point, primary sound
	uint32 pos1; // 16:16 fixed point, fading sound
	const sint16* sample0;
	const sint16* sample1;
	uint32 sample_len0;
	uint32 sample_len1;
	uint32 fade;

	void play(unsigned char note, unsigned char inst)
	{
		// begin fade of previous sample
		fade = FADE_LEN;
		pos1 = pos0;
		sample1 = sample0;
		sample_len1 = sample_len0;

		int si = (inst * 13) + (note - 1);
		pos0 = 0;
		sample0 = sampledata[si].d;
		sample_len0 = sampledata[si].len;
	}

	void stop()
	{
		sample0 = NULL; // halt sample
		sample1 = NULL;
	}

	inline signed int render()
	{
		if (sample0 == NULL) return 0; // sampler disabled

		// calculate playback position
		uint32 high_pos = pos0 >> 16;
		if (high_pos >= sample_len0) // silence if sample is finished
		{
			sample0 = NULL;
			return 0;
		}

		signed int output = sample0[high_pos]; // gather output sample
		pos0 += tuning; // advance sample for next iteration

		if (sample1 != NULL) // fading old sample to remove pop
		{
			high_pos = pos1 >> 16;
			if (high_pos < sample_len1)
			{
				output += (sample1[high_pos] * fade) >> FADE_POWER;
				pos1 += tuning;
				--fade;
				if (fade < 1) sample1 = NULL;
			}
			else sample1 = NULL;
		}

		return output;
	}
};

const unsigned int CHANNEL_POWER = 2;
const unsigned int CHANNELS = 1 << CHANNEL_POWER;
Sampler sampler[CHANNELS];

void start_sample(unsigned int channel, unsigned char note, unsigned char inst)
{
	sampler[channel].play(note,inst);
}

inline signed int mix()
{
	signed int output = 0;
	for (int i=0; i < CHANNELS; ++i)
		output += sampler[i].render();
	return output >> CHANNEL_POWER;
}

// internal play functions

void play_beat()
{
	if (!playing) return;

	for (int i=0; i<3; ++i)
	{
		unsigned char note = song->notes[(beat*6)+(i*2)+0];
		unsigned char inst = song->notes[(beat*6)+(i*2)+1];

		if (note >=1 && note <= 0x0D && inst <= 0x0E)
			start_sample(i,note,inst);
	}

	++beat;
	if (beat >= song->length)
	{
		if (song->loop) beat = 0;
		else player::stop_song();
	}
}

void update_tempo()
{
	// samples per beat, based on lengths measured empirically,
	// and then presuming tempo in mario paint is
	// implemented as a 14 + the song->tempo added
	// to an accumulater each frame that triggers a beat on overflow
	double beat_samples = (690892.8 / double(14 + song->tempo));
	beat_length = int(double(samplerate) * beat_samples / 32000.0);
}

// public interface

namespace player
{

void setup(const Song* song_)
{
	set_samplerate(32000); // default samplerate
	playing = false;
	beat = 0;
	next_beat = 0;
	song = song_;
	silence();
}

void set_samplerate(unsigned int sr)
{
	AudioLock audio_lock;

	samplerate = sr;
	tuning = uint32(65536.0 * 32000.0 / double(sr)); // 16 bit fixed point adjustment
}

void silence()
{
	AudioLock audio_lock;

	for (int i=0; i < CHANNELS; ++i)
		sampler[i].stop();
}

void apply_tempo()
{
	AudioLock audio_lock;
	
	update_tempo();
}

void set_beat(int beat_)
{
	AudioLock audio_lock;

	if (beat_ <  0          ) beat_ = 0;
	if (beat_ >= song->length) beat_ = song->length;
	beat = beat_;
}

void play_song()
{
	AudioLock audio_lock;

	if (song == NULL) return;

	silence();
	update_tempo();
	playing = true;
	beat = 0;
	next_beat = 0;
}

void stop_song()
{
	AudioLock audio_lock;

	playing = false;
}

void play_note_immediate(unsigned char note, unsigned char inst)
{
	AudioLock audio_lock;

	start_sample(3,note,inst);
}

void play_beat_immediate(int b)
{
	AudioLock audio_lock;

	if (b < 0 || b >= song->length) return;
	for (int i=0; i<3; ++i)
	{
		unsigned char note = song->notes[(b*6)+(i*2)+0];
		unsigned char inst = song->notes[(b*6)+(i*2)+1];

		if (note >=1 && note <= 0x0D && inst <= 0x0E)
			start_sample(i,note,inst);
	}
}

void render(sint16* buffer, int len)
{
	// AudioLock audio_lock; render() is already called from a thread that has lock

	if (!playing)
	{
		while(len)
		{
			*buffer = mix();
			++buffer;
			--len;
		}
		return;
	}

	while(len)
	{
		if (next_beat < len) // advance to beat if it occurs during len
		{
			while (next_beat > 0)
			{
				*buffer = mix();
				++buffer;
				--len;
				--next_beat;
			}
		}
		else // otherwise finish render to end off len
		{
			next_beat -= len;
			while (len)
			{
				*buffer = mix();
				++buffer;
				--len;
			}
			return;
		}

		// time to play a beat
		while (next_beat <= 0)
		{
			play_beat();
			next_beat += beat_length;
		}
	}
}

unsigned int get_beat_length()
{
	AudioLock audio_lock;

	return beat_length;
}

} // namespace player

// end of file
