// To You: songs/To You/scripts/odeslopEvents.hx on the Abbey stage
// (+ songs/camera.hx and the ui.hx "noUI" mode).
#include "play.h"

static Sheet shBg, shBack, shFace, shSigns, shIlove, shGround, shFrame, shAspect, shLove;
static Character cMaxi, cScary, cDarling, cHeart, cBorder;
static bool realVisible;
static float boxA, frameA, loveA, borderA, heartA;
static float animT;
static bool strumsVisible;
static float strumA;

static void realStage(bool show) { realVisible = show; }

static void create(void) {
	P.stageZoom = AB_ZOOM;
	P.defaultCamZoom = P.zoom = AB_ZOOM;
	P.followX = P.followY = 0;
	chr_slots_reset();
	ChrSlot *sa = chr_slot_new(4, 4), *sb = chr_slot_new(3, 4), *sh = chr_slot_new(4, 4), *sbo = chr_slot_new(4, 5);
	chr_load(&cMaxi, CHR_MAXIMILLIAN, sa, true);
	chr_load(&cScary, CHR_MAXIMILLIANSCARY, sa, true);
	chr_load(&cDarling, CHR_DARLING, sb, true);
	chr_load(&cHeart, CHR_HEART, sh, false);
	chr_load(&cBorder, CHR_BORDERS, sbo, false);
	cMaxi.x = cScary.x = AB_DAD_X; cMaxi.y = cScary.y = AB_DAD_Y;
	cDarling.x = AB_BF_X; cDarling.y = AB_BF_Y;
	P.sl[0].chars[0] = &cMaxi;
	P.sl[1].chars[0] = &cDarling;
	sheet_load(&shBg, layer_ab_bg.sheet, true);
	sheet_load(&shBack, layer_ab_back.sheet, true);
	sheet_load(&shFace, layer_ab_face.sheet, true);
	sheet_load(&shSigns, layer_ab_signs.sheet, true);
	sheet_load(&shIlove, layer_ab_iloveyou.sheet, true);
	sheet_load(&shGround, layer_ab_ground.sheet, true);
	sheet_load(&shFrame, layer_ab_frame.sheet, false);
	sheet_load(&shAspect, layer_ab_aspect.sheet, false);
	sheet_load(&shLove, layer_ab_love.sheet, true);
	// onStrumCreation / onNoteCreation: red notes + red splashes
	sheet_load(&P.shNotes, noteskin_red.sheet, false);
	sheet_load(&P.shSplash, &sheet_splash_red, false);
	for (int i = 0; i < 2; i++) play_set_noteskin(i, &noteskin_red);
	realStage(false);
	cHeart.x = 50; cHeart.y = -280;
	cHeart.alpha = 0;
	chr_play(&cHeart, "start", true, CTX_NONE, 0);
	loveA = 0;
	frameA = 0;
	borderA = 0;
	boxA = 1;
	heartA = 0;
	animT = 0;
	strumsVisible = true;
	strumA = 1;
	P.showCombo = false;              // comboGroup.visible = false
}

static void songStart(void) {
	for (int d = 0; d < 4; d++) P.sl[1].alpha[d] = 0;   // tween over 0.000001 s
	tween_num(&heartA, 0, 1, 2.5f, EASE_CUBEINOUT, 0, NULL, NULL);
}

static void beat(int b) {
	if (P.curCameraTarget == 0) {
		if (b >= 203) P.camMoveAmount = 75;
	} else P.camMoveAmount = 25;
}

static void step(int s) {
	switch (s) {
	case 5: chr_play(&cHeart, "anim", true, CTX_NONE, 0); break;
	case 25:
		tween_to(&cHeart.y, 200, 1.8f, EASE_QUARTIN, 0, NULL, NULL);
		tween_to(&heartA, 0, 1.8f, EASE_QUARTIN, 0, NULL, NULL);
		break;
	case 48:
		tween_to(&frameA, 1, 1, EASE_QUARTOUT, 0, NULL, NULL);
		tween_to(&boxA, 0, 1, EASE_QUARTOUT, 0, NULL, NULL);
		for (int d = 0; d < 4; d++) tween_to(&P.sl[1].alpha[d], 1, 1, EASE_QUARTOUT, 0, NULL, NULL);
		break;
	case 816:
		realStage(true);
		tween_to(&borderA, 1, 1.8f, EASE_QUARTOUT, 0, NULL, NULL);
		break;
	case 1152:
		P.sl[1].visible = false;
		tween_cancel_target(&boxA);
		boxA = 1;
		P.camMoveAmount = 0;
		loveA = 1;
		tween_to(&frameA, 0, 0.5f, EASE_QUARTOUT, 0, NULL, NULL);
		tween_to(&borderA, 0, 0.5f, EASE_QUARTOUT, 0, NULL, NULL);
		break;
	case 1176: tween_to(&loveA, 0, 0.15f, EASE_QUARTOUT, 0, NULL, NULL); break;
	}
}

static void event(const EventDef *e) {
	if (e->type == EV_CHANGE_CHARACTER) {
		int sl = e->i0 & 1;
		Character *n = !strcmp(e->s, "maximillianSCARY") ? &cScary : !strcmp(e->s, "maximillian") ? &cMaxi :
			!strcmp(e->s, "darling") ? &cDarling : NULL;
		if (n) play_change_character(sl, n);
	}
}

static void update(float dt) {
	float sc = play_step_crochet();
	chr_update(&cMaxi, dt, P.songPos, sc);
	chr_update(&cScary, dt, P.songPos, sc);
	chr_update(&cDarling, dt, P.songPos, sc);
	chr_update(&cHeart, dt, 0, 0);
	// heart.animation.callback: "anim" finished -> "stay"
	if (cHeart.anim >= 0 && !strcmp(chr_anim_name(&cHeart), "anim") && cHeart.finished) chr_play(&cHeart, "stay", true, CTX_NONE, 0);
	chr_update(&cBorder, dt, 0, 0);
	if (cBorder.anim < 0) chr_play(&cBorder, "loop", true, CTX_NONE, 0);
	animT += dt;
}

// camera.hx
static void postUpdate(float dt) {
	if (P.curCameraTarget < 0 || P.curCameraTarget > 1) return;
	Character *c = P.sl[P.curCameraTarget].chars[0];
	if (!c) return;
	const char *an = chr_anim_name(c);
	if (strstr(an, "singLEFT")) P.followX -= P.camMoveAmount;
	else if (strstr(an, "singRIGHT")) P.followX += P.camMoveAmount;
	else if (strstr(an, "singUP")) P.followY -= P.camMoveAmount;
	else if (strstr(an, "singDOWN")) P.followY += P.camMoveAmount;
}

static int seq_frame(const u8 *seq, int len) { return seq[(int)(animT * 24) % len]; }

static void drawGameBack(void) {
	if (!realVisible) play_layer(&shBg, &layer_ab_bg, WHITE, 31);
	if (realVisible) {
		play_layer_frame(&shBack, &layer_ab_back, seq_frame(ab_back_seq, AB_BACK_LEN), WHITE, 31);
		play_layer_frame(&shFace, &layer_ab_face, seq_frame(ab_face_seq, AB_FACE_LEN), WHITE, 31);
		play_layer(&shSigns, &layer_ab_signs, WHITE, 31);
		play_layer_frame(&shIlove, &layer_ab_iloveyou, seq_frame(ab_iloveyou_seq, AB_ILOVEYOU_LEN), WHITE, 31);
		play_layer(&shGround, &layer_ab_ground, WHITE, 31);
	}
	chr_draw(P.sl[0].chars[0], &P.cam, WHITE);
	chr_draw(P.sl[1].chars[0], &P.cam, WHITE);
}

static void drawGameFront(void) {
	if (boxA > 0.004f) play_game_rect(AB_BOX_X, AB_BOX_Y, AB_BOX_X + AB_BOX_W, AB_BOX_Y + AB_BOX_H, 1, 1, 0, boxA);
	if (loveA > 0.004f) {
		// love (102x97, scale 1.7 around its center)
		float x = 143 + AB_LOVE_W / 2 - AB_LOVE_W * 1.7f / 2, y = 369 + AB_LOVE_H / 2 - AB_LOVE_H * 1.7f / 2;
		sheet_draw(&shLove, 0, cam_sx(&P.cam, x + layer_ab_love.x, 1), cam_sy(&P.cam, y + layer_ab_love.y, 1), P.cam.zoom * KDS, WHITE, A31(loveA));
	}
}

static void drawBorder(void) {
	// heart on camBorder (screen space, zoom 1)
	if (heartA > 0.004f) {
		static Cam bc = { 640, 360, 1, 0, 0 };
		chr_draw_alpha(&cHeart, &bc, WHITE, heartA);
	}
}

static void drawHud(void) {
	if (frameA > 0.004f && shFrame.loaded) {
		// frame: screenCenter, scale 0.67 around the center (camHUD)
		float w = AB_FRAME_W * 0.67f, h = AB_FRAME_H * 0.67f;
		float x = 640 - w / 2 + layer_ab_frame.x, y = 360 - h / 2 + layer_ab_frame.y;
		sheet_draw(&shFrame, 0, hx(x), hy(y), P.hudZoomEff * KDS, WHITE, A31(frameA * P.camHudAlpha));
	}
}

static void drawTop(void) {
	// camAspect: aspect bars + animated borders
	static Cam ac = { 640, 360, 1, 0, 0 };
	if (shAspect.loaded) sheet_draw(&shAspect, 0, bx(layer_ab_aspect.x), by(layer_ab_aspect.y), KDS, WHITE, 31);
	if (borderA > 0.004f) {
		cBorder.scaleSign = 1;
		cBorder.x = -40; cBorder.y = -120;
		chr_draw_alpha(&cBorder, &ac, WHITE, borderA);
		cBorder.scaleSign = -1;
		cBorder.x = 960;
		chr_draw_alpha(&cBorder, &ac, WHITE, borderA);
	}
}

static void drawSub(void) {
	char b[48], sc[24];
	float h = P.health / 2;
	int idx = h > 0.75f ? 0 : h > 0.5f ? 1 : h > 0.25f ? 2 : 3;
	extern Img16 toyou_icons[4];
	sub_blit(&toyou_icons[idx], 4, 192 - toyou_icons[idx].h - 4);
	format_score(sc, P.score);
	snprintf(b, sizeof(b), "Score: %s", sc);
	sub_text(FONT_MDP13O, 90, 140, b, RGB(0xF7, 0xF1, 0xDF));
	snprintf(b, sizeof(b), "Misses: %d", P.misses);
	sub_text(FONT_MDP13O, 90, 160, b, RGB(0xF7, 0xF1, 0xDF));
}

Img16 toyou_icons[4];

static void create_wrap(void) {
	create();
	for (int i = 0; i < 4; i++) {
		char p[32];
		snprintf(p, sizeof(p), "/sub/icon_nug_%d.b16", i);
		img_load(&toyou_icons[i], p);
	}
}

static bool songEnd(void) {
	if (P.score > save.scoreToYou) { save.scoreToYou = P.score; save.missesToYou = P.misses; }
	save_write();
	state_switch(ST_BONUS);
	return true;
}

// gameOverWWCD.hx: no death video for To You -> restart right away
static void gameOver(void) { state_switch(ST_PLAY); }

static void leave(void) {
	Sheet *all[] = { &shBg, &shBack, &shFace, &shSigns, &shIlove, &shGround, &shFrame, &shAspect, &shLove };
	for (unsigned i = 0; i < sizeof(all) / sizeof(all[0]); i++) sheet_unload(all[i]);
	Character *cs[] = { &cMaxi, &cScary, &cDarling, &cHeart, &cBorder };
	for (unsigned i = 0; i < 5; i++) chr_unload(cs[i]);
	for (int i = 0; i < 4; i++) img_free(&toyou_icons[i]);
}

const SongScript toyou_script = {
	.create = create_wrap, .songStart = songStart, .update = update, .postUpdate = postUpdate,
	.beat = beat, .step = step, .event = event, .drawGameBack = drawGameBack, .drawGameFront = drawGameFront,
	.drawBorder = drawBorder, .drawHud = drawHud, .drawTop = drawTop, .drawSub = drawSub,
	.songEnd = songEnd, .leave = leave, .gameOver = gameOver,
};
