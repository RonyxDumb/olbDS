// WWCD DS - small engine shared declarations
#pragma once
#include <nds.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gen_assets.h"

// ---------------------------------------------------------------------------
// coordinate helpers
// ---------------------------------------------------------------------------
#define VW 1280.0f
#define VH 720.0f
#define KDS (192.0f / 720.0f)       // virtual px -> DS px (the 16:9 view is cropped to 4:3)

static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline int A31(float a) { return (int)(clampf(a, 0, 1) * 31 + 0.5f); }
// flixel fps-independent lerp ratio (CoolUtil.getFPSRatio)
static inline float fps_ratio(float r, float dt) { return 1.0f - powf(1.0f - r, dt * 60.0f); }
static inline float frand(void) { return (rand() & 0xFFFF) / 65536.0f; }
static inline float frand_range(float a, float b) { return a + (b - a) * frand(); }
static inline int irand_range(int a, int b) { return a + rand() % (b - a + 1); }

// ---------------------------------------------------------------------------
// files
// ---------------------------------------------------------------------------
void *file_load(const char *path, u32 *size);   // malloc'd, NULL on failure

// ---------------------------------------------------------------------------
// 3D graphics (top screen)
// ---------------------------------------------------------------------------
#define RGB(r, g, b) RGB15((r) >> 3, (g) >> 3, (b) >> 3)
#define HEXC(c) RGB(((c) >> 16) & 255, ((c) >> 8) & 255, (c) & 255)
#define WHITE 0x7FFF

typedef struct {
	const SheetDef *def;
	u32 texOff[24];
	u32 texParam[24];
	u32 palParam;
	u32 palAddr;
	u16 *palOrig;
	bool loaded;
	bool shaded;
} Sheet;

typedef struct {
	const FontDef *def;
	u8 *alpha;              // A3I5 glyph atlas (RAM copy, for the bottom screen)
	u32 texOff, texParam, palParam;
	bool onTop;
} Font;

void gfx_init(void);
void gfx_reset_vram(void);
void gfx_frame_begin(void);
void gfx_frame_end(void);
void gfx_vblank(void);

u32 tex_alloc(u32 size);
void tex_free(u32 off, u32 size);
u32 pal_alloc(u32 bytes);
void tex_write(u32 off, const void *src, u32 size);
void pal_write(u32 addr, const u16 *src, u32 count);
u32 tex_free_bytes(void);

bool sheet_load(Sheet *s, const SheetDef *d, bool shaded);
void sheet_unload(Sheet *s);
void sheet_draw(Sheet *s, int frame, float x, float y, float scale, u16 color, int alpha);
void sheet_draw_ex(Sheet *s, int frame, float x, float y, float sx, float sy, u16 color, int alpha);
// flip: bit0 = mirror horizontally around x, bit1 = vertically around y
void sheet_draw_flip(Sheet *s, int frame, float x, float y, float sx, float sy, int flip, u16 color, int alpha);
// sub-rectangle of a single-part frame: u/v fractions in [0,1]
void sheet_draw_sub(Sheet *s, int frame, float x0, float y0, float x1, float y1, float fu0, float fv0, float fu1, float fv1, u16 color, int alpha);
void sheet_draw_rect(Sheet *s, int frame, float x0, float y0, float x1, float y1, float fv0, float fv1, u16 color, int alpha);
void gfx_rect(float x0, float y0, float x1, float y1, u16 color, int alpha);
void gfx_gradient(float x0, float y0, float x1, float y1, u16 top, u16 bottom, int alpha);
// menus: the whole 1280x720 view letterboxed in 256x144
#define MS 0.2f
static inline float mx(float v) { return (v - 640.0f) * MS + 128.0f; }
static inline float my(float v) { return (v - 360.0f) * MS + 96.0f; }
void gfx_quad_raw(int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1, u32 tex, u32 pal, u16 color, int alpha);
void gfx_quad_uv(int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1, u32 tex, u32 pal, u16 color, int alpha);

// palette "shaders": per-channel multiply (r,g,b) applied to registered palettes
void shader_tint(float r, float g, float b);
void shader_register(u16 *orig, u32 palAddr, int count);
void shader_unregister(u32 addr);
void shader_clear(void);

// character texture streaming
void gfx_queue_upload(u32 texOff, const u8 *src, int w4, int h, int stride);
u32 gfx_chr_slot(u32 size);
void gfx_chr_reset(void);

// fonts
void font_load(int id, bool top);
void font_unload_all(void);
Font *font_get(int id);
int font_width(int id, const char *s);
void font_draw(int id, float x, float y, const char *s, u16 color, int alpha);
void font_draw_scaled(int id, float x, float y, float sc, const char *s, u16 color, int alpha);
void font_draw_centered(int id, float cx, float y, const char *s, u16 color, int alpha);

// ---------------------------------------------------------------------------
// bottom screen (software rendered 16bpp bitmap)
// ---------------------------------------------------------------------------
typedef struct { u16 w, h; u16 *px; } Img16;
void sub_init(void);
void sub_present(void);
u16 *sub_fb(void);
void sub_clear(u16 color);
void sub_fill(int x, int y, int w, int h, u16 color);
void sub_fill_blend(int x, int y, int w, int h, u16 color, int a32);
void sub_rect(int x, int y, int w, int h, u16 color);
bool img_load(Img16 *img, const char *path);
void img_free(Img16 *img);
void sub_blit(const Img16 *img, int x, int y);
void sub_blit_alpha(const Img16 *img, int x, int y, int a32);
void sub_blit_flip(const Img16 *img, int x, int y);
void sub_blit_tint(const Img16 *img, int x, int y, int clipw, u16 tint);
void sub_text(int font, int x, int y, const char *s, u16 color);
void sub_text_centered(int font, int cx, int y, const char *s, u16 color);

// ---------------------------------------------------------------------------
// audio
// ---------------------------------------------------------------------------
enum { TR_MUSIC, TR_VOICES, TR_SFX1, TR_SFX2, TR_COUNT };
void audio_init(void);
void audio_update(void);
void audio_play_music(const char *path, bool loop, float volume);
void audio_play_song(const char *inst, const char *voices);
void audio_stop(void);
void audio_stop_music(void);
void audio_set_volume(float v);          // music / inst volume
float audio_get_volume(void);
void audio_set_voices(float v);
void audio_set_pitch(float p);           // music pitch
void audio_pause_song(bool paused);
bool audio_song_finished(void);
bool audio_music_playing(void);
float audio_song_time(void);             // ms
float audio_music_time(void);            // ms (menu music)
void audio_sfx(int id, float vol);
void audio_sfx_pitch(int id, float vol, float pitch);
void audio_stream_sfx(const char *path, float vol, float pitch);   // long one-shot sound
void audio_stream_stop(void);
bool audio_stream_playing(void);
u32 time_ticks(void);
void audio_fade_music(float to, float dur);

// ---------------------------------------------------------------------------
// video (main engine BG2 bitmap)
// ---------------------------------------------------------------------------
void video_init(void);
bool video_open(const char *path, bool loop, bool overlay);
void video_close(void);
bool video_active(void);
bool video_update(float ms);
void video_vblank(void);
void video_set_affine(float pa, float px, float py);
void video_set_blend(int eva);
void video_set_overlay_visible(bool on);
void video_set_window(int x0, int x1);
float video_duration(void);

// ---------------------------------------------------------------------------
// tweens
// ---------------------------------------------------------------------------
enum {
	EASE_LINEAR = 0,
	EASE_QUADIN = 3, EASE_QUADOUT, EASE_QUADINOUT,
	EASE_CUBEIN, EASE_CUBEOUT, EASE_CUBEINOUT,
	EASE_QUARTIN, EASE_QUARTOUT, EASE_QUARTINOUT,
	EASE_QUINTIN, EASE_QUINTOUT, EASE_QUINTINOUT,
	EASE_SINEIN, EASE_SINEOUT, EASE_SINEINOUT,
	EASE_CIRCIN, EASE_CIRCOUT, EASE_CIRCINOUT,
	EASE_EXPOIN, EASE_EXPOOUT, EASE_EXPOINOUT,
	EASE_BACKIN, EASE_BACKOUT, EASE_BACKINOUT,
	EASE_CLASSIC = 255
};
typedef void (*TweenCb)(void *ctx);
int tween_num(float *target, float from, float to, float dur, int ease, float delay, TweenCb cb, void *ctx);
int tween_to(float *target, float to, float dur, int ease, float delay, TweenCb cb, void *ctx);
void tween_cancel(int id);
bool tween_active(int id);
void tween_cancel_target(float *target);
void tween_update(float dt);
void tween_clear(void);
int timer_after(float delay, TweenCb cb, void *ctx);
float ease_apply(int ease, float t);

// ---------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------
extern u32 kDown, kHeld, kUp;
extern touchPosition touch;
extern bool touchDown;
#define KEY_ACCEPT (KEY_A | KEY_START)
#define KEY_BACK (KEY_B)

// ---------------------------------------------------------------------------
// save data (settings + mod progress)
// ---------------------------------------------------------------------------
typedef struct {
	bool downscroll, ghostTapping, botplay, lyricSubs;
	int noteOffset;
	bool olbGood, olbBad;              // songEndings
	int scoreOlb, scoreToYou;          // FunkinSave highscores
	int missesOlb, missesToYou;
	int soccerHigh, soccerWins, soccerSkin, soccerMode;
} Save;
extern Save save;
void save_load(void);
void save_write(void);
static inline bool any_ending(void) { return save.olbGood || save.olbBad; }

// ---------------------------------------------------------------------------
// states
// ---------------------------------------------------------------------------
enum { ST_INTRO, ST_MAINMENU, ST_STORY, ST_BONUS, ST_OPTIONS, ST_PLAY, ST_NEXTUP, ST_GAMEOVER,
	ST_SOCCERMENU, ST_SOCCERMODE, ST_SOCCER, ST_COUNT };
void state_switch(int st);
typedef struct { void (*enter)(void); void (*update)(float); void (*draw)(void); void (*leave)(void); } StateDef;
#define DECL_STATE(n) void n##_enter(void); void n##_update(float dt); void n##_draw(void); void n##_leave(void);
DECL_STATE(intro) DECL_STATE(mainmenu) DECL_STATE(story) DECL_STATE(bonus) DECL_STATE(options) DECL_STATE(play)
DECL_STATE(nextup) DECL_STATE(gameover) DECL_STATE(soccermenu) DECL_STATE(soccermode) DECL_STATE(soccer)

// song selection (PlayState.loadSong)
enum { SONG_OLB, SONG_TOYOU };
extern int g_song;
extern bool g_storyMode;
extern bool g_eyeIntroSeen;     // MainMenu.eyeIntroSeen
extern bool g_outroSeen;        // olbEvents outroSeen

// fades: 0 = visible, 1 = black
extern float g_fade;
extern float g_subFade;

// menu music (CoolUtil.playMenuSong)
void menu_music(bool fadeIn);
extern bool g_retroMusic;

void format_score(char *out, int score);

#include "soundbank.h"
