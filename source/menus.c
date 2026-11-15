// Menus: data/states/Intro.hx, MainMenu.hx, StoryMenu.hx, BonusMenu.hx,
// NextUp.hx, the options menu and gameOverWWCD.hx.
// The top screen shows the 1280x720 view letterboxed (MS = 0.2 DS px / px);
// the bottom screen has touch controls.
#include "engine.h"

static const u16 TXT = RGB(0xF7, 0xF1, 0xDF);
#define SUBBG RGB(14, 10, 12)

static void ms_draw(Sheet *s, int frame, float vx, float vy, float scale, float bake, u16 color, float alpha) {
	sheet_draw(s, frame, mx(vx), my(vy), MS * scale / bake, color, A31(alpha));
}

static void letterbox(void) {
	gfx_rect(0, 0, 256, 24, 0, 31);
	gfx_rect(0, 168, 256, 192, 0, 31);
}

static void sub_header(const char *title) {
	sub_clear(SUBBG);
	sub_fill(0, 0, 256, 22, RGB(30, 12, 14));
	sub_text_centered(FONT_MDP13, 128, 4, title, TXT);
}

static bool touched(int x, int y, int w, int h) {
	return (kDown & KEY_TOUCH) && touch.px >= x && touch.px < x + w && touch.py >= y && touch.py < y + h;
}

// ===========================================================================
// Intro.hx
// ===========================================================================
static Sheet shIntro;
static float iBlack, iText, iLogo;
static bool iJohnOn, iJohnVisible, iRjpVisible, iBlackVisible;
static float iJohnT;
static int iBeat;

void intro_enter(void) {
	sheet_load(&shIntro, &sheet_intro, false);
	font_load(FONT_MDP_LYR, true);
	font_load(FONT_MDP13, false);
	menu_music(false);
	audio_set_volume(0);
	iBlack = 1;
	iText = iLogo = 0;
	iJohnOn = false;
	iJohnVisible = iRjpVisible = iBlackVisible = true;
	iJohnT = 0;
	iBeat = 0;
	g_fade = 0;
	sub_header("WINNER WINNER CHICKEN DINNER");
	sub_text_centered(FONT_MDP9, 128, 170, "A: skip", RGB(150, 150, 150));
}

void intro_leave(void) { sheet_unload(&shIntro); }

static void intro_beat(int b) {
	switch (b) {
	case 1:
		tween_to(&iBlack, 0, 0.4f, EASE_CUBEOUT, 0, NULL, NULL);
		audio_stream_sfx("nitro:/snd/johnjingle.raw", 1, 1);
		iJohnOn = true;
		break;
	case 5: tween_to(&iBlack, 1, 0.67f, EASE_CUBEOUT, 0, NULL, NULL); break;
	case 6:
		iJohnVisible = iRjpVisible = iBlackVisible = false;
		tween_to(&iText, 1, 0.67f, EASE_CUBEOUT, 0, NULL, NULL);
		break;
	case 8:
		audio_stream_sfx("nitro:/snd/mdpjingle.raw", 1, 1);
		tween_to(&iLogo, 1, 0.2f, EASE_CUBEOUT, 0, NULL, NULL);
		break;
	case 12:
		tween_to(&iText, 0, 0.67f, EASE_CUBEOUT, 0, NULL, NULL);
		tween_to(&iLogo, 0, 0.67f, EASE_CUBEOUT, 0, NULL, NULL);
		break;
	case 15:
		state_switch(ST_MAINMENU);
		audio_set_volume(1);
		break;
	}
}

void intro_update(float dt) {
	int b = (int)floorf(audio_music_time() / (60000.0f / 102));
	while (iBeat < b) intro_beat(++iBeat);
	if (iJohnOn) iJohnT += dt;
	if (kDown & (KEY_ACCEPT | KEY_TOUCH)) {
		state_switch(ST_MAINMENU);
		audio_set_volume(1);
	}
}

void intro_draw(void) {
	if (iRjpVisible) ms_draw(&shIntro, INTRO_BACKCIRCLE, 640 - BACKCIRCLE_W * 0.375f, 360 - BACKCIRCLE_H * 0.375f, 0.75f, 0.75f, WHITE, 1);
	if (iJohnVisible) {
		int f = intro_john_seq[iJohnOn ? (int)(iJohnT * 24) % INTRO_JOHN_SEQ_LEN : 0];
		ms_draw(&shIntro, f, 220 + JOHN_W / 2 * 0.25f, 115 + JOHN_H / 2 * 0.25f, 0.75f, 0.75f, WHITE, 1);
	}
	if (iLogo > 0.004f) ms_draw(&shIntro, INTRO_LOGO, 640 - MDPLOGO_W * 0.13f, 360 - MDPLOGO_H * 0.13f, 0.26f, 0.26f, WHITE, iLogo);
	if (iText > 0.004f) font_draw_centered(FONT_MDP_LYR, 128, my(70 + 6), "based on:", WHITE, A31(iText));
	if (iBlackVisible && iBlack > 0.004f) gfx_rect(0, 0, 256, 192, 0, A31(iBlack));
	letterbox();
}

// ===========================================================================
// MainMenu.hx
// ===========================================================================
static Sheet shBg, shMain, shChar, shEye;
static const char *btnNames[4] = { "story", "bonus", "settings", "credits" };
static const float btnX[4] = { 29, 189, 9, 239 }, btnY[4] = { 40, 200, 358, 450 };
static int mSel;
static int mAnim[4];               // 1 idle, 2 hover, 3 click
static float mAnimT[4];
static float mScaleX[4], mScaleY[4];
static bool mCanDo;
static float mBlack, mEye, mTile;
static float bubbyX;
static bool mRetro, mCredits, mKeyboard;
static char typed[24];
static float mTileX, mTileY;

static void m_change(int c) {
	audio_sfx_pitch(SFX_SCROLL, 0.7f, frand_range(0.8f, 1.2f));
	mSel = (mSel + c + 4) % 4;
	for (int i = 0; i < 4; i++) {
		tween_cancel_target(&mScaleX[i]);
		tween_cancel_target(&mScaleY[i]);
		mAnim[i] = i == mSel ? 2 : 1;
		mAnimT[i] = 0;
		float s = i == mSel ? 0.95f : 0.9f;
		tween_to(&mScaleX[i], s, 0.2f, EASE_EXPOOUT, 0, NULL, NULL);
		tween_to(&mScaleY[i], s, 0.2f, EASE_EXPOOUT, 0, NULL, NULL);
	}
}

static void cb_eye(void *c) { tween_to(&mEye, 1, 1, EASE_QUADINOUT, 0, NULL, NULL); }
static void cb_reveal(void *c) {
	if (g_eyeIntroSeen) return;
	tween_to(&mBlack, 0, 1, EASE_QUADINOUT, 0, NULL, NULL);
	g_eyeIntroSeen = true;
	mCanDo = true;
}
static void cb_story(void *c) { state_switch(ST_STORY); }
static void cb_bonus(void *c) { state_switch(ST_BONUS); }
static void cb_settings(void *c) { state_switch(ST_OPTIONS); }
static void m_draw_sub(void);
static void cb_credits(void *c) { mCredits = true; m_draw_sub(); }

void mainmenu_enter(void) {
	menu_music(true);
	sheet_load(&shBg, &sheet_mainbg, false);
	sheet_load(&shMain, &sheet_mainmenu, false);
	bool ending = any_ending();
	mRetro = g_retroMusic;
	sheet_load(&shChar, mRetro ? &sheet_chars_old : ending ? &sheet_chars_true : &sheet_chars_normal, false);
	sheet_load(&shEye, ending ? &sheet_eye_true : &sheet_eye_normal, false);
	font_load(FONT_MDP9, false);
	font_load(FONT_MDP13, false);
	font_load(FONT_VCR10, false);
	for (int i = 0; i < 4; i++) { mScaleX[i] = mScaleY[i] = 1; mAnim[i] = 1; }
	mSel = 0;
	mBlack = 1;
	mEye = 0;
	bubbyX = -600;
	mCredits = mKeyboard = false;
	typed[0] = 0;
	m_change(0);
	// postCreate
	if (!g_eyeIntroSeen) {
		mCanDo = false;
		timer_after(0.5f, cb_eye, NULL);
		timer_after(10, cb_reveal, NULL);
	} else {
		mCanDo = true;
		mBlack = 0;
	}
	g_fade = 0;
	m_draw_sub();
}

void mainmenu_leave(void) {
	sheet_unload(&shBg);
	sheet_unload(&shMain);
	sheet_unload(&shChar);
	sheet_unload(&shEye);
	if (mRetro) {
		// destroy(): back to freakyMenu
		g_retroMusic = false;
		audio_stop_music();
		menu_music(false);
	}
}

static void m_select(void) {
	audio_sfx(SFX_CONFIRM, 1);
	audio_sfx(SFX_CHOMP, 0.1f);
	mAnim[mSel] = 3;
	mAnimT[mSel] = 0;
	// FlxTween BACKWARD: from (1.05, 0.9) back to the current scale
	float sx = mScaleX[mSel], sy = mScaleY[mSel];
	tween_cancel_target(&mScaleX[mSel]);
	tween_cancel_target(&mScaleY[mSel]);
	tween_num(&mScaleX[mSel], 1.05f, sx, 0.15f, EASE_CIRCIN, 0, NULL, NULL);
	tween_num(&mScaleY[mSel], 0.9f, sy, 0.15f, EASE_CIRCIN, 0, NULL, NULL);
	switch (mSel) {
	case 0: mCanDo = false; timer_after(0.7f, cb_story, NULL); break;
	case 1: mCanDo = false; timer_after(0.7f, cb_bonus, NULL); break;
	case 2: mCanDo = false; timer_after(0.7f, cb_settings, NULL); break;
	case 3: timer_after(0.2f, cb_credits, NULL); break;
	}
}

// typed codes (FlxG.stage.window.onTextInput)
static void m_type(char ch) {
	int l = strlen(typed);
	if (l >= 20) { memmove(typed, typed + 1, l); l--; }
	typed[l] = ch;
	typed[l + 1] = 0;
	l++;
	if (l >= 6 && !strcmp(typed + l - 6, "soccer")) {
		typed[0] = 0;
		mCanDo = false;
		state_switch(ST_SOCCERMENU);
	}
	if (l >= 5 && !strcmp(typed + l - 5, "retro")) {
		typed[0] = 0;
		audio_sfx(SFX_CONFIRM, 1);
		sheet_unload(&shChar);
		sheet_load(&shChar, &sheet_chars_old, false);
		mRetro = true;
		g_retroMusic = true;
		audio_stop_music();
		audio_play_music("nitro:/snd/menuold.raw", true, 1);
	}
	if (l >= 5 && !strcmp(typed + l - 5, "bubby")) {
		bubbyX = -600;
		tween_cancel_target(&bubbyX);
		tween_to(&bubbyX, 1100, 0.6f, EASE_LINEAR, 0, NULL, NULL);
		char p[48];
		snprintf(p, sizeof(p), "nitro:/snd/soccer/chars_bubby_%d.raw", irand_range(1, 5));
		audio_stream_sfx(p, 1.4f, frand_range(0.85f, 1.15f));
	}
}

static const char *credits[] = {
	"CREDITS", "",
	"Brayfruit - Director, Artist, Animator,", "  Composer, Programmer",
	"Borupen - Co-Director, Artist, Charter",
	"EnzuPenzu - Artist, Animator",
	"igpie - Artist, Animator",
	"Sibottle - Programmer",
	"KATPizza - MDP-Styled Sprites",
	"Esburger - MDP Rigs, Playtester",
	"Rain - MDP Rigs",
	"Onyxium, Az the Maz, Foxywuz - Art",
	"Landelsoup - 'To You' Guest Composer",
	"Amy - Programming Help",
	"Special thanks: MAXPROLOVER998",
	"(TWIDDLEFINGER), Max Design Pro", NULL
};

static void m_draw_sub(void) {
	sub_header("WINNER WINNER CHICKEN DINNER");
	if (mCredits) {
		for (int i = 0; credits[i]; i++) sub_text(FONT_VCR10, 6, 25 + i * 9, credits[i], i == 0 ? RGB(0xF9, 0xCF, 0x51) : TXT);
		sub_text_centered(FONT_VCR10, 128, 182, "B / tap: back", RGB(140, 140, 140));
		return;
	}
	if (mKeyboard) {
		static const char *rows[3] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };
		sub_text_centered(FONT_MDP9, 128, 30, "type something...", RGB(160, 160, 160));
		sub_text_centered(FONT_MDP13, 128, 46, typed, TXT);
		for (int r = 0; r < 3; r++) {
			int n = strlen(rows[r]);
			int x0 = (256 - n * 24) / 2;
			for (int i = 0; i < n; i++) {
				int x = x0 + i * 24, y = 80 + r * 28;
				sub_fill(x + 1, y, 22, 24, RGB(50, 40, 40));
				char c[2] = { rows[r][i], 0 };
				sub_text_centered(FONT_MDP13, x + 12, y + 5, c, TXT);
			}
		}
		sub_text_centered(FONT_VCR10, 128, 176, "SELECT: close keyboard", RGB(140, 140, 140));
		return;
	}
	static const char *lbl[4] = { "STORY", "BONUS", "SETTINGS", "CREDITS" };
	for (int i = 0; i < 4; i++) {
		int x = 16 + (i % 2) * 116, y = 40 + (i / 2) * 58;
		sub_fill(x, y, 108, 48, i == mSel ? RGB(0xC0, 0x30, 0x30) : RGB(50, 30, 32));
		sub_text_centered(FONT_MDP13, x + 54, y + 16, lbl[i], TXT);
	}
	sub_text_centered(FONT_VCR10, 128, 160, "UP/DOWN: select  A: confirm", RGB(140, 140, 140));
	sub_text_centered(FONT_VCR10, 128, 174, "SELECT: keyboard", RGB(110, 110, 110));
}

void mainmenu_update(float dt) {
	mTileX += 30 * dt;
	mTileY += 20 * dt;
	for (int i = 0; i < 4; i++) mAnimT[i] += dt;
	if (mCredits) {
		if (kDown & (KEY_B | KEY_A | KEY_TOUCH)) { mCredits = false; m_draw_sub(); }
		return;
	}
	if (kDown & KEY_SELECT) { mKeyboard = !mKeyboard; m_draw_sub(); return; }
	if (mKeyboard) {
		if (kDown & KEY_TOUCH) {
			static const char *rows[3] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };
			for (int r = 0; r < 3; r++) {
				int n = strlen(rows[r]);
				int x0 = (256 - n * 24) / 2;
				for (int i = 0; i < n; i++)
					if (touched(x0 + i * 24, 80 + r * 28, 24, 26)) m_type(rows[r][i]);
			}
			m_draw_sub();
		}
		if (kDown & KEY_B) { mKeyboard = false; m_draw_sub(); }
		return;
	}
	if (mCanDo) {
		int old = mSel;
		if (kDown & KEY_UP) m_change(-1);
		if (kDown & KEY_DOWN) m_change(1);
		if (kDown & KEY_ACCEPT) m_select();
		for (int i = 0; i < 4; i++) {
			int x = 16 + (i % 2) * 116, y = 40 + (i / 2) * 58;
			if (touched(x, y, 108, 48)) {
				if (i != mSel) m_change(i - mSel);
				m_select();
			}
		}
		if (old != mSel || (kDown & (KEY_ACCEPT | KEY_TOUCH))) m_draw_sub();
	} else if (!g_eyeIntroSeen && (kDown & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_START | KEY_TOUCH | KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT))) {
		tween_to(&mBlack, 0, 1, EASE_QUADINOUT, 0, NULL, NULL);
		g_eyeIntroSeen = true;
		mCanDo = true;
	}
}

void mainmenu_draw(void) {
	ms_draw(&shBg, MAINBG_BG, 0, 0, 1, 1, WHITE, 1);
	// bgTile: FlxBackdrop menuPattern x2, alpha 0.1, color 9e0000
	float t = 256 * MS;
	float ox = fmodf(mTileX, 256) * MS, oy = fmodf(mTileY, 256) * MS;
	for (float y = -t + oy + 24; y < 168; y += t)
		for (float x = -t + ox; x < 256; x += t)
			sheet_draw(&shMain, MAINMENU_PATTERN, x, y, MS, HEXC(0x9E0000), A31(0.1f));
	ms_draw(&shMain, MAINMENU_CHAINSAW, 190 + CHAINSAW_W / 2 - CHAINSAW_W * 0.8f, (720 - CHAINSAW_H) / 2 + CHAINSAW_H / 2 - CHAINSAW_H * 0.6f, 1, 1, WHITE, 1);
	ms_draw(&shMain, MAINMENU_CRUMBS, 40 + 391 * 0.04f, 37 + 630 * 0.04f, 0.92f, 0.92f, WHITE, 1);
	ms_draw(&shChar, 0, 640 - 2560 * 0.255f, 360 - 1440 * 0.255f, 0.51f, 0.51f, WHITE, 1);
	for (int i = 0; i < 4; i++) {
		int f = menu_nug_frames[i * 3 + mAnim[i] - 1];
		// origin = center of the idle frame box (updateHitbox -> centerOrigin)
		float ox2 = menu_nug_size[i][0] / 2, oy2 = menu_nug_size[i][1] / 2;
		float x = btnX[i] + ox2 * (1 - mScaleX[i]), y = btnY[i] + oy2 * (1 - mScaleY[i]);
		sheet_draw_ex(&shMain, f, mx(x), my(y), MS * mScaleX[i], MS * mScaleY[i], WHITE, 31);
	}
	if (mBlack > 0.004f) gfx_rect(0, 0, 256, 192, 0, A31(mBlack));
	if (!mRetro && mEye > 0.004f) ms_draw(&shEye, 0, 640 - 2560 * 0.255f, 360 - 1440 * 0.255f, 0.51f, 0.51f, WHITE, mEye);
	if (bubbyX > -590) ms_draw(&shMain, MAINMENU_BUBBY, bubbyX, 250, 0.2f, 0.2f, WHITE, 1);
	letterbox();
}

// ===========================================================================
// StoryMenu.hx
// ===========================================================================
static Sheet shStoryBg, shStory;
static int sSel;
static float spotX, spotY;
static bool sSelected;
static char sTitle[32], sHigh[32];
static const float paperX[4] = { 120, 360, 600, 840 }, paperY[4] = { 270, 170, 270, 170 };
static const float paperW[4] = { PAPER1_W, PAPER2_W, PAPER3_W, PAPER4_W }, paperH[4] = { PAPER1_H, PAPER2_H, PAPER3_H, PAPER4_H };

static void s_draw_sub(void) {
	sub_header("STORY");
	for (int i = 0; i < 4; i++) {
		int x = 8 + i * 61;
		sub_fill(x, 50, 56, 70, i == sSel ? RGB(0xC0, 0x30, 0x30) : RGB(50, 30, 32));
		char b[4];
		snprintf(b, sizeof(b), "%d", i + 1);
		sub_text_centered(FONT_MDP13, x + 28, 76, b, TXT);
	}
	sub_text_centered(FONT_MDP13, 128, 130, sTitle, TXT);
	sub_text_centered(FONT_MDP9, 128, 148, sHigh, TXT);
	sub_text_centered(FONT_VCR10, 128, 176, "LEFT/RIGHT  A: play  B: back", RGB(140, 140, 140));
}

static void s_change(int c, bool instant, bool absolute) {
	if (!instant) audio_sfx_pitch(SFX_SCROLL, 0.7f, frand_range(0.8f, 1.2f));
	sSel = absolute ? c : (sSel + c + 4) % 4;
	if (sSel == 0) {
		strcpy(sTitle, save.scoreOlb == 0 ? "???" : "One Last Bite");
		char sc[24];
		format_score(sc, save.scoreOlb);
		snprintf(sHigh, sizeof(sHigh), "HIGH SCORE: %s", sc);
	} else {
		strcpy(sTitle, "Coming Soon!");
		strcpy(sHigh, "...probably");
	}
	float tx = paperX[sSel] + paperW[sSel] / 2 - SPOTLIGHT_W / 2, ty = paperY[sSel] + paperH[sSel] / 2 - SPOTLIGHT_H / 2;
	tween_cancel_target(&spotX);
	tween_cancel_target(&spotY);
	if (instant) { spotX = tx; spotY = ty; }
	else {
		tween_to(&spotX, tx, 0.2f, EASE_CIRCOUT, 0, NULL, NULL);
		tween_to(&spotY, ty, 0.2f, EASE_CIRCOUT, 0, NULL, NULL);
	}
	s_draw_sub();
}

static void cb_story_play(void *c) {
	g_song = SONG_OLB;
	g_storyMode = true;
	state_switch(ST_PLAY);
}

void story_enter(void) {
	menu_music(true);
	sheet_load(&shStoryBg, &sheet_storybg, false);
	sheet_load(&shStory, &sheet_story, false);
	font_load(FONT_MDP13, true);
	font_load(FONT_MDP6, true);
	font_load(FONT_MDP9, false);
	font_load(FONT_VCR10, false);
	sSelected = false;
	g_fade = 0;
	s_change(0, true, true);
}

void story_leave(void) {
	sheet_unload(&shStoryBg);
	sheet_unload(&shStory);
}

static void s_select(void) {
	if (sSelected) return;
	if (sSel == 0) {
		sSelected = true;
		audio_sfx(SFX_CONFIRM, 1);
		tween_num(&g_fade, 0, 1, 0.5f, EASE_LINEAR, 0, cb_story_play, NULL);
		audio_fade_music(0, 0.5f);
	} else audio_sfx(SFX_BUZZER, 1);
}

void story_update(float dt) {
	if (sSelected) return;
	if (kDown & KEY_RIGHT) s_change(1, false, false);
	if (kDown & KEY_LEFT) s_change(-1, false, false);
	if (kDown & KEY_BACK) { state_switch(ST_MAINMENU); return; }
	if (kDown & KEY_ACCEPT) s_select();
	for (int i = 0; i < 4; i++)
		if (touched(8 + i * 61, 50, 56, 70)) {
			if (i != sSel) s_change(i, false, true);
			s_select();
		}
}

void story_draw(void) {
	ms_draw(&shStoryBg, 0, 0, 0, 0.5f, 0.5f, WHITE, 1);
	for (int i = 0; i < 4; i++)
		ms_draw(&shStory, STORY_PAPER1 + i, paperX[i] + paperW[i] * 0.2f, paperY[i] + paperH[i] * 0.2f, 0.6f, 0.6f, WHITE, 1);
	ms_draw(&shStory, STORY_SPOTLIGHT, spotX + SPOTLIGHT_W / 2 - SPOTLIGHT_W * 1.25f, spotY + SPOTLIGHT_H / 2 - SPOTLIGHT_H * 1.25f, 2.5f, 2.5f, WHITE, 0.8f);
	font_draw_centered(FONT_MDP13, 128, my(20 + 4), sTitle, WHITE, 31);
	font_draw_centered(FONT_MDP6, 128, my(90 + 2), sHigh, WHITE, 31);
	letterbox();
}

// ===========================================================================
// BonusMenu.hx
// ===========================================================================
static Sheet shBonus;
static int bSel, bCol;
static float bScale[10], bAlpha[10];
static float bTileX, bTileY;
static bool bSelected;
static bool bUnlocked[4];
static char bTitle[32], bHigh[32];
static const s8 bNav[10][4] = {   // up, down, left, right (-1 = none)
	{ -1, -1, 7, 1 }, { -1, 2, 0, 3 }, { 1, -1, 0, 5 }, { -1, 5, 1, 4 }, { -1, 5, 3, 6 },
	{ 3, -1, 2, 8 }, { -1, 8, 4, 7 }, { -1, 9, 6, 0 }, { 6, -1, 5, 9 }, { 7, -1, 8, 0 },
};
static const u8 bColOf[10] = { 0, 1, 1, 2, 2, 2, 3, 3, 3, 3 };
static float bBtnX[10], bBtnY[10];

static void b_layout(void) {
	float pw = 1280 / 4.0f, w = 500 * 0.47f, h = 500 * 0.47f;
	static const u8 colItems[4][4] = { { 0 }, { 1, 2 }, { 3, 4, 5 }, { 6, 7, 8, 9 } };
	static const u8 colN[4] = { 1, 2, 3, 4 };
	for (int c = 0; c < 4; c++) {
		float cx = pw * c + pw / 2, cy = 440;
		for (int k = 0; k < colN[c]; k++) {
			int i = colItems[c][k];
			float x, y;
			if (c == 2) {
				if (k == 0) { x = cx - 75 - w / 2; y = cy - 80 - h / 2; }
				else if (k == 1) { x = cx + 75 - w / 2; y = cy - 80 - h / 2; }
				else { x = cx - w / 2; y = cy + 80 - h / 2; }
			} else if (c == 3) {
				x = cx + (k % 2 ? 75 : -75) - w / 2;
				y = cy + (k < 2 ? -80 : 80) - h / 2;
			} else {
				float total = (colN[c] - 1) * 170;
				x = cx - w / 2;
				y = cy - total / 2 - h / 2 + k * 170;
			}
			bBtnX[i] = x;
			bBtnY[i] = y;
		}
	}
}

static void b_draw_sub(void) {
	sub_header("BONUS");
	static const u16 cols[4] = { RGB(0x6D, 0x0A, 0x32), RGB(0x33, 0x61, 0x14), RGB(0x0D, 0x4C, 0x56), RGB(0x51, 0x13, 0x54) };
	for (int i = 0; i < 10; i++) {
		int x = 8 + (int)(bBtnX[i] / 1280 * 240), y = 30 + (int)((bBtnY[i] - 140) / 580 * 120);
		sub_fill(x, y, 40, 30, i == bSel ? RGB(240, 240, 240) : cols[bColOf[i]]);
		if (i == 0 && bUnlocked[0]) sub_text_centered(FONT_VCR10, x + 20, y + 10, "To You", i == bSel ? RGB(20, 20, 20) : TXT);
	}
	sub_text_centered(FONT_MDP13, 128, 150, bTitle, TXT);
	sub_text_centered(FONT_MDP9, 128, 166, bHigh, TXT);
	sub_text_centered(FONT_VCR10, 128, 180, "D-PAD  A: play  B: back", RGB(140, 140, 140));
}

static void b_change(int newId, int dir) {
	if (bSelected) return;
	int old = bSel;
	if (dir < 0) bSel = newId;
	else if (bNav[bSel][dir] >= 0) bSel = bNav[bSel][dir];
	for (int i = 0; i < 10; i++) {
		tween_cancel_target(&bScale[i]);
		bAlpha[i] = i == bSel ? 1 : 0.6f;
		tween_to(&bScale[i], i == bSel ? 0.47f : 0.4f, 0.2f, EASE_CIRCOUT, 0, NULL, NULL);
	}
	if (old == bSel) { b_draw_sub(); return; }
	audio_sfx_pitch(SFX_SCROLL, 0.7f, frand_range(0.8f, 1.2f));
	bCol = bColOf[bSel];
	if (bSel == 0) {
		char sc[24];
		strcpy(bTitle, save.scoreToYou ? "To You" : "???");
		format_score(sc, save.scoreToYou);
		snprintf(bHigh, sizeof(bHigh), "HIGH SCORE: %s", sc);
		if (!bUnlocked[bCol]) { strcpy(bTitle, "???"); strcpy(bHigh, "Song is locked!"); }
	} else {
		strcpy(bTitle, "Coming Soon!");
		strcpy(bHigh, "...probably");
	}
	b_draw_sub();
}

static void cb_bonus_play(void *c) {
	g_song = SONG_TOYOU;
	g_storyMode = false;
	state_switch(ST_PLAY);
}

void bonus_enter(void) {
	menu_music(true);
	sheet_load(&shBonus, &sheet_bonus, false);
	font_load(FONT_MDP13, true);
	font_load(FONT_MDP6, true);
	font_load(FONT_MDP9, false);
	font_load(FONT_VCR10, false);
	memset(bUnlocked, 0, sizeof(bUnlocked));
	if (any_ending()) bUnlocked[0] = true;
	b_layout();
	for (int i = 0; i < 10; i++) { bScale[i] = 0.47f; bAlpha[i] = 0.6f; }
	bSel = 0;
	bSelected = false;
	strcpy(bTitle, "???");
	strcpy(bHigh, "HIGH SCORE: 0");
	b_change(0, 2);
	b_change(0, 3);
	g_fade = 0;
}

void bonus_leave(void) { sheet_unload(&shBonus); }

static void b_select(void) {
	if (!bSelected && bSel == 0 && bUnlocked[bCol]) {
		bSelected = true;
		audio_sfx(SFX_CONFIRM, 1);
		tween_num(&g_fade, 0, 1, 0.5f, EASE_LINEAR, 0, cb_bonus_play, NULL);
		audio_fade_music(0, 0.5f);
	} else audio_sfx(SFX_BUZZER, 1);
}

void bonus_update(float dt) {
	bTileX += 30 * dt;
	bTileY += 20 * dt;
	if (bSelected) return;
	if (kDown & KEY_RIGHT) b_change(0, 3);
	if (kDown & KEY_LEFT) b_change(0, 2);
	if (kDown & KEY_DOWN) b_change(0, 1);
	if (kDown & KEY_UP) b_change(0, 0);
	if (kDown & KEY_ACCEPT) b_select();
	if (kDown & KEY_BACK) { state_switch(ST_MAINMENU); return; }
	for (int i = 0; i < 10; i++) {
		int x = 8 + (int)(bBtnX[i] / 1280 * 240), y = 30 + (int)((bBtnY[i] - 140) / 580 * 120);
		if (touched(x, y, 40, 30)) {
			if (i != bSel) b_change(i, -1);
			b_select();
		}
	}
}

void bonus_draw(void) {
	static const u32 top[4] = { 0x2C1013, 0x192D1A, 0x1B1B35, 0x1F1126 }, bot[4] = { 0x6D0A32, 0x336114, 0x0D4C56, 0x511354 };
	for (int c = 0; c < 4; c++) gfx_gradient(mx(320 * c), my(140), mx(320 * (c + 1)), my(720), HEXC(top[c]), HEXC(bot[c]), 31);
	float t = 256 * MS;
	float ox = fmodf(bTileX, 256) * MS, oy = fmodf(bTileY, 256) * MS;
	for (float y = -t + oy + 24; y < 168; y += t)
		for (float x = -t + ox; x < 256; x += t)
			sheet_draw(&shBonus, BONUS_PATTERN, x, y, MS, 0, A31(0.15f));
	for (int i = 0; i < 10; i++) {
		bool unl = bUnlocked[bColOf[i]] && i == 0;
		float s = bScale[i];
		float cx = bBtnX[i] + 500 * 0.47f / 2, cy = bBtnY[i] + 500 * 0.47f / 2;
		ms_draw(&shBonus, unl ? BONUS_TOYOU : BONUS_NULL, cx - 250 * s, cy - 250 * s, s, 0.47f, WHITE, bAlpha[i]);
	}
	gfx_rect(0, 0, 256, my(140), 0, 31);
	font_draw_centered(FONT_MDP13, 128, my(20 + 4), bTitle, WHITE, 31);
	font_draw_centered(FONT_MDP6, 128, my(90 + 2), bHigh, WHITE, 31);
	letterbox();
}

// ===========================================================================
// NextUp.hx
// ===========================================================================
static Sheet shNText, shNImg;
static float nText, nImg;
static void cb_n1(void *c) { tween_to(&nText, 1, 1, EASE_QUADINOUT, 0, NULL, NULL); }
static void cb_n2(void *c) {
	tween_to(&nImg, 1, 0.3f, EASE_QUADINOUT, 0, NULL, NULL);
	audio_stream_sfx("nitro:/snd/jingle_cf.raw", 1, 1);
}
static void cb_n3(void *c) {
	tween_to(&nText, 0, 1, EASE_QUADINOUT, 0, NULL, NULL);
	tween_to(&nImg, 0, 2, EASE_QUADINOUT, 0, NULL, NULL);
}
static void cb_n4(void *c) { state_switch(ST_MAINMENU); }

void nextup_enter(void) {
	g_eyeIntroSeen = false;
	audio_stop();
	sheet_load(&shNText, &sheet_nextup_text, false);
	sheet_load(&shNImg, &sheet_nextup_img, false);
	nText = nImg = 0;
	timer_after(0.5f, cb_n1, NULL);
	timer_after(2.3f, cb_n2, NULL);
	timer_after(5, cb_n3, NULL);
	timer_after(6, cb_n4, NULL);
	g_fade = 0;
	sub_clear(0);
}

void nextup_leave(void) {
	sheet_unload(&shNText);
	sheet_unload(&shNImg);
}

void nextup_update(float dt) {}

void nextup_draw(void) {
	ms_draw(&shNImg, 0, 0, 0, 1, 1, WHITE, nImg);
	ms_draw(&shNText, 0, 0, 0, 1, 1, WHITE, nText);
	letterbox();
}

// ===========================================================================
// Options (Codename OptionsMenu + data/config/options.xml)
// ===========================================================================
extern bool g_restartPending;
static int oSel;
#define NOPT 8

static const char *o_label(int i, char *buf) {
	switch (i) {
	case 0: sprintf(buf, "Downscroll: %s", save.downscroll ? "ON" : "OFF"); break;
	case 1: sprintf(buf, "Ghost Tapping: %s", save.ghostTapping ? "ON" : "OFF"); break;
	case 2: sprintf(buf, "Note Offset: %d ms", save.noteOffset); break;
	case 3: sprintf(buf, "Botplay: %s", save.botplay ? "ON" : "OFF"); break;
	case 4: sprintf(buf, "Lyric Subtitles: %s", save.lyricSubs ? "ON" : "OFF"); break;
	case 5: sprintf(buf, "Reset Mod Progress"); break;
	case 6: sprintf(buf, "Reset Soccer Scores"); break;
	default: sprintf(buf, "Back"); break;
	}
	return buf;
}

static const char *o_desc(int i) {
	switch (i) {
	case 0: return "Notes scroll down instead of up.";
	case 1: return "Pressing a key with no note is not a miss.";
	case 2: return "Shifts the note timing (LEFT/RIGHT).";
	case 3: return "The game plays by itself.";
	case 4: return "Turns on/off lyric-section captions.";
	case 5: return "Press A to reset the mods progression.";
	case 6: return "Press A to reset your Soccer high scores.";
	default: return "";
	}
}

static void o_draw_sub(void) {
	sub_header("OPTIONS");
	char buf[48];
	for (int i = 0; i < NOPT; i++) {
		int y = 28 + i * 18;
		if (i == oSel) sub_fill(8, y - 2, 240, 16, RGB(0xC0, 0x30, 0x30));
		sub_text_centered(FONT_VCR10, 128, y + 2, o_label(i, buf), TXT);
	}
	sub_text_centered(FONT_VCR10, 128, 176, o_desc(oSel), RGB(170, 170, 170));
}

void options_enter(void) {
	oSel = 0;
	font_load(FONT_VCR10, true);
	font_load(FONT_VCR16, true);
	if (!g_restartPending) menu_music(false);
	g_fade = 0;
	o_draw_sub();
}

void options_leave(void) { save_write(); }

static void o_back(void) {
	audio_sfx(SFX_CANCEL, 0.7f);
	if (g_restartPending) { g_restartPending = false; state_switch(ST_PLAY); }
	else state_switch(ST_MAINMENU);
}

static void o_change(int dir) {
	switch (oSel) {
	case 0: save.downscroll = !save.downscroll; break;
	case 1: save.ghostTapping = !save.ghostTapping; break;
	case 2:
		save.noteOffset += (dir ? dir : 1) * 5;
		if (save.noteOffset > 500) save.noteOffset = 500;
		if (save.noteOffset < -500) save.noteOffset = -500;
		break;
	case 3: save.botplay = !save.botplay; break;
	case 4: save.lyricSubs = !save.lyricSubs; break;
	case 5:
		if (dir) return;
		save.olbGood = save.olbBad = false;     // resetWWCD
		audio_sfx(SFX_CONFIRM, 0.7f);
		break;
	case 6:
		if (dir) return;
		save.soccerHigh = save.soccerWins = 0;  // resetSoccer
		audio_sfx(SFX_CONFIRM, 0.7f);
		break;
	case 7:
		if (!dir) o_back();
		return;
	}
	audio_sfx(SFX_SCROLL, 0.6f);
	o_draw_sub();
}

void options_update(float dt) {
	if (kDown & KEY_UP) { oSel = (oSel + NOPT - 1) % NOPT; audio_sfx(SFX_SCROLL, 0.6f); o_draw_sub(); }
	if (kDown & KEY_DOWN) { oSel = (oSel + 1) % NOPT; audio_sfx(SFX_SCROLL, 0.6f); o_draw_sub(); }
	if (kDown & KEY_A) o_change(0);
	if (kDown & KEY_LEFT) o_change(-1);
	if (kDown & KEY_RIGHT) o_change(1);
	if (kDown & KEY_B) { o_back(); return; }
	for (int i = 0; i < NOPT; i++) {
		if (touched(8, 26 + i * 18, 240, 16)) {
			oSel = i;
			o_change(i == 2 ? (touch.px < 128 ? -1 : 1) : 0);
			o_draw_sub();
		}
	}
}

void options_draw(void) {
	char buf[48];
	gfx_gradient(0, 0, 256, 192, RGB(0x2C, 0x10, 0x13), RGB(0x6D, 0x0A, 0x32), 31);
	font_draw_centered(FONT_VCR16, 128, 10, "OPTIONS", WHITE, 31);
	for (int i = 0; i < NOPT; i++) {
		int y = 38 + i * 18;
		if (i == oSel) gfx_rect(16, y - 3, 240, y + 13, RGB(0xF9, 0xCF, 0x51), 12);
		font_draw_centered(FONT_VCR10, 128, y, o_label(i, buf), WHITE, i == oSel ? 31 : 18);
	}
}

// ===========================================================================
// gameOverWWCD.hx: death video, then PlayState again
// ===========================================================================
static float goT;

void gameover_enter(void) {
	audio_stop();
	g_fade = 0;
	goT = 0;
	if (!video_open("nitro:/vid/death_olb.vid", false, false)) { state_switch(ST_PLAY); return; }
	audio_play_music("nitro:/snd/death_olb.raw", false, 1);
	sub_clear(0);
	font_load(FONT_VCR10, false);
	sub_text_centered(FONT_VCR10, 128, 176, "A: retry", RGB(120, 120, 120));
}

void gameover_leave(void) {
	video_close();
	audio_stop();
}

void gameover_update(float dt) {
	goT += dt;
	if (!video_update(goT * 1000)) { state_switch(ST_PLAY); return; }
	if (kDown & KEY_ACCEPT) state_switch(ST_PLAY);
}

void gameover_draw(void) {}
