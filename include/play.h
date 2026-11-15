// PlayState (Codename Engine semantics) shared with the song scripts
#pragma once
#include "engine.h"
#include "character.h"

// a note or one sustain piece (Codename splits sustains in step-long notes)
typedef struct {
	float t, len;           // strum time, piece length (sustain pieces)
	u8 dir;
	u8 sus;                 // 0 head, 1 sustain piece, 2 sustain tail (last piece)
	u8 state;               // 0 pending, 1 hit, 2 gone (missed / deleted)
	u8 pad;
	float endT;             // end of the whole sustain (for drawing)
} Note;

typedef struct {
	const StrumDef *def;
	Note *notes;
	int nNotes, first;
	Character *chars[1];
	float x[4], y;          // strum hitbox top-left (camHUD units)
	float scale, alpha[4];
	bool visible, cpu;
	int anim[4];            // 0 static, 1 pressed, 2 confirm
	float animTime[4], lastHit[4];
	char suffix[8];         // "-alt" when altAnim
	const NoteSkin *skin;
	float skinW;            // hitbox width of the current skin frame
} StrumLine;

typedef struct {
	float x, y, vx, vy, ay;
	float scale, scaleTo, scaleTime, scaleDur;
	float alpha, fadeDelay;
	int frame;
	bool used;
} Popup;

typedef struct {
	void (*create)(void);
	void (*postCreate)(void);
	void (*songStart)(void);
	void (*update)(float dt);
	void (*postUpdate)(float dt);
	void (*beat)(int beat);
	void (*step)(int step);
	void (*event)(const EventDef *e);
	void (*drawGameBack)(void);    // camGame, before characters
	void (*drawGameFront)(void);   // camGame, after characters
	void (*drawBorder)(void);      // camBorder
	void (*drawHud)(void);         // camHUD (after notes)
	void (*drawTop)(void);         // above everything (camAspect)
	void (*drawSub)(void);         // bottom screen (gameplay mode)
	bool (*songEnd)(void);         // true = handled
	void (*dadHit)(void);
	void (*leave)(void);
	void (*gameOver)(void);
} SongScript;

typedef struct {
	const SongDef *song;
	const SongScript *script;
	float songPos;
	bool songStarted, songEnded, paused, dead;
	int curStep, curBeat;
	float stepF, beatF, measureF;
	float scrollSpeed;
	int scrollTween;
	StrumLine sl[2];
	// camera
	Cam cam;                 // drawn camera (cam.zoom = effective zoom)
	float camX, camY;        // FlxCamera scroll center
	float followX, followY;  // camFollow
	float zoom, defaultCamZoom;
	float hudZoom, defaultHudZoom, hudZoomEff;
	bool camZooming, followEnabled;
	float camZoomingMult, camZoomingInterval, camZoomingStrength, camZoomingOffset;
	int camZoomingEvery;     // 0 beat, 1 step, 2 measure
	float camZoomingLast;
	int curCameraTarget;
	int camMoveTween, zoomTween, hudZoomTween;
	float camTweenX, camTweenY;
	float stageZoom;
	float camMoveAmount;     // camera.hx
	float camHudAlpha;
	bool camHudVisible;
	// score
	float health;
	int score, misses, combo;
	float accTotal;
	int accCount;
	bool canPause;
	// popups (camGame) and splashes (camHUD)
	Popup pops[20];
	struct { bool used; int lane, var; float t, x, y, scale; } splash[8];
	bool showCombo;
	Sheet shRatings, shSplash, shNotes;
	int deaths;
	int evIdx;
} PlayState;

extern PlayState P;
extern const SongScript olb_script, toyou_script;

// helpers for scripts
float play_step_crochet(void);
float play_crochet(void);
void play_set_noteskin(int sl, const NoteSkin *skin);
void play_layer(Sheet *sh, const LayerDef *ld, u16 color, int alpha);
void play_layer_frame(Sheet *sh, const LayerDef *ld, int frame, u16 color, int alpha);
void play_border_rect(float x0, float y0, float x1, float y1, u16 color, float alpha);
static inline float bx(float v) { return (v - 640.0f) * KDS + 128.0f; }
static inline float by(float v) { return (v - 360.0f) * KDS + 96.0f; }
static inline float hx(float v) { return (v - 640.0f) * P.hudZoomEff * KDS + 128.0f; }
static inline float hy(float v) { return (v - 360.0f) * P.hudZoomEff * KDS + 96.0f; }
void play_game_rect(float x0, float y0, float x1, float y1, float sfx, float sfy, u16 color, float alpha);
void play_change_character(int sl, Character *c);
void play_end_song(void);
void play_sub_dirty(void);
