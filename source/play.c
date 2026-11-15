// PlayState: a port of Codename Engine's PlayState / StrumLine / Conductor
// behaviour. Song specific code lives in olb.c and toyou.c (the mod's .hx
// scripts), through the SongScript hooks.
#include "play.h"

PlayState P;

#define HIT_WINDOW 250.0f
static const u32 laneKeys[4] = { KEY_LEFT | KEY_Y, KEY_DOWN | KEY_B, KEY_UP | KEY_X, KEY_RIGHT | KEY_A };
static float bpmStep[16];
static bool subDirty;
static int subTimer;
static int pauseSel;
static float pauseBgX, pauseBgY;
static float pausePtrX, pausePtrY, pauseScale[4];
static Sheet shPause;
static Img16 imgIcon[4];
extern bool g_restartPending;

void play_sub_dirty(void) { subDirty = true; }

// ---------------------------------------------------------------------------
// conductor
// ---------------------------------------------------------------------------
static void conductor_setup(void) {
	const SongDef *s = P.song;
	bpmStep[0] = 0;
	for (int i = 1; i < s->nBpm; i++)
		bpmStep[i] = bpmStep[i - 1] + (s->bpm[i].time - s->bpm[i - 1].time) / (15000.0f / s->bpm[i - 1].bpm);
}

static int bpm_index(float pos) {
	const SongDef *s = P.song;
	int i = 0;
	while (i + 1 < s->nBpm && s->bpm[i + 1].time <= pos) i++;
	return i;
}

float play_step_crochet(void) { return 15000.0f / P.song->bpm[bpm_index(P.songPos)].bpm; }
float play_crochet(void) { return play_step_crochet() * P.song->stepsPerBeat; }

static void conductor_update(void) {
	const SongDef *s = P.song;
	int i = bpm_index(P.songPos);
	P.stepF = bpmStep[i] + (P.songPos - s->bpm[i].time) / (15000.0f / s->bpm[i].bpm);
	P.beatF = P.stepF / s->stepsPerBeat;
	P.measureF = P.beatF / s->beatsPerMeasure;
}

// ---------------------------------------------------------------------------
// strumlines + notes
// ---------------------------------------------------------------------------
static void strum_setup(StrumLine *s, const StrumDef *d, int idx) {
	memset(s, 0, sizeof(*s));
	s->def = d;
	s->scale = d->scale;
	s->visible = d->visible;
	s->cpu = idx == 0;
	float sw = 160 * 0.7f;
	float startX = 1280 * d->pos - (sw * d->scale * (4 / 2.0f - 0.5f) * d->spacing + sw * 0.5f * d->scale);
	for (int i = 0; i < 4; i++) {
		s->x[i] = startX + sw * d->scale * d->spacing * i;
		s->alpha[i] = 1;
		s->lastHit[i] = -5000;
	}
	s->y = d->y + sw * 0.5f - sw * d->scale * 0.5f;
	// notes, with sustains split into step-long pieces (StrumLine.generate)
	float stepC = 15000.0f / P.song->bpm[0].bpm;
	int n = 0;
	for (int i = 0; i < d->nNotes; i++) {
		n++;
		float len = d->notes[i].len;
		if (len > stepC * 0.75f) while (len > 10) { len -= len < stepC ? len : stepC; n++; }
	}
	s->notes = malloc(sizeof(Note) * n);
	int k = 0;
	for (int i = 0; i < d->nNotes; i++) {
		const NoteDef *nd = &d->notes[i];
		Note *h = &s->notes[k++];
		memset(h, 0, sizeof(*h));
		h->t = nd->time;
		h->dir = nd->dir & 3;
		h->endT = nd->time;
		if (nd->len > stepC * 0.75f) {
			h->endT = nd->time + nd->len;
			float len = nd->len;
			while (len > 10) {
				float cur = len < stepC ? len : stepC;
				Note *p = &s->notes[k++];
				memset(p, 0, sizeof(*p));
				p->t = nd->time + (nd->len - len);
				p->len = cur;
				p->dir = h->dir;
				p->sus = 1;
				p->endT = h->endT;
				len -= cur;
			}
			s->notes[k - 1].sus = 2;
		}
	}
	// stable sort by time (pieces stay after their head)
	for (int i = 1; i < k; i++) {
		Note t = s->notes[i];
		int j = i - 1;
		while (j >= 0 && s->notes[j].t > t.t) { s->notes[j + 1] = s->notes[j]; j--; }
		s->notes[j + 1] = t;
	}
	s->nNotes = k;
}

void play_set_noteskin(int idx, const NoteSkin *skin) {
	StrumLine *s = &P.sl[idx];
	s->skin = skin;
	// strum scale stays the one computed for the default skin (setGraphicSize)
	s->skinW = skin->w0 * 0.7f * s->scale;
}

// DS only: the 4:3 crop of the 16:9 HUD would cut the right lanes, so the
// player strumline is moved left just enough to stay on screen.
static float strum_shift(StrumLine *s) {
	if (s->cpu) return 0;
	float right = s->x[3] + s->skinW + 8;
	float maxv = 640 + 128 / KDS;
	return right > maxv ? maxv - right : 0;
}

static float strum_cx(StrumLine *s, int i) { return s->x[i] + s->skinW / 2 + strum_shift(s); }
static float strum_cy(StrumLine *s) { return s->y + (s->skin ? s->skin->h0 : 157) * 0.7f * s->scale / 2; }

static void vocals_set(bool on) { audio_set_voices(on ? 1 : 0); }

static void popup_rating(int r) {
	static const int fr[4] = { RATINGS_SICK, RATINGS_GOOD, RATINGS_BAD, RATINGS_SHIT };
	static const int rw[4] = { RATING_SICK_W, RATING_GOOD_W, RATING_BAD_W, RATING_SHIT_W };
	static const int rh[4] = { RATING_SICK_H, RATING_GOOD_H, RATING_BAD_H, RATING_SHIT_H };
	static const int nw[10] = { RATING_NUM0_W, RATING_NUM1_W, RATING_NUM2_W, RATING_NUM3_W, RATING_NUM4_W, RATING_NUM5_W, RATING_NUM6_W, RATING_NUM7_W, RATING_NUM8_W, RATING_NUM9_W };
	static const int nh[10] = { RATING_NUM0_H, RATING_NUM1_H, RATING_NUM2_H, RATING_NUM3_H, RATING_NUM4_H, RATING_NUM5_H, RATING_NUM6_H, RATING_NUM7_H, RATING_NUM8_H, RATING_NUM9_H };
	if (!P.showCombo) return;
	float cx = 1280 * 0.55f, cy = 720 * 0.5f - 60;
	float crochet = play_crochet();
	// rating (vslice ratings.hx)
	Popup *p = NULL;
	static int rr;
	p = &P.pops[rr++ % 20];
	memset(p, 0, sizeof(*p));
	p->used = true;
	p->frame = fr[r];
	p->x = cx + 190;
	p->y = cy + 25;
	(void)rw; (void)rh;
	p->ay = 550;
	p->vy = -(float)irand_range(140, 175);
	p->vx = -(float)irand_range(0, 10);
	p->scale = 0.7f * 1.15f;
	p->scaleTo = 0.7f * 0.95f;
	p->scaleDur = crochet / 1000;
	p->alpha = 1;
	p->fadeDelay = crochet * 0.0004f;
	// combo digits
	char b[12];
	int c = P.combo;
	snprintf(b, sizeof(b), "%03d", c);
	int len = strlen(b);
	for (int i = 0; i < len; i++) {
		int dgt = b[len - 1 - i] - '0';
		p = &P.pops[rr++ % 20];
		memset(p, 0, sizeof(*p));
		p->used = true;
		p->frame = RATINGS_NUM0 + dgt;
		float sc = 0.5f * 1.25f;
		p->x = cx - 43 * i + 190 + nw[dgt] * sc / 2;
		p->y = cy + 80 + nh[dgt] * sc / 2;
		p->ay = irand_range(200, 300);
		p->vy = -(float)irand_range(140, 160);
		p->vx = frand_range(-5, 5);
		p->scale = sc;
		p->scaleTo = 0.5f * 0.95f;
		p->scaleDur = crochet / 1000;
		p->alpha = 1;
		p->fadeDelay = crochet * 0.0009f;
	}
}

static void splash_show(StrumLine *s, int lane) {
	for (int i = 0; i < 8; i++) {
		if (!P.splash[i].used) {
			P.splash[i].used = true;
			P.splash[i].lane = lane;
			P.splash[i].var = rand() & 1;
			P.splash[i].t = 0;
			P.splash[i].x = strum_cx(s, lane);
			P.splash[i].y = strum_cy(s);
			P.splash[i].scale = s->scale;
			return;
		}
	}
}

static void good_note_hit(int idx, Note *n) {
	StrumLine *s = &P.sl[idx];
	if (n->state != 0) return;
	n->state = 1;
	bool player = !s->cpu;
	if (player) {
		float diff = fabsf(P.songPos - n->t);
		int r = 0;
		int sc = 300;
		float acc = 1;
		if (diff > HIT_WINDOW * 0.9f) { r = 3; sc = 50; acc = 0.25f; }
		else if (diff > HIT_WINDOW * 0.75f) { r = 2; sc = 100; acc = 0.45f; }
		else if (diff > HIT_WINDOW * 0.2f) { r = 1; sc = 200; acc = 0.75f; }
		if (!n->sus) {
			popup_rating(r);    // vslice ratings.hx (onPlayerHit, before the combo increment)
			P.score += sc;
			P.accTotal += acc;
			P.accCount++;
			P.combo++;
			if (r == 0) splash_show(s, n->dir);
		}
		P.health += 0.023f;
		if (P.health > 2) P.health = 2;
	} else {
		if (P.script->dadHit) P.script->dadHit();
	}
	Character *c = s->chars[0];
	if (c) chr_sing(c, n->dir, s->suffix, CTX_SING, P.songPos);
	s->anim[n->dir] = 2;
	s->animTime[n->dir] = 0;
	s->lastHit[n->dir] = n->t;
	vocals_set(true);
	P.camZooming = true;
	if (!P.camZooming) {}
	if (!n->sus) n->state = 2;
	subDirty = true;
}

static void note_miss(int idx, Note *n, int dir) {
	StrumLine *s = &P.sl[idx];
	P.health -= n ? 0.0475f : 0.04f;
	P.combo = 0;
	P.score -= 10;
	P.misses++;
	audio_sfx(SFX_MISSNOTE1 + rand() % 3, frand_range(0.1f, 0.2f));
	vocals_set(false);
	P.accCount++;
	Character *c = s->chars[0];
	if (c) {
		c->stunned = true;
		c->stunTime = 0;
		chr_sing(c, dir, "miss", CTX_MISS, P.songPos);
	}
	if (n) n->state = 2;
	subDirty = true;
}

static void update_strumline(int idx) {
	StrumLine *s = &P.sl[idx];
	float pos = P.songPos;
	while (s->first < s->nNotes && s->notes[s->first].state == 2) s->first++;
	for (int i = s->first; i < s->nNotes; i++) {
		Note *n = &s->notes[i];
		if (n->t > pos + 2000) break;
		if (n->state == 2) continue;
		if (s->cpu && n->state == 0 && n->t < pos) good_note_hit(idx, n);
		if (n->state == 1 && n->sus && n->t + n->len < pos) { n->state = 2; continue; }
		if (n->state == 0 && n->t < pos - HIT_WINDOW) {
			if (!s->cpu) note_miss(idx, n, n->dir);
			else n->state = 2;
		}
	}
}

static void update_input(int idx) {
	StrumLine *s = &P.sl[idx];
	if (s->cpu) {
		for (int d = 0; d < 4; d++)
			if (s->anim[d] == 2 && s->lastHit[d] + play_crochet() / 2 < P.songPos) s->anim[d] = 0;
		return;
	}
	bool bot = save.botplay;
	bool pressed[4], just[4], rel[4];
	bool any = false, anyJust = false;
	float pos = P.songPos;
	for (int d = 0; d < 4; d++) {
		pressed[d] = (kHeld & laneKeys[d]) != 0;
		just[d] = (kDown & laneKeys[d]) != 0;
		rel[d] = (kUp & laneKeys[d]) && !pressed[d];
	}
	if (bot) {
		// botplay: press exactly on time
		for (int d = 0; d < 4; d++) { pressed[d] = false; just[d] = false; rel[d] = false; }
		for (int i = s->first; i < s->nNotes; i++) {
			Note *n = &s->notes[i];
			if (n->t > pos) break;
			if (n->state != 0) continue;
			if (n->sus) pressed[n->dir] = true;
			else just[n->dir] = true;
		}
		for (int i = s->first; i < s->nNotes; i++) {
			Note *n = &s->notes[i];
			if (n->t > pos + 200) break;
			if (n->sus && n->state != 2 && n->t <= pos + play_step_crochet()) pressed[n->dir] = true;
		}
	}
	for (int d = 0; d < 4; d++) { any |= pressed[d]; anyJust |= just[d]; }
	if (any && s->chars[0] && s->chars[0]->ctx != CTX_DANCE) s->chars[0]->lockAnim = true;
	Note *per[4] = { NULL, NULL, NULL, NULL };
	for (int i = s->first; i < s->nNotes; i++) {
		Note *n = &s->notes[i];
		if (n->t > pos + HIT_WINDOW) break;
		if (n->state != 0) continue;
		if (n->sus) {
			if (pressed[n->dir] && n->t < pos) good_note_hit(idx, n);
		} else if (just[n->dir]) {
			bool canHit = n->t > pos - HIT_WINDOW && n->t < pos + HIT_WINDOW * 0.5f;
			if (!canHit) continue;
			Note *o = per[n->dir];
			if (!o) per[n->dir] = n;
			else if (fabsf(o->t - n->t) <= 2) n->state = 2;
			else if (n->t < o->t) per[n->dir] = n;
		}
	}
	if (!save.ghostTapping && anyJust)
		for (int d = 0; d < 4; d++) if (just[d] && !per[d]) note_miss(idx, NULL, d);
	for (int d = 0; d < 4; d++) if (per[d]) good_note_hit(idx, per[d]);
	for (int d = 0; d < 4; d++) {
		if (s->anim[d] == 2 || s->anim[d] == 1) {
			if (rel[d] || !pressed[d]) s->anim[d] = 0;
		} else if (just[d] && !bot) {
			s->anim[d] = 1;
			s->animTime[d] = 0;
		}
	}
	if (bot)
		for (int d = 0; d < 4; d++)
			if (s->anim[d] == 2 && s->lastHit[d] + play_crochet() / 2 < pos && !pressed[d]) s->anim[d] = 0;
}

// ---------------------------------------------------------------------------
// camera
// ---------------------------------------------------------------------------
static void move_camera(void) {
	if (P.curCameraTarget < 0 || P.curCameraTarget > 1) return;
	Character *c = P.sl[P.curCameraTarget].chars[0];
	if (!c || !c->visible) return;
	P.followX = chr_cam_x(c);
	P.followY = chr_cam_y(c);
}

static void cb_follow_on(void *ctx) { P.followEnabled = true; P.camMoveTween = 0; }

static void cam_tween_to_follow(float steps, int ease) {
	P.followEnabled = false;
	tween_cancel_target(&P.camX);
	tween_cancel_target(&P.camY);
	P.camMoveTween = tween_to(&P.camX, P.followX, play_step_crochet() / 1000 * steps, ease, 0, cb_follow_on, NULL);
	tween_to(&P.camY, P.followY, play_step_crochet() / 1000 * steps, ease, 0, NULL, NULL);
}

static void cam_cancel_move_tween(void) {
	if (P.camMoveTween && tween_active(P.camMoveTween)) {
		tween_cancel_target(&P.camX);
		tween_cancel_target(&P.camY);
		P.followEnabled = true;
	}
	P.camMoveTween = 0;
}

static void cb_zoom_done(void *ctx) { P.zoomTween = 0; }
static void cb_hudzoom_done(void *ctx) { P.hudZoomTween = 0; }

// ---------------------------------------------------------------------------
// events
// ---------------------------------------------------------------------------
static void run_event(const EventDef *e) {
	if (P.script->event) P.script->event(e);
	switch (e->type) {
	case EV_CAMERA_MOVEMENT:
		cam_cancel_move_tween();
		P.curCameraTarget = e->i0;
		move_camera();
		if (!e->i1) { P.camX = P.followX; P.camY = P.followY; }
		else if (e->ease != EASE_CLASSIC) cam_tween_to_follow(e->f[0], e->ease);
		break;
	case EV_CAMERA_POSITION:
		cam_cancel_move_tween();
		P.curCameraTarget = -1;
		if (e->i1) { P.followX += e->f[0]; P.followY += e->f[1]; }
		else { P.followX = e->f[0]; P.followY = e->f[1]; }
		if (!e->i0) { P.camX = P.followX; P.camY = P.followY; }
		else if (e->ease != EASE_CLASSIC) cam_tween_to_follow(e->f[2], e->ease);
		break;
	case EV_CAMERA_ZOOM: {
		bool hud = e->i1 & 1;
		float z = e->f[0] * ((e->i1 & 2) ? P.stageZoom : 1);
		if (e->i1 & 4) z *= hud ? P.hudZoom : P.zoom;
		float *cz = hud ? &P.hudZoom : &P.zoom;
		float *dz = hud ? &P.defaultHudZoom : &P.defaultCamZoom;
		int *tw = hud ? &P.hudZoomTween : &P.zoomTween;
		if (*tw) { tween_cancel(*tw); *tw = 0; }
		if (!e->i0) { *cz = z; *dz = z; }
		else if (e->ease == EASE_CLASSIC) *dz = z;
		else {
			*tw = tween_to(cz, z, play_step_crochet() / 1000 * e->f[1], e->ease, 0, hud ? cb_hudzoom_done : cb_zoom_done, NULL);
			*dz = z;    // onComplete / onUpdate keep defaultCamZoom = zoom
		}
		break;
	}
	case EV_CAMERA_BOP:
		P.camZoomingMult += e->f[0];
		break;
	case EV_CAMERA_MODULO_CHANGE:
		P.camZoomingInterval = e->f[0];
		P.camZoomingStrength = e->f[1];
		P.camZoomingEvery = e->i0;
		P.camZoomingOffset = e->f[2];
		break;
	case EV_SCROLL_SPEED_CHANGE: {
		float sp = e->f[0];
		if (e->i1) sp *= P.scrollSpeed;
		tween_cancel(P.scrollTween);
		P.scrollTween = 0;
		if (!e->i0) P.scrollSpeed = sp;
		else P.scrollTween = tween_to(&P.scrollSpeed, sp, play_step_crochet() / 1000 * e->f[1], e->ease == EASE_CLASSIC ? EASE_LINEAR : e->ease, 0, NULL, NULL);
		break;
	}
	case EV_ALT_ANIMATION_TOGGLE: {
		int i = (int)e->f[0];
		if (i < 0 || i > 1) break;
		strcpy(P.sl[i].suffix, e->i0 ? "-alt" : "");
		if (P.sl[i].chars[0]) strcpy(P.sl[i].chars[0]->idleSuffix, e->i1 ? "-alt" : "");
		break;
	}
	case EV_PLAY_ANIMATION: {
		Character *c = P.sl[e->i0 & 1].chars[0];
		if (c && chr_has_anim(c, e->s)) chr_play(c, e->s, e->i1, CTX_NONE, P.songPos);
		break;
	}
	default:
		break;
	}
}

void play_change_character(int sl, Character *n) {
	Character *o = P.sl[sl].chars[0];
	if (o == n || !n) return;
	n->x = o->x;
	n->y = o->y;
	n->sfx = o->sfx;
	n->sfy = o->sfy;
	strcpy(n->idleSuffix, o->idleSuffix);
	// newChar.playAnim(oldChar.animation.name, true, oldChar.lastAnimContext) + same frame
	if (o->anim >= 0) {
		chr_play(n, chr_anim_name(o), true, o->ctx, P.songPos);
		n->lastHit = o->lastHit;
		if (n->anim >= 0) {
			const ChrAnim *a = &n->def->anims[n->anim];
			n->frameIdx = o->frameIdx < a->nFrames ? o->frameIdx : a->nFrames - 1;
			n->animTime = n->frameIdx / a->fps;
		}
	}
	P.sl[sl].chars[0] = n;
	subDirty = true;
}

// ---------------------------------------------------------------------------
// drawing helpers for scripts
// ---------------------------------------------------------------------------
void play_layer_frame(Sheet *sh, const LayerDef *ld, int frame, u16 color, int alpha) {
	if (!sh->loaded || alpha <= 0) return;
	float k = P.cam.zoom * KDS;
	sheet_draw(sh, frame, cam_sx(&P.cam, ld->x, ld->sfx), cam_sy(&P.cam, ld->y, ld->sfy), k, color, alpha);
}

void play_layer(Sheet *sh, const LayerDef *ld, u16 color, int alpha) { play_layer_frame(sh, ld, 0, color, alpha); }

void play_game_rect(float x0, float y0, float x1, float y1, float sfx, float sfy, u16 color, float alpha) {
	gfx_rect(cam_sx(&P.cam, x0, sfx), cam_sy(&P.cam, y0, sfy), cam_sx(&P.cam, x1, sfx), cam_sy(&P.cam, y1, sfy), color, A31(alpha));
}

void play_border_rect(float x0, float y0, float x1, float y1, u16 color, float alpha) {
	gfx_rect(bx(x0), by(y0), bx(x1), by(y1), color, A31(alpha));
}

void play_draw_popups(void) {
	if (!P.shRatings.loaded) return;
	float k = P.cam.zoom * KDS;
	for (int i = 0; i < 20; i++) {
		Popup *p = &P.pops[i];
		if (!p->used) continue;
		const PartDef *pd = &P.shRatings.def->parts[P.shRatings.def->frames[p->frame].first];
		float s = p->scale / P.shRatings.def->scale;   // world units per texel
		float w = pd->w * s, h = pd->h * s;
		float sx = cam_sx(&P.cam, p->x - w / 2, 1), sy = cam_sy(&P.cam, p->y - h / 2, 1);
		sheet_draw(&P.shRatings, p->frame, sx, sy, p->scale * k, WHITE, A31(p->alpha));
	}
}

static void update_popups(float dt) {
	for (int i = 0; i < 20; i++) {
		Popup *p = &P.pops[i];
		if (!p->used) continue;
		p->vy += p->ay * dt;
		p->x += p->vx * dt;
		p->y += p->vy * dt;
		p->scaleTime += dt;
		float k = p->scaleDur > 0 ? clampf(p->scaleTime / p->scaleDur, 0, 1) : 1;
		float s0 = p->scaleTo == 0.7f * 0.95f ? 0.7f * 1.15f : 0.5f * 1.25f;
		p->scale = s0 + (p->scaleTo - s0) * ease_apply(EASE_CUBEOUT, k);
		if (p->fadeDelay > 0) p->fadeDelay -= dt;
		else {
			p->alpha -= dt / 0.2f;
			if (p->alpha <= 0) p->used = false;
		}
	}
	for (int i = 0; i < 8; i++) {
		if (!P.splash[i].used) continue;
		P.splash[i].t += dt;
		if (P.splash[i].t * 24 >= SPLASH_DEFAULT_FRAMES) P.splash[i].used = false;
	}
}

// ---------------------------------------------------------------------------
// HUD drawing (camHUD)
// ---------------------------------------------------------------------------
static void draw_strumline(int idx) {
	StrumLine *s = &P.sl[idx];
	if (!s->visible || !s->skin) return;
	const NoteSkin *sk = s->skin;
	Sheet *sh = &P.shNotes;
	if (!sh->loaded) return;
	float ha = P.camHudAlpha;
	float zs = P.hudZoomEff * KDS * s->scale;   // sheet scale = texels per (0.7 * source px)
	bool down = save.downscroll;
	float cy = strum_cy(s);
	float sy = down ? 720 - cy : cy;
	for (int d = 0; d < 4; d++) {
		float cx = strum_cx(s, d);
		int fr;
		int f = (int)(s->animTime[d] * 24);
		if (s->anim[d] == 2) {
			if (f >= sk->nConfirm[d]) f = sk->nConfirm[d] - 1;
			fr = sk->fConfirm[d] + sk->confirm[d][f];
		} else if (s->anim[d] == 1) {
			if (f >= sk->nPress[d]) f = sk->nPress[d] - 1;
			fr = sk->fPress[d] + sk->press[d][f];
		} else fr = sk->fStatic[d];
		sheet_draw(sh, fr, hx(cx), hy(sy), zs, WHITE, A31(s->alpha[d] * ha));
	}
	// notes
	float pxPerMs = 0.45f * roundf(P.scrollSpeed * 100) / 100;
	float pos = P.songPos;
	float noteHalf = sk->noteH * 0.7f * s->scale / 2;
	float topY = s->y;
	for (int i = s->first; i < s->nNotes; i++) {
		Note *n = &s->notes[i];
		float dy = (n->t - pos) * pxPerMs;
		if (dy > 720 / P.hudZoomEff + 200) break;
		if (n->state == 2) continue;
		float cx = strum_cx(s, n->dir);
		float a = s->alpha[n->dir] * ha;
		if (n->sus) {
			// one sustain piece: from its time to time + len (+56: N_WIDTHDIV2)
			float t0 = n->t, t1 = n->t + n->len;
			float y0 = topY + (t0 - pos) * pxPerMs + 56;
			float y1 = topY + (t1 - pos) * pxPerMs + 56;
			if (n->state == 1) {   // held: clipped at the strum center
				if (y0 < cy) y0 = cy;
				if (y1 <= y0) continue;
			}
			int frm = n->sus == 2 ? sk->fEnd[n->dir] : sk->fHold[n->dir];
			const PartDef *pd = &sh->def->parts[sh->def->frames[frm].first];
			float w = pd->w / sh->def->scale * s->scale;
			float x = hx(cx);
			float ww = w * P.hudZoomEff * KDS;
			if (down) { float a0 = 720 - y1, a1 = 720 - y0; y0 = a0; y1 = a1; }
			float yy0 = hy(y0), yy1 = hy(y1);
			if (!down) sheet_draw_rect(sh, frm, x - ww / 2, yy0, x + ww / 2, yy1, 0, 1, WHITE, A31(a * 0.6f));
			else sheet_draw_sub(sh, frm, x - ww / 2, yy0, x + ww / 2, yy1, 0, 1, 1, 0, WHITE, A31(a * 0.6f));
			continue;
		}
		if (n->state != 0) continue;
		float ny = topY + dy + noteHalf;
		if (down) ny = 720 - ny;
		sheet_draw(sh, sk->fNote[n->dir], hx(cx), hy(ny), zs, WHITE, A31(a));
	}
	// splashes
	if (P.shSplash.loaded) {
		for (int i = 0; i < 8; i++) {
			if (!P.splash[i].used) continue;
			int f = (int)(P.splash[i].t * 24);
			if (f >= SPLASH_DEFAULT_FRAMES) continue;
			int fr = (P.splash[i].lane * 2 + P.splash[i].var) * SPLASH_DEFAULT_FRAMES + f;
			float yy = down ? 720 - P.splash[i].y : P.splash[i].y;
			sheet_draw(&P.shSplash, fr, hx(P.splash[i].x), hy(yy), P.splash[i].scale * P.hudZoomEff * KDS, WHITE, A31(0.6f * ha));
		}
	}
}

// ---------------------------------------------------------------------------
// bottom screen
// ---------------------------------------------------------------------------
static const char *pauseItems[4] = { "resume", "restart", "options", "quit" };

static void draw_sub(void) {
	sub_clear(RGB(12, 8, 10));
	if (P.paused) {
		sub_text_centered(FONT_MDP13, 128, 8, "PAUSED", RGB(0xF7, 0xF1, 0xDF));
		for (int i = 0; i < 4; i++) {
			int y = 36 + i * 36;
			sub_fill(48, y, 160, 30, i == pauseSel ? RGB(0xF9, 0xCF, 0x51) : RGB(40, 34, 30));
			sub_text_centered(FONT_MDP13, 128, y + 8, pauseItems[i], i == pauseSel ? RGB(20, 16, 10) : RGB(0xF7, 0xF1, 0xDF));
		}
		return;
	}
#ifdef DEBUG_HUD
	{
		char d[96];
		extern u32 tex_free_bytes(void);
		snprintf(d, sizeof(d), "t %d b %d z %.2f cam %d,%d", (int)P.songPos, P.curBeat, P.cam.zoom, (int)P.camX, (int)P.camY);
		sub_text(FONT_MDP9, 2, 2, d, RGB(255, 255, 0));
		extern int perf_drops, perf_frames;
		snprintf(d, sizeof(d), "hp %.2f hud %.2f free %d drop %d/%d", P.health, P.camHudAlpha, (int)tex_free_bytes(), perf_drops, perf_frames);
		sub_text(FONT_MDP9, 2, 14, d, RGB(255, 255, 0));
	}
#endif
	if (P.script->drawSub) { P.script->drawSub(); return; }
	char b[48], sc[24];
	format_score(sc, P.score);
	snprintf(b, sizeof(b), "Score: %s", sc);
	sub_text(FONT_MDP13O, 70, 120, b, RGB(0xF7, 0xF1, 0xDF));
	snprintf(b, sizeof(b), "Misses: %d", P.misses);
	sub_text(FONT_MDP13O, 70, 140, b, RGB(0xF7, 0xF1, 0xDF));
}

// ---------------------------------------------------------------------------
// enter / leave
// ---------------------------------------------------------------------------
void play_enter(void) {
	g_fade = 1;
	audio_stop();
	memset(&P, 0, sizeof(P));
	P.song = g_song == SONG_OLB ? &song_olb : &song_toyou;
	P.script = g_song == SONG_OLB ? &olb_script : &toyou_script;
	conductor_setup();
	P.scrollSpeed = P.song->speed;
	for (int i = 0; i < 2; i++) strum_setup(&P.sl[i], &P.song->sl[i], i);
	P.health = 1;
	P.zoom = P.defaultCamZoom = 1.05f;
	P.hudZoom = P.defaultHudZoom = 1;
	P.camZoomingMult = 1;
	P.camZoomingStrength = 1;
	P.camZoomingInterval = P.song->beatsPerMeasure == 4 && P.song->stepsPerBeat == 4 ? 4 : 1;
	P.camZoomingEvery = P.song->beatsPerMeasure == 4 && P.song->stepsPerBeat == 4 ? 0 : 2;
	P.camZoomingLast = -1e9f;
	P.followEnabled = true;
	P.camMoveAmount = 25;
	P.camHudAlpha = 1;
	P.camHudVisible = true;
	P.canPause = true;
	P.showCombo = true;
	P.songPos = 0;
	P.curStep = P.curBeat = -1;
	sheet_load(&P.shRatings, &sheet_ratings, false);
	font_load(FONT_MDP13, false);
	font_load(FONT_MDP13O, false);
	font_load(FONT_MDP9, false);
	img_load(&imgIcon[0], "/sub/icon_nug_0.b16");
	img_load(&imgIcon[1], "/sub/icon_nug_1.b16");
	img_load(&imgIcon[2], "/sub/icon_nug_2.b16");
	img_load(&imgIcon[3], "/sub/icon_nug_3.b16");
	if (P.script->create) P.script->create();
	// first camera movement event at time < 10 runs during generateSong
	P.camX = P.followX;
	P.camY = P.followY;
	if (P.script->postCreate) P.script->postCreate();
	P.cam.zoom = P.zoom;
	P.cam.camX = P.camX;
	P.cam.camY = P.camY;
	subDirty = true;
	g_fade = 0;
}

void play_leave(void) {
	if (P.script->leave) P.script->leave();
	audio_stop();
	video_close();
	sheet_unload(&P.shRatings);
	sheet_unload(&P.shSplash);
	sheet_unload(&P.shNotes);
	sheet_unload(&shPause);
	for (int i = 0; i < 4; i++) img_free(&imgIcon[i]);
	for (int i = 0; i < 2; i++) { free(P.sl[i].notes); P.sl[i].notes = NULL; }
}

// ---------------------------------------------------------------------------
// pause (data/scripts/pause.hx)
// ---------------------------------------------------------------------------
static void pause_change(int d) {
	audio_sfx_pitch(SFX_SCROLL, 0.7f, frand_range(0.8f, 1.2f));
	pauseSel = (pauseSel + d + 4) % 4;
	for (int i = 0; i < 4; i++) {
		tween_cancel_target(&pauseScale[i]);
		tween_to(&pauseScale[i], i == pauseSel ? 1.5f : 1.2f, 0.2f, EASE_EXPOOUT, 0, NULL, NULL);
	}
	static const float off[4] = { -20, -30, -30, 20 };
	tween_cancel_target(&pausePtrX);
	tween_cancel_target(&pausePtrY);
	pausePtrX = (1280 - 500) / 2.0f + 100;
	extern int pause_item_width(int i);
	tween_to(&pausePtrX, pause_item_width(pauseSel) + off[pauseSel], 0.2f, EASE_EXPOOUT, 0, NULL, NULL);
	tween_to(&pausePtrY, 320 + pauseSel * 90 + 5, 0.2f, EASE_EXPOOUT, 0, NULL, NULL);
	subDirty = true;
}

int pause_item_width(int i) {
	// FlxText field width = centerBox.width (500)
	(void)i;
	return 500;
}

static void open_pause(void) {
	P.paused = true;
	pauseSel = 0;
	audio_pause_song(true);
	video_set_overlay_visible(true);
	sheet_load(&shPause, &sheet_pause, false);
	font_load(FONT_MDP13, true);
	font_load(FONT_MDP6, true);
	for (int i = 0; i < 4; i++) pauseScale[i] = 1.2f;
	pausePtrX = (1280 - 500) / 2.0f + 100;
	pausePtrY = 0;
	pause_change(0);
	subDirty = true;
}

static void close_pause(void) {
	P.paused = false;
	sheet_unload(&shPause);
	audio_pause_song(false);
	subDirty = true;
}

static void pause_select(void) {
	switch (pauseSel) {
	case 0: close_pause(); break;
	case 1: state_switch(ST_PLAY); break;      // FlxG.resetState()
	case 2: g_restartPending = true; state_switch(ST_OPTIONS); break;
	case 3:
		menu_music(false);
		state_switch(g_storyMode ? ST_STORY : ST_BONUS);
		break;
	}
}

static void update_pause(float dt) {
	pauseBgX += 30 * dt;
	pauseBgY += 20 * dt;
	if (kDown & KEY_UP) pause_change(-1);
	if (kDown & KEY_DOWN) pause_change(1);
	if (kDown & KEY_ACCEPT) { pause_select(); return; }
	if (kDown & KEY_BACK) { close_pause(); return; }
	if (kDown & KEY_TOUCH) {
		for (int i = 0; i < 4; i++) {
			int y = 36 + i * 36;
			if (touch.py >= y && touch.py < y + 30 && touch.px >= 48 && touch.px < 208) {
				if (pauseSel != i) pause_change(i - pauseSel);
				pause_select();
				return;
			}
		}
	}
}

static void draw_pause(void) {
	gfx_rect(0, 0, 256, 192, 0, A31(0.7f));
	// bgTile backdrop (menuPattern, 128 px tiles scaled 2x, color 3C59FF, alpha 0.2)
	float t = 256 * KDS;
	float ox = fmodf(pauseBgX, 256) * KDS, oy = fmodf(pauseBgY, 256) * KDS;
	for (float y = -t + oy; y < 192; y += t)
		for (float x = -t + ox; x < 256; x += t)
			sheet_draw(&shPause, PAUSE_PATTERN, x, y, KDS * 2, HEXC(0x3C59FF), A31(0.2f));
	float cx0 = (1280 - 500) / 2.0f;
	gfx_rect(bx(cx0), 0, bx(cx0 + 500), 192, 0, 31);
	// pausedText (scale 0.8, no updateHitbox: centered scaling)
	sheet_draw(&shPause, PAUSE_PAUSETEXT, bx(cx0 - 33 + PAUSETEXT_W * 0.1f), by(10 + PAUSETEXT_H * 0.1f), KDS, WHITE, 31);
	extern const char *play_song_title(void);
	extern const char *play_song_credits(int line);
	const char *title = play_song_title();
	font_draw_centered(FONT_MDP13, 128, by(153), title, RGB(0xF7, 0xF1, 0xDF), 31);
	for (int l = 0; l < 3; l++) {
		const char *c = play_song_credits(l);
		if (c) font_draw_centered(FONT_MDP6, 128, by(153 + 67) + l * 6, c, RGB(0xA3, 0x9A, 0x7E), 31);
	}
	for (int i = 0; i < 4; i++) {
		float s = pauseScale[i] / 1.5f;
		u16 col = i == pauseSel ? RGB(0xF9, 0xCF, 0x51) : RGB(0xF7, 0xF1, 0xDF);
		float w = font_width(FONT_MDP13, pauseItems[i]) * s;
		float yy = by(320 + i * 90 + 20) - 7 * s;
		font_draw_scaled(FONT_MDP13, 128 - w / 2, yy, s, pauseItems[i], col, 31);
	}
	sheet_draw(&shPause, PAUSE_POINTER, bx(pausePtrX), by(pausePtrY), KDS, WHITE, 31);
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------
void play_end_song(void) {
	P.songEnded = true;
	P.canPause = false;
	audio_stop_music();
	if (P.script->songEnd && P.script->songEnd()) return;
	state_switch(g_storyMode ? ST_STORY : ST_BONUS);
}

void play_update(float dt) {
	if (P.dead) return;
	if (P.paused) {
		update_pause(dt);
		if (subDirty) { draw_sub(); subDirty = false; }
		return;
	}
	if (P.script->update) P.script->update(dt);

	// conductor (song starts right away: introLength = 0)
	if (!P.songStarted && !P.songEnded) {
		P.songStarted = true;
		audio_play_song(g_song == SONG_OLB ? "nitro:/snd/olb_inst.raw" : "nitro:/snd/toyou_inst.raw",
			g_song == SONG_OLB ? "nitro:/snd/olb_voices.raw" : "nitro:/snd/toyou_voices.raw");
		P.songPos = 0;
		if (P.script->songStart) P.script->songStart();
	} else if (P.songStarted && !P.songEnded) {
		float t = audio_song_time();
		float predicted = P.songPos + dt * 1000;
		if (fabsf(t - predicted) > 30) P.songPos = t;
		else P.songPos = predicted + (t - predicted) * 0.1f;
		if (P.songPos < 0) P.songPos = 0;
		if (audio_song_finished()) { play_end_song(); return; }
	}
	conductor_update();
	int step = (int)floorf(P.stepF), beat = (int)floorf(P.beatF);
	if (step != P.curStep) {
		int from = step > P.curStep ? P.curStep + 1 : step;
		for (int s = from; s <= step; s++) if (P.script->step) P.script->step(s);
		P.curStep = step;
	}
	if (beat != P.curBeat) {
		int from = beat > P.curBeat ? P.curBeat + 1 : beat;
		for (int b = from; b <= beat; b++) {
			for (int i = 0; i < 2; i++) if (P.sl[i].chars[0]) chr_beat(P.sl[i].chars[0], b, P.songPos, play_step_crochet());
			if (P.script->beat) P.script->beat(b);
		}
		P.curBeat = beat;
		subDirty = true;
	}

	// camera bop
	if (P.camZooming) {
		float v = P.camZoomingEvery == 1 ? P.stepF : P.camZoomingEvery == 2 ? P.measureF : P.beatF;
		float b = P.camZoomingInterval <= 0 ? v - P.camZoomingOffset : floorf((v - P.camZoomingOffset) / P.camZoomingInterval) * P.camZoomingInterval;
		if (b != P.camZoomingLast) {
			if (P.camZoomingLast > -1e8f && P.camZoomingMult < 10) P.camZoomingMult += P.camZoomingStrength;
			else if (P.camZoomingMult < 10) P.camZoomingMult += P.camZoomingStrength;
			P.camZoomingLast = b;
		}
	}

	// events
	while (P.evIdx < P.song->nEvents && P.song->events[P.evIdx].time <= P.songPos)
		run_event(&P.song->events[P.evIdx++]);

	if (kDown & KEY_START && P.canPause) {
		open_pause();
		draw_sub();
		subDirty = false;
		return;
	}

	move_camera();

	if (P.camZooming) {
		P.camZoomingMult = P.camZoomingMult + (1 - P.camZoomingMult) * fps_ratio(0.05f, dt);
		if (!P.zoomTween) P.zoom = P.zoom + (P.defaultCamZoom - P.zoom) * fps_ratio(0.05f, dt);
		if (!P.hudZoomTween) P.hudZoom = P.hudZoom + (P.defaultHudZoom - P.hudZoom) * fps_ratio(0.05f, dt);
	}

	// keyShit
	for (int i = 0; i < 2; i++) {
		update_strumline(i);
		update_input(i);
	}
	for (int i = 0; i < 2; i++)
		for (int d = 0; d < 4; d++) P.sl[i].animTime[d] += dt;

	// characters update (super.update)
	if (P.script->postUpdate) P.script->postUpdate(dt);   // camera.hx etc. before the camera update

	// FlxCamera.update: follow lerp
	if (P.followEnabled) {
		float r = fps_ratio(0.04f, dt);
		P.camX += (P.followX - P.camX) * r;
		P.camY += (P.followY - P.camY) * r;
	}
	update_popups(dt);

	P.cam.camX = P.camX;
	P.cam.camY = P.camY;
	P.cam.zoom = P.zoom * ((P.camZoomingMult - 1) * 0.015f + 1);
	P.hudZoomEff = P.hudZoom * ((P.camZoomingMult - 1) * 0.03f + 1);

	if (P.health <= 0 && P.songStarted && !P.songEnded) {
		P.health = 0;
		P.dead = true;
		audio_stop();
		video_close();
		if (P.script->gameOver) P.script->gameOver();
		else state_switch(ST_PLAY);
		return;
	}

	if (subDirty && ++subTimer >= 6) {
		draw_sub();
		subDirty = false;
		subTimer = 0;
	}
}

void play_draw(void) {
	if (P.script->drawGameBack) P.script->drawGameBack();
	if (P.script->drawGameFront) P.script->drawGameFront();
	if (P.script->drawBorder) P.script->drawBorder();
	if (P.camHudVisible && P.camHudAlpha > 0.004f) {
		for (int i = 0; i < 2; i++) draw_strumline(i);
		if (P.script->drawHud) P.script->drawHud();
	}
	if (P.script->drawTop) P.script->drawTop();
	if (P.paused) draw_pause();
}

// pause.hx texts
const char *play_song_title(void) {
	int hs = g_song == SONG_OLB ? save.scoreOlb : save.scoreToYou;
	if (hs == 0) return "???";
	return g_song == SONG_OLB ? "One Last Bite" : "To You!";
}

const char *play_song_credits(int line) {
	if (line == 0) return g_song == SONG_OLB ? "Composer: Brayfruit" : "Composer: Landelsoup";
	if (line == 1) return g_song == SONG_OLB ? "Chart: BoruIDK" : "Chart: Brayfruit";
	if (line == 2 && g_song == SONG_TOYOU) return "this songs isnt canon!!!";
	return NULL;
}
