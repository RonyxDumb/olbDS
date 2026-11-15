// Codename Engine Character / FunkinSprite animation semantics
#include "character.h"

static const char *singAnims[4] = { "singLEFT", "singDOWN", "singUP", "singRIGHT" };

static ChrSlot slots[8];
static int nSlots;

void chr_slots_reset(void) {
	nSlots = 0;
	gfx_chr_reset();
}

ChrSlot *chr_slot_new(int wlog, int hlog) {
	if (nSlots >= 8) return NULL;
	ChrSlot *s = &slots[nSlots];
	u32 size = (8 << wlog) * (8 << hlog);
	u32 off = gfx_chr_slot(size);
	if (off == 0xFFFFFFFF) return NULL;
	s->off = off;
	s->size = size;
	s->wlog = wlog;
	s->hlog = hlog;
	s->owner = NULL;
	s->frame = -1;
	nSlots++;
	return s;
}

bool chr_load(Character *c, int id, ChrSlot *slot, bool isChar) {
	memset(c, 0, sizeof(*c));
	c->def = chr_defs[id];
	c->id = id;
	c->slot = slot;
	c->isChar = isChar;
	c->isPlayer = c->def->isPlayer;
	u32 size;
	c->data = file_load(c->def->file, &size);
	if (!c->data) return false;
	c->palAddr = pal_alloc(512);
	c->palParam = c->palAddr >> 4;
	pal_write(c->palAddr, (u16 *)c->data, 256);
	shader_register((u16 *)c->data, c->palAddr, 256);
	c->alpha = 1;
	c->sfx = c->sfy = 1;
	c->scaleSign = 1;
	c->visible = true;
	c->anim = -1;
	c->ctx = CTX_DANCE;
	c->lastHit = -1e9f;
	c->loaded = true;
	if (isChar) chr_dance(c);
	return true;
}

void chr_unload(Character *c) {
	if (c->loaded) shader_unregister(c->palAddr);
	if (c->data) free(c->data);
	c->data = NULL;
	c->loaded = false;
}

int chr_find_anim(Character *c, const char *name) {
	for (int i = 0; i < c->def->nAnims; i++)
		if (!strcmp(c->def->anims[i].name, name)) return i;
	return -1;
}

bool chr_has_anim(Character *c, const char *name) { return chr_find_anim(c, name) >= 0; }

const char *chr_anim_name(Character *c) {
	if (c->anim < 0) return "";
	return c->def->anims[c->anim].name;
}

// FunkinSprite.playAnim -> FlxAnimation.play semantics
void chr_play(Character *c, const char *name, bool force, int ctx, float songPos) {
	if (!c->loaded) return;
	int a = chr_find_anim(c, name);
	if (a < 0) return;
	if (!(a == c->anim && !force && !c->finished)) {
		c->anim = a;
		c->animTime = 0;
		c->frameIdx = 0;
		c->finished = false;
	}
	c->ctx = ctx;
	if (ctx == CTX_SING || ctx == CTX_MISS) c->lastHit = songPos;
}

void chr_dance(Character *c) {
	char n[24];
	snprintf(n, sizeof(n), "idle%s", c->idleSuffix);
	chr_play(c, n, false, CTX_DANCE, 0);
}

void chr_try_dance(Character *c, float songPos, float stepCrochet) {
	switch (c->ctx) {
	case CTX_SING:
	case CTX_MISS:
		if (c->lastHit + stepCrochet * c->def->holdTime < songPos) chr_dance(c);
		break;
	case CTX_DANCE:
		chr_dance(c);
		break;
	case CTX_LOCK:
		if (c->anim < 0) chr_dance(c);
		break;
	default:
		if (c->anim < 0 || c->finished) chr_dance(c);
		break;
	}
}

void chr_sing(Character *c, int dir, const char *suffix, int ctx, float songPos) {
	char n[32];
	snprintf(n, sizeof(n), "%s%s", singAnims[dir & 3], suffix);
	if (!chr_has_anim(c, n)) snprintf(n, sizeof(n), "%s", singAnims[dir & 3]);
	chr_play(c, n, true, ctx, songPos);
}

static void advance(Character *c, float dt) {
	if (c->anim < 0) return;
	const ChrAnim *a = &c->def->anims[c->anim];
	if (c->finished) return;
	c->animTime += dt;
	int n = a->nFrames;
	int f = (int)(c->animTime * a->fps);
	if (f >= n) {
		if (a->loop) f %= n;
		else {
			f = n - 1;
			c->finished = true;
		}
	}
	c->frameIdx = f;
}

void chr_update(Character *c, float dt, float songPos, float stepCrochet) {
	if (!c->loaded) return;
	advance(c, dt);
	if (!c->isChar) return;
	if (c->stunned) {
		c->stunTime += dt;
		if (c->stunTime > 5.0f / 60) c->stunned = false;
	}
	if (!c->lockAnim && c->ctx != CTX_DANCE) chr_try_dance(c, songPos, stepCrochet);
	c->lockAnim = false;
}

void chr_beat(Character *c, int beat, float songPos, float stepCrochet) {
	if (!c->loaded || !c->isChar) return;
	if (beat < 0) return;
	if (beat % 2 == 0 && !c->lockAnim) chr_try_dance(c, songPos, stepCrochet);
}

float chr_cam_x(Character *c) {
	return c->x + c->def->width / 2 + (c->isPlayer ? -100 : 150) + c->def->gx + c->def->camx;
}
float chr_cam_y(Character *c) {
	return c->y + c->def->height / 2 - 100 + c->def->gy + c->def->camy;
}

void chr_draw_alpha(Character *c, const Cam *cam, u16 color, float alpha) {
	if (!c->loaded || !c->visible || c->anim < 0 || alpha <= 0.01f || !c->slot) return;
	const ChrAnim *a = &c->def->anims[c->anim];
	int uf = a->frames[c->frameIdx];
	const ChrFrame *f = &c->def->frames[uf];
	ChrSlot *s = c->slot;
	if (s->owner != c->def || s->frame != uf) {
		gfx_queue_upload(s->off, c->data + f->off, f->w, f->h, 8 << s->wlog);
		s->owner = c->def;
		s->frame = uf;
	}
	float bx = c->x - c->offX + c->def->gx, by = c->y - c->offY + c->def->gy;
	float wx0 = bx + f->dx + a->offx;
	float wy = by + f->dy + a->offy;
	float wx1 = wx0 + f->ww;
	int u0 = 0, u1 = f->cw << 4;
	if (c->scaleSign < 0) {
		float o = bx + c->def->width / 2;
		float n0 = 2 * o - wx1, n1 = 2 * o - wx0;
		wx0 = n0; wx1 = n1;
		u0 = f->cw << 4; u1 = 0;
	}
	float x0 = cam_sx(cam, wx0, c->sfx), y0 = cam_sy(cam, wy, c->sfy);
	float x1 = cam_sx(cam, wx1, c->sfx), y1 = cam_sy(cam, wy + f->wh, c->sfy);
	u32 tp = (s->off >> 3) | (s->wlog << 20) | (s->hlog << 23) | (GL_RGB256 << 26) | GL_TEXTURE_COLOR0_TRANSPARENT;
	gfx_quad_raw((int)(x0 * 16), (int)(y0 * 16), (int)(x1 * 16), (int)(y1 * 16), u0, 0, u1, f->h << 4,
		tp, c->palParam, color, A31(alpha));
}

void chr_draw(Character *c, const Cam *cam, u16 color) { chr_draw_alpha(c, cam, color, c->alpha); }
