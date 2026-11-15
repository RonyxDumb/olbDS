// Secret soccer minigame: data/states/soccer/SoccerMenu.hx,
// SoccerModeSelect.hx and JimmySoccer.hx (type "soccer" in the main menu).
#include "engine.h"

static const char *skinName[6] = { "Jimmy", "Ollie", "Duncan", "TrifleJimmy", "Oxaya", "Bubby" };
static const u32 skinMenuColor[6] = { 0x47A6FF, 0x00AA99, 0x606060, 0x00578F, 0xFFD700, 0x9A3F2A };
static const u32 skinColor[6] = { 0x0CCAD4, 0x15885E, 0x565150, 0x0A97BA, 0xFFE344, 0xA54B44 };
static const u16 TXT = RGB(0xF7, 0xF1, 0xDF);

static Sheet shMenu, shSoc, shField;

static void letterbox(void) {
	gfx_rect(0, 0, 256, 24, 0, 31);
	gfx_rect(0, 168, 256, 192, 0, 31);
}

static bool touched(int x, int y, int w, int h) {
	return (kDown & KEY_TOUCH) && touch.px >= x && touch.px < x + w && touch.py >= y && touch.py < y + h;
}

static void stream_sound(const char *name, float vol, float pitch) {
	char p[64];
	snprintf(p, sizeof(p), "nitro:/snd/soccer/%s.raw", name);
	audio_stream_sfx(p, vol, pitch);
}

static void lower(char *d, const char *s) {
	while (*s) { *d++ = (*s >= 'A' && *s <= 'Z') ? *s + 32 : *s; s++; }
	*d = 0;
}

// outlined FlxText (FlxTextBorderStyle.OUTLINE) drawn with the outlined font
static void text_c(int font, float cx, float y, const char *s, u16 col, float a) { font_draw_centered(font, cx, y, s, col, A31(a)); }

// ===========================================================================
// SoccerMenu.hx
// ===========================================================================
static int smSel;
static bool smPanel;
static const char *smOpts[2] = { "Enter to play!", "Change skin" };

static void sm_sub(void) {
	sub_clear(RGB(10, 20, 40));
	sub_text_centered(FONT_MDP13O, 128, 6, "SOCCER", RGB(255, 255, 255));
	if (smPanel) {
		sub_text_centered(FONT_MDP13, 128, 40, "SKINS", TXT);
		sub_fill(16, 80, 40, 40, RGB(60, 60, 80));
		sub_fill(200, 80, 40, 40, RGB(60, 60, 80));
		sub_text_centered(FONT_MDP13, 36, 92, "<", TXT);
		sub_text_centered(FONT_MDP13, 220, 92, ">", TXT);
		char b[48];
		snprintf(b, sizeof(b), "Selected: %s", skinName[save.soccerSkin]);
		sub_text_centered(FONT_MDP13, 128, 92, b, TXT);
		sub_text_centered(FONT_MDP9, 128, 170, "A / B: close", RGB(150, 150, 150));
		return;
	}
	for (int i = 0; i < 2; i++) {
		sub_fill(28, 50 + i * 50, 200, 40, i == smSel ? RGB(0x00, 0x57, 0x8F) : RGB(30, 40, 60));
		sub_text_centered(FONT_MDP13, 128, 62 + i * 50, smOpts[i], TXT);
	}
	sub_text_centered(FONT_MDP9, 128, 170, "UP/DOWN  A: select  B: back", RGB(150, 150, 150));
}

static void sm_change(int c) {
	audio_sfx_pitch(SFX_SCROLL, 0.7f, frand_range(0.8f, 1.2f));
	smSel = (smSel + c + 2) % 2;
	sm_sub();
}

void soccermenu_enter(void) {
	audio_stop();
	int weird = irand_range(1, 100);
	audio_play_music(weird == 67 ? "nitro:/snd/soccerweird.raw" : "nitro:/snd/soccer.raw", true, 1);
	sheet_load(&shMenu, &sheet_soccermenu, false);
	sheet_load(&shSoc, &sheet_soccer, false);
	font_load(FONT_MDP9O, true);
	font_load(FONT_MDP13, true);
	font_load(FONT_MDP13O, true);
	font_load(FONT_MDP9, false);
	smSel = 0;
	smPanel = false;
	g_fade = 0;
	sm_change(0);
}

void soccermenu_leave(void) {
	sheet_unload(&shMenu);
	sheet_unload(&shSoc);
}

static void sm_skin(int c) {
	save.soccerSkin = (save.soccerSkin + c + 6) % 6;
	audio_sfx_pitch(SFX_SCROLL, 0.7f, frand_range(0.8f, 1.2f));
	save_write();
	sm_sub();
}

static void sm_close(void) {
	char n[24], p[40];
	lower(n, skinName[save.soccerSkin]);
	snprintf(p, sizeof(p), "select_%s", n);
	stream_sound(p, 1, 1);
	smPanel = false;
	audio_sfx(SFX_CONFIRMMENU, 1);
	sm_sub();
}

void soccermenu_update(float dt) {
	if (smPanel) {
		if (kDown & KEY_LEFT) sm_skin(-1);
		if (kDown & KEY_RIGHT) sm_skin(1);
		if (touched(16, 80, 40, 40)) sm_skin(-1);
		if (touched(200, 80, 40, 40)) sm_skin(1);
		if (kDown & (KEY_BACK | KEY_ACCEPT)) sm_close();
		return;
	}
	if (kDown & KEY_UP) sm_change(-1);
	if (kDown & KEY_DOWN) sm_change(1);
	for (int i = 0; i < 2; i++)
		if (touched(28, 50 + i * 50, 200, 40)) { if (smSel != i) sm_change(i - smSel); kDown |= KEY_A; }
	if (kDown & KEY_ACCEPT) {
		if (smSel == 0) state_switch(ST_SOCCERMODE);
		else { smPanel = true; sm_sub(); }
	}
	if (kDown & KEY_BACK) {
		audio_play_music("nitro:/snd/menu.raw", true, 1);
		state_switch(ST_MAINMENU);
	}
}

void soccermenu_draw(void) {
	// bg: scale 1280 / width, screenCenter
	float bh = SOC_MENUBG_H * 1280 / SOC_MENUBG_W;
	sheet_draw(&shMenu, SOCCERMENU_BG, mx(0), my(360 - bh / 2), MS, WHITE, 31);
	// logo: scale 0.67, screenCenter then y = -100
	sheet_draw(&shMenu, SOCCERMENU_LOGO, mx(640 - SOC_LOGO_W * 0.335f), my(-100 + SOC_LOGO_H * 0.165f), MS, WHITE, 31);
	char b[48], sc[24];
	format_score(sc, save.soccerHigh);
	snprintf(b, sizeof(b), "High Score: %s", sc);
	text_c(FONT_MDP9O, mx(640 - 400), my(250 + 3), b, HEXC(0x00578F), 1);
	format_score(sc, save.soccerWins);
	snprintf(b, sizeof(b), "Most Wins: %s", sc);
	text_c(FONT_MDP9O, mx(640 - 400), my(290 + 3), b, HEXC(0xFFAE36), 1);
	for (int i = 0; i < 2; i++) {
		if (i == smSel) snprintf(b, sizeof(b), "- %s", smOpts[i]);
		else snprintf(b, sizeof(b), "%s", smOpts[i]);
		text_c(FONT_MDP13, 128, my(500 + i * 100 + 5), b, 0, i == smSel ? 1 : 0.6f);
	}
	if (smPanel) {
		gfx_rect(0, 0, 256, 192, 0, A31(0.7f));
		gfx_rect(mx(390), my(210), mx(890), my(510), RGB(0xBB, 0xBB, 0xBB), 31);
		text_c(FONT_MDP13, 128, my(230 + 5), "SKINS", 0, 1);
		// skinBox: icons atlas frame, screenCenter on the first frame (131x145)
		float ix = 640 - 131 / 2.0f, iy = 360 - 145 / 2.0f;
		sheet_draw(&shSoc, soc_icon[save.soccerSkin], mx(ix), my(iy), MS, WHITE, 31);
		snprintf(b, sizeof(b), "Selected: %s", skinName[save.soccerSkin]);
		text_c(FONT_MDP9O, 128, my(iy + 150 + 3), b, 0, 1);
		font_draw(FONT_MDP13, mx(ix - 50), my(iy + 50), "<", 0, 31);
		font_draw(FONT_MDP13, mx(ix + 150), my(iy + 50), ">", 0, 31);
	}
	letterbox();
}

// ===========================================================================
// SoccerModeSelect.hx
// ===========================================================================
static int smoSel;

static void smo_sub(void) {
	sub_clear(RGB(10, 20, 40));
	sub_text_centered(FONT_MDP13O, 128, 6, "GAME MODES", RGB(255, 255, 255));
	static const char *n[2] = { "Normal", "Super Fast" };
	for (int i = 0; i < 2; i++) {
		sub_fill(16 + i * 120, 50, 104, 80, i == smoSel ? RGB(0x00, 0x57, 0x8F) : RGB(30, 40, 60));
		sub_text_centered(FONT_MDP13, 68 + i * 120, 84, n[i], TXT);
	}
	sub_text_centered(FONT_MDP9, 128, 170, "LEFT/RIGHT  A: play  B: back", RGB(150, 150, 150));
}

void soccermode_enter(void) {
	sheet_load(&shMenu, &sheet_soccermenu, false);
	font_load(FONT_MDP13, true);
	font_load(FONT_MDP9, true);
	smoSel = 0;
	g_fade = 0;
	audio_sfx_pitch(SFX_SCROLL, 0.7f, frand_range(0.8f, 1.2f));
	smo_sub();
}

void soccermode_leave(void) { sheet_unload(&shMenu); }

void soccermode_update(float dt) {
	int c = 0;
	if (kDown & KEY_LEFT) c = -1;
	if (kDown & KEY_RIGHT) c = 1;
	for (int i = 0; i < 2; i++)
		if (touched(16 + i * 120, 50, 104, 80)) { if (i != smoSel) c = i - smoSel; kDown |= KEY_A; }
	if (c) {
		audio_sfx_pitch(SFX_SCROLL, 0.7f, frand_range(0.8f, 1.2f));
		smoSel = (smoSel + c + 2) % 2;
		smo_sub();
	}
	if (kDown & KEY_ACCEPT) {
		save.soccerMode = smoSel;
		state_switch(ST_SOCCER);
	}
	if (kDown & KEY_BACK) state_switch(ST_SOCCERMENU);
}

void soccermode_draw(void) {
	float bh = SOC_MENUBG_H * 1280 / SOC_MENUBG_W;
	sheet_draw(&shMenu, SOCCERMENU_BG, mx(0), my(360 - bh / 2), MS, WHITE, 31);
	text_c(FONT_MDP13, 128, my(50 + 5), "GAME MODES", 0, 1);
	static const char *n[2] = { "Normal", "Super Fast" };
	for (int i = 0; i < 2; i++) {
		float x = 640 - 200 + i * 400 - 100;
		u16 col = i == smoSel ? WHITE : RGB(0x88, 0x88, 0x88);
		sheet_draw(&shMenu, i ? SOCCERMENU_SUPERFAST : SOCCERMENU_NORMAL, mx(x), my(250), MS, col, A31(i == smoSel ? 1 : 0.6f));
		text_c(FONT_MDP9, mx(x + 100), my(460 + 3), n[i], 0, 1);
	}
	letterbox();
}

// ===========================================================================
// JimmySoccer.hx
// ===========================================================================
#define BORDER 60
static float paddleSpeed, ballSpeed, basePitch;
static float plY, tyY;             // hitbox tops (x fixed)
static float plX, tyX;
static float ballX, ballY, ballVX, ballVY, ballAngle;
static bool ballStarted, newHighYuh;
static int scorePoints, playerScore, tylerScore;
static int plAnim;                 // 0 idle, 1 run
static float plT, tyT;
static int tyAnim;                 // 0 up, 1 down, 2 win, 3 lose
static float hsY, hsTextY;
static int curSkin;
static bool socDirty;

static const float gameTop = BORDER, gameBottom = 720 - BORDER, gameRight = 1280 - BORDER;

static void soc_sub(void) {
	sub_clear(RGB(10, 30, 15));
	char b[48], sc[24];
	snprintf(b, sizeof(b), "%d - %d", playerScore, tylerScore);
	sub_text_centered(FONT_MDP13O, 128, 20, b, RGB(255, 255, 255));
	format_score(sc, scorePoints);
	snprintf(b, sizeof(b), "Score: %s", sc);
	sub_text_centered(FONT_MDP13O, 128, 50, b, RGB(255, 255, 255));
	format_score(sc, save.soccerHigh);
	snprintf(b, sizeof(b), "High Score: %s", sc);
	sub_text_centered(FONT_MDP9O, 128, 80, b, RGB(255, 255, 0));
	snprintf(b, sizeof(b), "Most Wins: %d", save.soccerWins);
	sub_text_centered(FONT_MDP9O, 128, 96, b, RGB(0xFF, 0xAE, 0x36));
	sub_text_centered(FONT_MDP9, 128, 150, "UP/DOWN: move   B: back", RGB(150, 150, 150));
	socDirty = false;
}

static void reset_round(void) {
	ballStarted = false;
	ballVX = ballVY = 0;
	ballX = 640 - 10;
	ballY = 360 - 10;
	plY = 360 - 40;
	tyY = 360 - 30;
	socDirty = true;
}

static void launch_ball(void) {
	ballStarted = true;
	ballVX = ballSpeed * (rand() & 1 ? 1 : -1);
	ballVY = irand_range(-200, 200);
	socDirty = true;
}

static void cb_hs_out(void *c) {
	tween_to(&hsY, 720, 0.4f, EASE_CIRCIN, 0, NULL, NULL);
	tween_to(&hsTextY, 720, 0.2f, EASE_CIRCIN, 0, NULL, NULL);
}

static void congrats(void) {
	audio_stream_sfx("nitro:/snd/highscore.raw", 1.7f, 1);
	tween_to(&hsY, 720 - 250, 0.2f, EASE_CIRCOUT, 0, NULL, NULL);
	tween_to(&hsTextY, 720 - 250 + 170, 0.2f, EASE_CIRCOUT, 0, NULL, NULL);
	timer_after(2, cb_hs_out, NULL);
}

static void update_score(void) {
	if (scorePoints > save.soccerHigh) {
		save.soccerHigh = scorePoints;
		save_write();
		if (!newHighYuh) { newHighYuh = true; congrats(); }
	}
	audio_set_pitch(basePitch + scorePoints / 50000.0f);
	if (playerScore > save.soccerWins) {
		save.soccerWins = playerScore;
		save_write();
	}
	socDirty = true;
}

void soccer_enter(void) {
	sheet_load(&shField, &sheet_soccerfield, false);
	sheet_load(&shSoc, &sheet_soccer, false);
	font_load(FONT_MDP9O, true);
	font_load(FONT_MDP13O, true);
	font_load(FONT_MDP9, false);
	if (save.soccerMode == 1) { basePitch = 1.35f; ballSpeed = 1500; }
	else { basePitch = 1; ballSpeed = 600; }
	audio_set_pitch(basePitch);
	paddleSpeed = 500;
	curSkin = save.soccerSkin;
	plX = BORDER + 20;
	tyX = gameRight - 80;
	scorePoints = playerScore = tylerScore = 0;
	plAnim = 0;
	tyAnim = 1;
	plT = tyT = 0;
	ballAngle = 0;
	hsY = 720 + 250;
	hsTextY = hsY + 170;
	newHighYuh = save.soccerHigh == 0;
	g_fade = 0;
	reset_round();
	soc_sub();
}

void soccer_leave(void) {
	sheet_unload(&shField);
	sheet_unload(&shSoc);
	audio_set_pitch(1);
}

static void voice(const char *who, float vol, float pitch) {
	char n[24], p[40];
	lower(n, who);
	snprintf(p, sizeof(p), "chars_%s_%d", n, irand_range(1, 5));
	stream_sound(p, vol, pitch);
}

void soccer_update(float e) {
	if (kDown & KEY_BACK) {
		audio_set_pitch(1);
		state_switch(ST_SOCCERMODE);
		return;
	}
	bool moving = false;
	if (kHeld & KEY_UP) { plY -= paddleSpeed * e; if (!ballStarted) launch_ball(); moving = true; }
	if (kHeld & KEY_DOWN) { plY += paddleSpeed * e; if (!ballStarted) launch_ball(); moving = true; }
	if (touchDown) {
		// touch: follow the stylus (DS convenience, like holding UP/DOWN)
		float ty = (touch.py - 24) / MS - 65;
		if (ty < plY - 4) { plY -= paddleSpeed * e; moving = true; if (!ballStarted) launch_ball(); }
		else if (ty > plY + 4) { plY += paddleSpeed * e; moving = true; if (!ballStarted) launch_ball(); }
	}
	int na = moving ? 1 : 0;
	if (na != plAnim) { plAnim = na; plT = 0; }
	plT += e;
	tyT += e;
	ballAngle += ballVX * e * 0.8f;
	if (!ballStarted) {
		ballX = 640 - 10;
		ballY = 360 - 10;
		if (socDirty) soc_sub();
		return;
	}
	// tyler's brain
	if (ballVX > 0) {
		float belly = tyY + 35, bc = ballY + 10;
		float sp = paddleSpeed * 0.77f;
		if ((rand() % 100) < 2) sp = 0;
		if (bc < belly - 20) { tyY -= sp * e; if (tyAnim != 0) { tyAnim = 0; tyT = 0; } }
		else if (bc > belly + 20) { tyY += sp * e; if (tyAnim != 1) { tyAnim = 1; tyT = 0; } }
	} else {
		if (tyY + 35 < 360 - 50) tyY += 100 * e;
		if (tyY + 35 > 360 + 50) tyY -= 100 * e;
	}
	if (plY < gameTop) plY = gameTop;
	if (plY > gameBottom - 130) plY = gameBottom - 130;
	if (tyY < gameTop) tyY = gameTop;
	if (tyY > gameBottom - 70) tyY = gameBottom - 70;
	ballX += ballVX * e;
	ballY += ballVY * e;
	if (ballY < gameTop) { ballY = gameTop; ballVY *= -1; }
	if (ballY > gameBottom - 20) { ballY = gameBottom - 20; ballVY *= -1; }
	// FlxG.overlap(ball, playerHitbox)
	if (ballX < plX + 60 && ballX + 20 > plX && ballY < plY + 130 && ballY + 20 > plY) {
		ballVX = fabsf(ballVX) + 20;
		ballX = plX + 60 + 2;
		float dist = (ballY + 10) - (plY + 65);
		ballVY = dist * 8;
		float ratio = fabsf(dist) / (65 + 10);
		if (ratio > 1) ratio = 1;
		scorePoints += (int)(5 + ratio * 45);
		update_score();
		stream_sound("ball", 1, frand_range(0.7f, 1.2f));
		audio_sfx_pitch(SFX_BALL, 1, frand_range(0.7f, 1.2f));
		voice(skinName[curSkin], 1, frand_range(0.85f, 1.15f));
	}
	if (ballX < tyX + 50 && ballX + 20 > tyX && ballY < tyY + 70 && ballY + 20 > tyY) {
		ballVX = -fabsf(ballVX) - 20;
		ballX = tyX - 20 - 2;
		ballVY = ((ballY + 10) - (tyY + 35)) * 8;
		audio_sfx_pitch(SFX_BALL, 1, frand_range(0.8f, 1.2f));
		voice("Tyler", 1, frand_range(0.9f, 1.1f));
	}
	if (ballX < -50) {
		tylerScore++;
		tyAnim = 2; tyT = 0;
		float miss = fabsf((ballY + 10) - (plY + 65));
		int pen = 10;
		if (miss > 50) pen += (int)((miss - 50) * 0.25f);
		if (pen > 100) pen = 100;
		scorePoints -= pen;
		update_score();
		reset_round();
	}
	if (ballX > 1280 + 50) {
		playerScore++;
		tyAnim = 3; tyT = 0;
		scorePoints += 100;
		update_score();
		reset_round();
	}
	if (socDirty) soc_sub();
}

static int seq_at(const u8 *seq, int len, float t, float fps, bool loop) {
	int f = (int)(t * fps);
	if (f >= len) f = loop ? f % len : len - 1;
	return seq[f];
}

void soccer_draw(void) {
	sheet_draw(&shField, SOCCERFIELD_FIELD, mx(0), my(0.5f), MS, WHITE, 31);
	// player (setGraphicSize 100x150 + updateHitbox), drawn at hitbox - (14, 25)
	int pf = plAnim ? seq_at(soc_run[curSkin], soc_run_len[curSkin], plT, 24, true) : seq_at(soc_idle[curSkin], soc_idle_len[curSkin], plT, 24, true);
	sheet_draw(&shSoc, pf, mx(plX - 14), my(plY - 25), MS, WHITE, 31);
	static const u8 *tys[4] = { soc_ty_up, soc_ty_down, soc_ty_win, soc_ty_lose };
	static const int tyl[4] = { SOC_TY_UP_LEN, SOC_TY_DOWN_LEN, SOC_TY_WIN_LEN, SOC_TY_LOSE_LEN };
	int tf = seq_at(tys[tyAnim], tyl[tyAnim], tyT, tyAnim >= 2 ? 2 : 24, tyAnim < 2);
	sheet_draw(&shSoc, tf, mx(tyX - 50), my(tyY - 30), MS, WHITE, 31);
	sheet_draw(&shSoc, SOCCER_BALL, mx(ballX - 10), my(ballY - 10), MS, WHITE, 31);
	char b[48], sc[24];
	snprintf(b, sizeof(b), ballStarted ? "%d - %d" : "%d - %d", playerScore, tylerScore);
	text_c(FONT_MDP13O, 128, my(50 + 4), b, WHITE, 1);
	if (!ballStarted) text_c(FONT_MDP13O, 128, my(50 + 4) + 12, "(Move to Start)", WHITE, 1);
	format_score(sc, save.soccerHigh);
	snprintf(b, sizeof(b), "High Score: %s", sc);
	font_draw(FONT_MDP9O, mx(20), my(15 + 2), b, RGB(255, 255, 0), 31);
	snprintf(b, sizeof(b), "Most Wins: %d", save.soccerWins);
	font_draw(FONT_MDP9O, mx(20), my(45 + 2), b, RGB(0xFF, 0xAE, 0x36), 31);
	format_score(sc, scorePoints);
	snprintf(b, sizeof(b), "Score: %s", sc);
	text_c(FONT_MDP9O, 128, my(10 + 2), b, WHITE, 1);
	font_draw(FONT_MDP9O, mx(BORDER + 70), my(720 - 40 + 2), skinName[curSkin], HEXC(skinColor[curSkin]), 31);
	const char *ty = "Tyler";
	font_draw(FONT_MDP9O, mx(1280 - BORDER - 60) - font_width(FONT_MDP9O, ty), my(720 - 40 + 2), ty, HEXC(0xFFAE36), 31);
	sheet_draw(&shSoc, soc_icon[curSkin], mx(10), my(720 - 150), MS, WHITE, 31);
	sheet_draw(&shSoc, SOCCER_TYLERICON, mx(1280 - 110), my(720 - 110), MS, WHITE, 31);
	if (hsY < 720) {
		sheet_draw(&shSoc, soc_nhs[curSkin], mx(400), my(hsY), MS, WHITE, 31);
		font_draw(FONT_MDP9O, mx(640), my(hsTextY), "NEW HIGH SCORE!!!", WHITE, 31);
	}
	(void)skinMenuColor;
	letterbox();
}
