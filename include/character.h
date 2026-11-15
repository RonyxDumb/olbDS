#pragma once
#include "engine.h"

typedef struct {
	float camX, camY;      // camera center (world) = FlxCamera.scroll + half size
	float zoom;            // effective zoom (zoom * zoomMultiplier)
	float shakeX, shakeY;  // DS px
} Cam;

// world -> DS screen (flixel camera, zoom around the screen center)
static inline float cam_sx(const Cam *c, float wx, float sf) {
	return (wx - c->camX * sf + 640.0f * sf - 640.0f) * c->zoom * KDS + 128.0f + c->shakeX;
}
static inline float cam_sy(const Cam *c, float wy, float sf) {
	return (wy - c->camY * sf + 360.0f * sf - 360.0f) * c->zoom * KDS + 96.0f + c->shakeY;
}

enum { CTX_NONE, CTX_DANCE, CTX_SING, CTX_MISS, CTX_LOCK };

typedef struct {
	u32 off, size;
	u8 wlog, hlog;
	const void *owner;
	int frame;
} ChrSlot;

typedef struct {
	const ChrDef *def;
	int id;
	u8 *data;
	u32 palAddr, palParam;
	ChrSlot *slot;
	float x, y, alpha;
	float sfx, sfy;        // scroll factors
	float offX, offY;      // FlxSprite.offset for plain sprites
	float scaleSign;       // -1 = drawn mirrored around its origin
	bool visible;
	bool isChar;           // funkin Character (dance logic, globalOffset)
	bool isPlayer;
	int anim;
	float animTime;
	int frameIdx;
	bool finished;
	int ctx;
	float lastHit;         // song position of the last sing/miss
	char idleSuffix[8];
	bool stunned;
	float stunTime;
	bool lockAnim;
	bool loaded;
} Character;

ChrSlot *chr_slot_new(int wlog, int hlog);
void chr_slots_reset(void);
bool chr_load(Character *c, int id, ChrSlot *slot, bool isChar);
void chr_unload(Character *c);
int chr_find_anim(Character *c, const char *name);
bool chr_has_anim(Character *c, const char *name);
void chr_play(Character *c, const char *name, bool force, int ctx, float songPos);
void chr_dance(Character *c);
void chr_try_dance(Character *c, float songPos, float stepCrochet);
void chr_update(Character *c, float dt, float songPos, float stepCrochet);
void chr_beat(Character *c, int beat, float songPos, float stepCrochet);
void chr_sing(Character *c, int dir, const char *suffix, int ctx, float songPos);
void chr_draw(Character *c, const Cam *cam, u16 color);
void chr_draw_alpha(Character *c, const Cam *cam, u16 color, float alpha);
const char *chr_anim_name(Character *c);
float chr_cam_x(Character *c);
float chr_cam_y(Character *c);
