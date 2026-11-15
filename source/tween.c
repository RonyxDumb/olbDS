// Minimal FlxTween/FlxTimer replacement
#include "engine.h"

#define MAX_TWEENS 96

typedef struct {
	bool used;
	int id;
	float *target;
	float from, to, dur, t, delay;
	int ease;
	bool fromCurrent;
	TweenCb cb;
	void *ctx;
} Tween;

static Tween tw[MAX_TWEENS];
static int nextId = 1;

static float ease_in(int base, float t) {
	switch (base) {
	case 1: return t * t;
	case 2: return t * t * t;
	case 3: return t * t * t * t;
	case 4: return t * t * t * t * t;
	case 5: return -cosf(t * (float)M_PI / 2) + 1;
	case 6: return -(sqrtf(1 - t * t) - 1);
	case 7: return t == 0 ? 0 : powf(2, 10 * (t - 1));
	case 8: return t * t * (2.70158f * t - 1.70158f);
	case 9: { float u = t / 2; return 2 * (u * u * (u * -2 + 3)); }
	case 10: { float u = t / 2; return 2 * (u * u * u * (u * (u * 6 - 15) + 10)); }
	case 11: {
		if (t <= 0 || t >= 1) return t;
		float u = t - 1;
		return -(powf(2, 10 * u) * sinf((u - 0.4f / 4) * (2 * (float)M_PI) / 0.4f));
	}
	case 12: {
		float u = 1 - t;
		float b;
		if (u < 1 / 2.75f) b = 7.5625f * u * u;
		else if (u < 2 / 2.75f) { u -= 1.5f / 2.75f; b = 7.5625f * u * u + 0.75f; }
		else if (u < 2.5f / 2.75f) { u -= 2.25f / 2.75f; b = 7.5625f * u * u + 0.9375f; }
		else { u -= 2.625f / 2.75f; b = 7.5625f * u * u + 0.984375f; }
		return 1 - b;
	}
	default: return t;
	}
}

float ease_apply(int e, float t) {
	if (e == EASE_CLASSIC || e <= 0) return t;
	int base = e / 3, dir = e % 3;
	if (base == 0) return t;
	if (base == 9) {   // smoothStep (FlxEase definitions)
		float io = t * t * (t * -2 + 3);
		if (dir == 2) return io;
		if (dir == 0) { float u = t / 2; return 2 * (u * u * (u * -2 + 3)); }
		{ float u = t / 2 + 0.5f; return 2 * (u * u * (u * -2 + 3)) - 1; }
	}
	if (base == 10) {
		if (dir == 2) return t * t * t * (t * (t * 6 - 15) + 10);
		if (dir == 0) { float u = t / 2; return 2 * (u * u * u * (u * (u * 6 - 15) + 10)); }
		{ float u = t / 2 + 0.5f; return 2 * (u * u * u * (u * (u * 6 - 15) + 10)) - 1; }
	}
	if (dir == 0) return ease_in(base, t);
	if (dir == 1) return 1 - ease_in(base, 1 - t);
	if (t < 0.5f) return ease_in(base, t * 2) / 2;
	return 1 - ease_in(base, (1 - t) * 2) / 2;
}

static int add(float *target, float from, float to, float dur, int ease, float delay, TweenCb cb, void *ctx, bool fromCur) {
	for (int i = 0; i < MAX_TWEENS; i++) {
		if (!tw[i].used) {
			Tween *t = &tw[i];
			t->used = true;
			t->id = nextId++;
			t->target = target;
			t->from = from;
			t->to = to;
			t->dur = dur;
			t->t = 0;
			t->delay = delay;
			t->ease = ease;
			t->cb = cb;
			t->ctx = ctx;
			t->fromCurrent = fromCur;
			return t->id;
		}
	}
	return 0;
}

int tween_num(float *target, float from, float to, float dur, int ease, float delay, TweenCb cb, void *ctx) {
	if (target && delay <= 0) *target = from;
	return add(target, from, to, dur, ease, delay, cb, ctx, false);
}

// like FlxTween.tween: the start value is read when the tween starts
int tween_to(float *target, float to, float dur, int ease, float delay, TweenCb cb, void *ctx) {
	return add(target, target ? *target : 0, to, dur, ease, delay, cb, ctx, true);
}

int timer_after(float delay, TweenCb cb, void *ctx) {
	return add(NULL, 0, 0, 0, EASE_LINEAR, delay, cb, ctx, false);
}

void tween_cancel(int id) {
	if (!id) return;
	for (int i = 0; i < MAX_TWEENS; i++)
		if (tw[i].used && tw[i].id == id) tw[i].used = false;
}

bool tween_active(int id) {
	if (!id) return false;
	for (int i = 0; i < MAX_TWEENS; i++)
		if (tw[i].used && tw[i].id == id) return true;
	return false;
}

void tween_cancel_target(float *target) {
	for (int i = 0; i < MAX_TWEENS; i++)
		if (tw[i].used && tw[i].target == target) tw[i].used = false;
}

void tween_clear(void) {
	for (int i = 0; i < MAX_TWEENS; i++) tw[i].used = false;
}

void tween_update(float dt0) {
	for (int i = 0; i < MAX_TWEENS; i++) {
		Tween *t = &tw[i];
		if (!t->used) continue;
		int id = t->id;
		float dt = dt0;
		if (t->delay > 0) {
			t->delay -= dt;
			if (t->delay > 0) continue;
			if (t->fromCurrent && t->target) t->from = *t->target;
			dt = -t->delay;
			t->delay = 0;
		} else if (t->t == 0 && t->fromCurrent && t->target) {
			t->from = *t->target;
		}
		t->t += dt;
		float k = t->dur > 0 ? t->t / t->dur : 1;
		if (k >= 1) k = 1;
		if (t->target) *t->target = t->from + (t->to - t->from) * ease_apply(t->ease, k);
		if (k >= 1 && t->used && t->id == id) {
			t->used = false;
			if (t->cb) t->cb(t->ctx);
		}
	}
}
