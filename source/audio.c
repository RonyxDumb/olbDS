// Audio: one maxmod stream fed from NitroFS. Up to four streamed tracks
// (music/inst, voices, two one-shot sounds) are mixed in software, with
// pitch (resampling) support. Short sound effects come from a soundbank.
#include "engine.h"
#include <maxmod9.h>
#include "soundbank_bin.h"

#define RING 16384              // samples per track ring buffer
#define RING_MASK (RING - 1)
#define BUFLEN 1024             // maxmod stream buffer (samples)
#define TICK_HZ ((float)TICK_FREQ)

typedef struct {
	FILE *f;
	s16 *ring;
	volatile u32 w, r;          // ring positions (samples)
	volatile u32 frac;          // 16.16 sub-sample position
	volatile u32 step;          // 16.16 increment (pitch)
	volatile int vol;           // 0..256
	volatile bool active, paused;
	bool loop, eof, open;
	volatile u32 consumed;      // samples consumed since start (for timing)
} Track;

static Track tr[TR_COUNT];
static volatile bool songMode;
static volatile u32 cbTicks;
static float musicVol = 1, voicesVol = 1;
static float fadeFrom, fadeTo, fadeDur, fadeT;
static bool fading;

static inline u32 ticks_now(void) { return (u32)tickGetCount(); }
u32 time_ticks(void) { return ticks_now(); }

static mm_word stream_cb(mm_word length, mm_addr dest, mm_stream_formats format) {
	s16 *out = dest;
	static s32 mix[BUFLEN * 2];
	for (u32 i = 0; i < length; i++) mix[i] = 0;
	bool any = false;
	for (int k = 0; k < TR_COUNT; k++) {
		Track *t = &tr[k];
		if (!t->active || t->paused) continue;
		any = true;
		int v = t->vol;
		u32 step = t->step;
		if (step == 0x10000) {
			u32 avail = t->w - t->r;
			u32 n = length < avail ? length : avail;
			for (u32 i = 0; i < n; i++) mix[i] += (t->ring[(t->r + i) & RING_MASK] * v) >> 8;
			t->r += n;
			t->consumed += n;
		} else {
			u32 frac = t->frac;
			u32 r = t->r;
			u32 i;
			for (i = 0; i < length; i++) {
				if (t->w - r < 2) break;
				s32 a = t->ring[r & RING_MASK], b = t->ring[(r + 1) & RING_MASK];
				s32 s = a + (((b - a) * (s32)(frac >> 4)) >> 12);
				mix[i] += (s * v) >> 8;
				frac += step;
				r += frac >> 16;
				t->consumed += frac >> 16;
				frac &= 0xFFFF;
			}
			t->r = r;
			t->frac = frac;
		}
	}
	for (u32 i = 0; i < length; i++) {
		s32 s = mix[i];
		out[i] = s > 32767 ? 32767 : (s < -32768 ? -32768 : s);
	}
	(void)any;
	if (songMode) cbTicks = ticks_now();
	return length;
}

static void track_close(Track *t) {
	t->active = false;
	if (t->f) fclose(t->f);
	t->f = NULL;
	t->open = false;
	t->w = t->r = 0;
	t->frac = 0;
	t->eof = false;
	t->paused = false;
	t->consumed = 0;
}

static bool track_open(Track *t, const char *path, bool loop, float vol, float pitch) {
	track_close(t);
	t->f = fopen(path, "rb");
	if (!t->f) return false;
	setvbuf(t->f, NULL, _IONBF, 0);
	t->loop = loop;
	t->vol = (int)(clampf(vol, 0, 4) * 256);
	t->step = (u32)(pitch * 65536);
	t->open = true;
	return true;
}

static void track_fill(Track *t, int maxChunk) {
	if (!t->open || t->eof) return;
	u32 space = RING - (t->w - t->r) - 4;
	while (space >= 1024 && maxChunk > 0) {
		u32 pos = t->w & RING_MASK;
		u32 n = space;
		if (n > RING - pos) n = RING - pos;
		if (n > 4096) n = 4096;
		size_t got = fread(t->ring + pos, 2, n, t->f);
		if (got < n) {
			if (t->loop) fseek(t->f, 0, SEEK_SET);
			else t->eof = true;
		}
		t->w += got;
		space -= got;
		maxChunk--;
		if (t->eof) break;
	}
}

void audio_init(void) {
	mmInitDefaultMem((mm_addr)soundbank_bin);
	for (int i = 0; i < MSL_NSAMPS; i++) mmLoadEffect(i);
	for (int i = 0; i < TR_COUNT; i++) {
		tr[i].ring = malloc(RING * 2);
		tr[i].step = 0x10000;
	}
	static mm_stream st;
	st.sampling_rate = AUDIO_RATE;
	st.buffer_length = BUFLEN;
	st.callback = stream_cb;
	st.format = MM_STREAM_16BIT_MONO;
	st.timer = MM_TIMER0;
	st.manual = false;
	mmStreamOpen(&st);
}

static bool track_done(Track *t) { return t->open && t->eof && t->r + 1 >= t->w; }

void audio_update(void) {
	for (int i = 0; i < TR_COUNT; i++) {
		Track *t = &tr[i];
		if (!t->open) continue;
		track_fill(t, 2);
		if (i >= TR_SFX1 && track_done(t)) track_close(t);
	}
	if (fading) {
		static u32 last;
		u32 now = ticks_now();
		float dt = last ? (now - last) / TICK_HZ : 0;
		last = now;
		fadeT += dt;
		float k = fadeDur > 0 ? clampf(fadeT / fadeDur, 0, 1) : 1;
		audio_set_volume(lerpf(fadeFrom, fadeTo, k));
		if (k >= 1) { fading = false; last = 0; }
	}
}

void audio_set_volume(float v) {
	musicVol = clampf(v, 0, 2);
	tr[TR_MUSIC].vol = (int)(musicVol * 256);
	tr[TR_VOICES].vol = songMode ? (int)(musicVol * voicesVol * 256) : tr[TR_VOICES].vol;
}

float audio_get_volume(void) { return musicVol; }

void audio_fade_music(float to, float dur) {
	fadeFrom = musicVol;
	fadeTo = to;
	fadeDur = dur;
	fadeT = 0;
	fading = true;
}

void audio_set_voices(float v) {
	voicesVol = clampf(v, 0, 1);
	tr[TR_VOICES].vol = (int)(musicVol * voicesVol * 256);
}

void audio_set_pitch(float p) {
	tr[TR_MUSIC].step = (u32)(clampf(p, 0.1f, 4) * 65536);
}

void audio_stop_music(void) {
	fading = false;
	songMode = false;
	track_close(&tr[TR_MUSIC]);
	track_close(&tr[TR_VOICES]);
}

void audio_stop(void) {
	audio_stop_music();
	track_close(&tr[TR_SFX1]);
	track_close(&tr[TR_SFX2]);
}

void audio_play_music(const char *path, bool loop, float volume) {
	audio_stop_music();
	musicVol = volume;
	if (!track_open(&tr[TR_MUSIC], path, loop, volume, 1)) return;
	track_fill(&tr[TR_MUSIC], 8);
	tr[TR_MUSIC].active = true;
}

bool audio_music_playing(void) { return tr[TR_MUSIC].open && !track_done(&tr[TR_MUSIC]); }

void audio_play_song(const char *inst, const char *voices) {
	audio_stop_music();
	musicVol = 1;
	voicesVol = 1;
	if (!track_open(&tr[TR_MUSIC], inst, false, 1, 1)) return;
	track_fill(&tr[TR_MUSIC], 8);
	if (voices && track_open(&tr[TR_VOICES], voices, false, 1, 1)) track_fill(&tr[TR_VOICES], 8);
	cbTicks = ticks_now();
	songMode = true;
	// start both at the same sample
	int old = enterCriticalSection();
	tr[TR_MUSIC].active = true;
	if (tr[TR_VOICES].open) tr[TR_VOICES].active = true;
	leaveCriticalSection(old);
}

void audio_pause_song(bool paused) {
	int old = enterCriticalSection();
	tr[TR_MUSIC].paused = paused;
	tr[TR_VOICES].paused = paused;
	leaveCriticalSection(old);
	if (!paused) cbTicks = ticks_now();
}

bool audio_song_finished(void) { return track_done(&tr[TR_MUSIC]); }

float audio_song_time(void) {
	u32 s = tr[TR_MUSIC].consumed;
	float base = ((float)s - BUFLEN) * 1000.0f / AUDIO_RATE;
	if (!songMode || tr[TR_MUSIC].paused) return base;
	u32 dt = ticks_now() - cbTicks;
	float extra = dt * (1000.0f / TICK_HZ);
	float chunk = BUFLEN * 500.0f / AUDIO_RATE;   // callbacks happen every half buffer
	if (extra > chunk * 2) extra = chunk * 2;
	return base + extra;
}

float audio_music_time(void) {
	return (float)tr[TR_MUSIC].consumed * 1000.0f / AUDIO_RATE;
}

void audio_sfx_pitch(int id, float v, float pitch) {
	mm_sound_effect e;
	e.id = id;
	e.rate = (mm_hword)(clampf(pitch, 0.1f, 4) * 1024);
	e.handle = 0;
	e.volume = (mm_byte)(clampf(v, 0, 1) * 255);
	e.panning = 128;
	mmEffectEx(&e);
}

void audio_sfx(int id, float v) { audio_sfx_pitch(id, v, 1); }

void audio_stream_sfx(const char *path, float vol, float pitch) {
	// use a free one-shot track (the oldest one is replaced)
	Track *t = !tr[TR_SFX1].open ? &tr[TR_SFX1] : !tr[TR_SFX2].open ? &tr[TR_SFX2] :
		(tr[TR_SFX1].consumed > tr[TR_SFX2].consumed ? &tr[TR_SFX1] : &tr[TR_SFX2]);
	if (!track_open(t, path, false, vol, pitch)) return;
	track_fill(t, 4);
	t->active = true;
}

void audio_stream_stop(void) {
	track_close(&tr[TR_SFX1]);
	track_close(&tr[TR_SFX2]);
}

bool audio_stream_playing(void) { return tr[TR_SFX1].open || tr[TR_SFX2].open; }
