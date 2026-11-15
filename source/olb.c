// One Last Bite: songs/One Last Bite/scripts/olbEvents.hx + songs/ui.hx +
// songs/camera.hx + data/events/lyrics.hx, on the youtubeland stage.
#include "play.h"

// stage
static Sheet shSky, shClouds, shCityL, shCityR, shRoad, shBuild, shShadowM, shShadowN, shMdp, shTreeR, shTreeL;
static Sheet shUi, shWatermark, shLogo, shBroDied, shHelp;
static bool realLoaded;
static bool realVisible;           // realStage(): real group visible, MDP hidden
static bool mdpSprVisible;
static Character cMaxMdp, cNugMdp, cMax, cNugget, cMaxRun, cNugRun, cPibby;
static bool pibbyOn, runOn;

// script state
static bool MDP;                   // public static var MDP
static int likeCount;
static bool unlockedGoodEnding;
static float blackA, black2A, likeY, likeScale;
static bool black2On, yellowScreenOn, logoOn, logoWhite, broDiedOn, redTintOn, likeOn;
static bool yellowBGOn, blueBGOn;
static float blueA;
static int lyricsVid;              // 0 none, 1 bad, 2 good
static float lyricsStart;
static bool lyricsVisible;
static bool scoreUiVisible;        // scoreText / missText / healthIcon alpha
static char lyrics[64];
static float healthLerp;
static bool areMdpNotes, dontZoomYet;
static int pendingMdpOff;          // MDP -> false: swap assets during the black screen
static int tapFlash;

static void realStage(bool show) {
	realVisible = show;
	mdpSprVisible = !show;
}

// ---------------------------------------------------------------------------
// asset swapping (VRAM is too small for the MDP stage and the real one)
// ---------------------------------------------------------------------------
static void load_real_stage(void) {
	if (realLoaded) return;
	sheet_unload(&shMdp);
	sheet_unload(&shUi);
	sheet_unload(&shWatermark);
	sheet_load(&shSky, layer_yt_sky.sheet, true);
	sheet_load(&shClouds, layer_yt_clouds.sheet, true);
	sheet_load(&shCityL, layer_yt_cityleft.sheet, true);
	sheet_load(&shCityR, layer_yt_cityright.sheet, true);
	sheet_load(&shRoad, layer_yt_road.sheet, true);
	sheet_load(&shBuild, layer_yt_buildings.sheet, true);
	sheet_load(&shShadowM, layer_yt_maxshadow.sheet, true);
	sheet_load(&shShadowN, layer_yt_nugshadow.sheet, true);
	sheet_load(&shTreeR, layer_yt_treeright.sheet, true);
	sheet_load(&shTreeL, layer_yt_treeleft.sheet, true);
	sheet_unload(&P.shNotes);
	sheet_load(&P.shNotes, noteskin_default.sheet, false);
	sheet_load(&shHelp, &sheet_helpme, false);
	realLoaded = true;
}

// ---------------------------------------------------------------------------
// hooks
// ---------------------------------------------------------------------------
static void create(void) {
	P.stageZoom = YT_ZOOM;
	P.defaultCamZoom = P.zoom = YT_ZOOM;
	P.followX = YT_CAMX;
	P.followY = YT_CAMY;
	chr_slots_reset();
	ChrSlot *sa = chr_slot_new(4, 4), *sb = chr_slot_new(4, 4), *sp = chr_slot_new(3, 3);
	chr_load(&cMaxMdp, CHR_MAXMDP, sa, true);
	chr_load(&cNugMdp, CHR_NUGMDP, sb, true);
	chr_load(&cMax, CHR_MAX, sa, true);
	chr_load(&cNugget, CHR_NUGGET, sb, true);
	chr_load(&cMaxRun, CHR_MAXRUN, sa, false);
	chr_load(&cNugRun, CHR_NUGRUN, sb, false);
	chr_load(&cPibby, CHR_PIBBY, sp, false);
	Character *dads[2] = { &cMaxMdp, &cMax }, *bfs[2] = { &cNugMdp, &cNugget };
	for (int i = 0; i < 2; i++) {
		dads[i]->x = YT_DAD_X; dads[i]->y = YT_DAD_Y; dads[i]->sfy = YT_DAD_SFY;
		bfs[i]->x = YT_BF_X; bfs[i]->y = YT_BF_Y; bfs[i]->sfy = YT_BF_SFY;
	}
	P.sl[0].chars[0] = &cMaxMdp;
	P.sl[1].chars[0] = &cNugMdp;
	sheet_load(&shMdp, layer_yt_mdp.sheet, true);
	sheet_load(&P.shNotes, noteskin_mdp.sheet, false);
	sheet_load(&P.shSplash, &sheet_splash_default, false);
	sheet_load(&shUi, &sheet_olbui, false);
	sheet_load(&shWatermark, &sheet_watermark, false);
	font_load(FONT_MDP_LYR, true);
	realLoaded = false;
	// olbEvents.create
	P.camHudAlpha = 0;
	realStage(false);
	MDP = true;
	likeCount = 0;
	unlockedGoodEnding = false;
	pibbyOn = runOn = false;
	cMaxRun.x = -800; cMaxRun.y = 220;
	cNugRun.x = -200; cNugRun.y = 550;
	cPibby.x = 120; cPibby.y = 300;
	cPibby.alpha = 0.5f;
	healthLerp = 1;
	scoreUiVisible = true;
	lyrics[0] = 0;
	// ui.hx create: MDP -> no zooming, mdp notes
	P.camZooming = false;
	dontZoomYet = true;
	for (int i = 0; i < 2; i++) {
		play_set_noteskin(i, &noteskin_mdp);
		P.sl[i].y = P.sl[i].def->y;     // b.y = a.startingPos.y
	}
	for (int d = 0; d < 4; d++) P.sl[1].x[d] += 49;
	areMdpNotes = true;
	pendingMdpOff = 0;
	tapFlash = 0;
}

static void postCreate(void) {
	blackA = 1;
	black2On = false;
	black2A = 0;
	yellowScreenOn = false;
	logoOn = false;
	logoWhite = true;
	broDiedOn = false;
	redTintOn = false;
	likeOn = false;
	yellowBGOn = blueBGOn = false;
	lyricsVid = 0;
	lyricsVisible = false;
	shader_tint(1, 1, 1);
}

static void songStart(void) {
	tween_num(&blackA, 1, 0, 2, EASE_CUBEINOUT, 0, NULL, NULL);
}

static void cb_likeY(void *ctx) {}

static void beat(int b) {
	switch (b) {
	case 2: tween_to(&P.camHudAlpha, 1, 2, EASE_CUBEOUT, 0, NULL, NULL); break;
	case 65: tween_to(&P.camHudAlpha, 0, 1, EASE_CUBEIN, 0, NULL, NULL); break;
	case 66:
		tween_to(&blackA, 1, 0.67f, EASE_QUINTIN, 0, NULL, NULL);
		pibbyOn = true;
		chr_play(&cPibby, "scary", true, CTX_NONE, 0);
		break;
	case 70:
		realStage(true);
		pibbyOn = false;
		MDP = false;
		pendingMdpOff = 1;
		break;
	case 72: tween_cancel_target(&blackA); blackA = 0; break;
	case 76: tween_to(&P.camHudAlpha, 1, 1, EASE_CUBEOUT, 0, NULL, NULL); break;
	case 211: tween_to(&blackA, 1, 0.2f, EASE_CUBEIN, 0, NULL, NULL); break;
	case 212:
		realStage(false);
		mdpSprVisible = false;
		P.sl[0].chars[0]->alpha = 0;
		P.sl[1].chars[0]->alpha = 0;
		yellowBGOn = true;
		blueBGOn = true;
		blueA = 0;
		P.camMoveAmount = 0;
		tween_to(&blackA, 0, 0.2f, EASE_CUBEOUT, 0, NULL, NULL);
		runOn = true;
		chr_play(&cMaxRun, "run", true, CTX_NONE, 0);
		tween_to(&cMaxRun.x, 25, 2, EASE_QUARTOUT, 0, NULL, NULL);
		chr_play(&cNugRun, "run", true, CTX_NONE, 0);
		tween_to(&cNugRun.x, 985, 1.8f, EASE_QUARTOUT, 0, NULL, NULL);
		break;
	case 274:
		chr_play(&cMaxRun, "huh", true, CTX_NONE, 0);
		cNugRun.offX = 30; cNugRun.offY = -80;
		chr_play(&cNugRun, "slide", true, CTX_NONE, 0);
		tween_cancel_target(&cMaxRun.x);
		tween_cancel_target(&cNugRun.x);
		tween_to(&cMaxRun.x, -250, 1.5f, EASE_QUARTOUT, 0, NULL, NULL);
		tween_to(&cNugRun.x, 800, 1.2f, EASE_QUARTOUT, 0, NULL, NULL);
		tween_to(&P.camHudAlpha, 0, 1, EASE_CUBEINOUT, 0, NULL, NULL);
		break;
	case 275:
		chr_play(&cNugRun, "what", true, CTX_NONE, 0);
		cNugRun.offX = 0; cNugRun.offY = -80;
		break;
	case 276:
		chr_play(&cMaxRun, "crank", true, CTX_NONE, 0);
		cMaxRun.offX = 100; cMaxRun.offY = -50;
		chr_play(&cNugRun, "help", true, CTX_NONE, 0);
		tween_to(&blueA, 1, 10, EASE_SINEINOUT, 0, NULL, NULL);
		likeOn = true;
		likeY = -110;
		likeScale = 0.7f;
		tween_to(&likeY, 90, 1, EASE_CUBEOUT, 0, NULL, NULL);
		play_sub_dirty();
		break;
	case 306: tween_cancel_target(&likeY); tween_to(&likeY, -110, 0.5f, EASE_CUBEIN, 0, cb_likeY, NULL); break;
	case 307: tween_to(&blackA, 1, 0.2f, EASE_CUBEIN, 0, NULL, NULL); break;
	case 310:
		if (unlockedGoodEnding) audio_stream_sfx("nitro:/snd/goodsfx.raw", 1, 1);
		yellowBGOn = blueBGOn = false;
		runOn = false;
		likeOn = false;
		scoreUiVisible = false;
		play_sub_dirty();
		break;
	case 312:
		lyricsVid = unlockedGoodEnding ? 2 : 1;
		if (video_open(unlockedGoodEnding ? "nitro:/vid/olb_good.vid" : "nitro:/vid/olb_bad.vid", false, false)) {
			lyricsVisible = true;
			lyricsStart = P.songPos;
		}
		break;
	case 313: tween_to(&blackA, 0, 0.3f, EASE_CUBEOUT, 0, NULL, NULL); break;
	case 328: tween_to(&P.camHudAlpha, 0.67f, 1, EASE_CUBEINOUT, 0, NULL, NULL); break;
	case 368: tween_to(&P.camHudAlpha, 0, 1, EASE_CUBEINOUT, 0, NULL, NULL); break;
	case 386:
		P.sl[0].chars[0]->alpha = 1;
		P.sl[1].chars[0]->alpha = 1;
		realStage(true);
		P.camMoveAmount = 25;
		if (unlockedGoodEnding) {
			strcpy(P.sl[1].suffix, "-alt");
			strcpy(P.sl[1].chars[0]->idleSuffix, "-alt");
		}
		scoreUiVisible = true;
		tween_to(&P.camHudAlpha, 1, 1, EASE_CUBEINOUT, 0, NULL, NULL);
		play_sub_dirty();
		break;
	case 388:
		redTintOn = true;
		video_close();
		lyricsVisible = false;
		lyricsVid = 0;
		break;
	case 460:
		if (!unlockedGoodEnding) {
			P.camHudVisible = false;
			broDiedOn = true;
			sheet_load(&shBroDied, layer_brodied.sheet, true);
		}
		break;
	case 462:
		if (unlockedGoodEnding) { tween_cancel_target(&blackA); blackA = 1; }
		else { yellowScreenOn = true; logoWhite = false; }
		// the game camera is covered from here on: free VRAM for the logo
		{
			Sheet *st[] = { &shSky, &shClouds, &shCityL, &shCityR, &shRoad, &shBuild, &shShadowM, &shShadowN, &shTreeR, &shTreeL, &shBroDied, &shHelp };
			for (unsigned i = 0; i < sizeof(st) / sizeof(st[0]); i++) sheet_unload(st[i]);
			realLoaded = false;
			realVisible = false;
			sheet_unload(&P.shSplash);
		}
		sheet_load(&shLogo, logoWhite ? layer_logo_olbwhite.sheet : layer_logo_olb.sheet, false);
		logoOn = true;
		black2On = true;
		black2A = 0;
		P.camHudVisible = false;
		redTintOn = false;
		broDiedOn = false;
		break;
	case 464: tween_to(&black2A, 1, 1.5f, EASE_CUBEINOUT, 0, NULL, NULL); break;
	}
}

static void event(const EventDef *e) {
	if (e->type == EV_LYRICS) {
		strncpy(lyrics, e->s, sizeof(lyrics) - 1);
		lyrics[sizeof(lyrics) - 1] = 0;
	} else if (e->type == EV_CHANGE_CHARACTER) {
		int sl = e->i0 & 1;
		Character *n = !strcmp(e->s, "max") ? &cMax : !strcmp(e->s, "nugget") ? &cNugget :
			!strcmp(e->s, "maxmdp") ? &cMaxMdp : !strcmp(e->s, "nugmdp") ? &cNugMdp : NULL;
		if (n) play_change_character(sl, n);
	}
}

static void like_click(void) {
	likeCount++;
	P.score += 100;
	P.health += 0.015f;
	if (P.health > 2) P.health = 2;
	if (!unlockedGoodEnding && likeCount >= 25) unlockedGoodEnding = true;
	tween_cancel_target(&likeScale);
	tween_num(&likeScale, 0.65f, 0.7f, 0.15f, EASE_CUBEOUT, 0, NULL, NULL);
	tapFlash = 4;
	play_sub_dirty();
}

static void update(float dt) {
	// ui.hx update
	float target = P.health / 2;
	if (target < 0) target = 0;
	healthLerp = lerpf(healthLerp, target, fps_ratio(0.1f, dt));
	if (!MDP) {
		dontZoomYet = false;
		if (areMdpNotes) {
			for (int i = 0; i < 2; i++) {
				play_set_noteskin(i, &noteskin_default);
				P.sl[i].y = P.sl[i].def->y;
			}
			for (int d = 0; d < 4; d++) P.sl[1].x[d] -= 40;
			areMdpNotes = false;
		}
	}
	if (pendingMdpOff) { load_real_stage(); pendingMdpOff = 0; }
	// HELP ME button (FlxButton): touch on the bottom screen
	if (likeOn && likeY > -100 && (kDown & KEY_TOUCH)) {
		if (touch.py >= 40 && touch.py < 40 + 60 && touch.px >= 12 && touch.px < 244) like_click();
	}
	if (tapFlash > 0 && --tapFlash == 0) play_sub_dirty();
	// characters + sprites animation
	float sc = play_step_crochet();
	chr_update(&cMaxMdp, dt, P.songPos, sc);
	chr_update(&cNugMdp, dt, P.songPos, sc);
	chr_update(&cMax, dt, P.songPos, sc);
	chr_update(&cNugget, dt, P.songPos, sc);
	if (runOn) { chr_update(&cMaxRun, dt, 0, 0); chr_update(&cNugRun, dt, 0, 0); }
	if (pibbyOn) chr_update(&cPibby, dt, 0, 0);
	// video
	if (lyricsVisible) {
		if (!video_update(P.songPos - lyricsStart)) {}
	}
}

// onDadHit: health drain
static void dadHit(void) {
	int b = P.curBeat;
	if (b > 70 && b < 386 && P.health > 0.4f) P.health -= 0.019f;
	if (b > 386 && !unlockedGoodEnding && P.health > 0.4f) P.health -= 0.023f;
}

// camera.hx postUpdate
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

// ---------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------
static void drawGameBack(void) {
	if (lyricsVisible) return;           // fullscreen video on camBorder covers camGame
	if (realVisible && realLoaded) {
		play_layer(&shSky, &layer_yt_sky, WHITE, 31);
		play_layer(&shClouds, &layer_yt_clouds, WHITE, 31);
		play_layer(&shCityL, &layer_yt_cityleft, WHITE, 31);
		play_layer(&shCityR, &layer_yt_cityright, WHITE, 31);
		play_layer(&shRoad, &layer_yt_road, WHITE, 31);
		play_layer(&shBuild, &layer_yt_buildings, WHITE, 31);
		play_layer(&shShadowM, &layer_yt_maxshadow, WHITE, 31);
		play_layer(&shShadowN, &layer_yt_nugshadow, WHITE, 31);
	}
	if (mdpSprVisible) play_layer(&shMdp, &layer_yt_mdp, WHITE, 31);
	chr_draw(P.sl[0].chars[0], &P.cam, WHITE);
	chr_draw(P.sl[1].chars[0], &P.cam, WHITE);
}

extern void play_draw_popups(void);

static void drawGameFront(void) {
	if (lyricsVisible) return;
	if (realVisible && realLoaded) {
		play_layer(&shTreeR, &layer_yt_treeright, WHITE, 31);
		play_layer(&shTreeL, &layer_yt_treeleft, WHITE, 31);
	}
	play_draw_popups();
	if (broDiedOn) play_layer(&shBroDied, &layer_brodied, WHITE, 31);
	if (pibbyOn) chr_draw(&cPibby, &P.cam, WHITE);
	if (yellowBGOn) gfx_rect(0, 0, 256, 192, HEXC(0xFFF000), 31);
	if (blueBGOn && blueA > 0.004f) gfx_rect(0, 0, 256, 192, HEXC(0x3E99FF), A31(blueA));
	if (runOn) {
		chr_draw(&cMaxRun, &P.cam, WHITE);
		chr_draw(&cNugRun, &P.cam, WHITE);
	}
}

static void drawBorder(void) {
	if (MDP && !lyricsVisible) {
		float k = KDS;
		// shortGradientTop / Bottom (270x84, scale 2.1 x 1.5 around the center)
		float gx = 640 - 105 - 50 + 135 - 270 * 2.1f / 2, gw = 270 * 2.1f;
		sheet_draw(&shUi, OLBUI_GRADTOP, bx(gx), by(42 - 84 * 1.5f / 2), k, WHITE, 31);
		sheet_draw(&shUi, OLBUI_GRADBOT, bx(gx), by(650 + 42 - 84 * 1.5f / 2), k, WHITE, 31);
		(void)gw;
		// watermark (scale 0.5 around its center, screenCenter X on the unscaled width)
		float wx = (1280 - WATERMARK_W) / 2.0f + WATERMARK_W * 0.25f + WATERMARK_OX * 0.5f;
		sheet_draw(&shWatermark, WATERMARK_MAIN, bx(wx), by(550 + 24 * 0.5f + WATERMARK_OY * 0.5f - 12), k, WHITE, A31(0.32f));
		// leftBar / rightBar
		float bw = ceilf((1280 - 9.0f / 16 * 720) / 2);
		play_border_rect(0, 0, bw, 720, HEXC(0x0F0F0F), 1);
		play_border_rect(1280 - bw, 0, 1280, 720, HEXC(0x0F0F0F), 1);
	}
	if (!lyricsVisible && blackA > 0.004f) gfx_rect(0, 0, 256, 192, 0, A31(blackA));
	if (yellowScreenOn) gfx_rect(0, 0, 256, 192, HEXC(0xFFD900), 31);
	if (logoOn && shLogo.loaded) {
		const LayerDef *ld = logoWhite ? &layer_logo_olbwhite : &layer_logo_olb;
		sheet_draw(&shLogo, 0, bx(ld->x), by(ld->y), KDS, WHITE, 31);
	}
	if (lyrics[0] && save.lyricSubs) {
		// lyricsText: MDP.ttf 50, scale 0.85, y = 630
		Font *f = font_get(FONT_MDP_LYR);
		(void)f;
		font_draw_centered(FONT_MDP_LYR, 128, by(630 + 50 * 0.075f), lyrics, RGB(0xF7, 0xF1, 0xDF), 31);
	}
	if (likeOn && shHelp.loaded) {
		// FlxButton (554x120 hitbox), scaled around its center
		float cx = 1280 / 3.5f + 554 / 2.0f, cy = likeY + 60;
		float w = 554 * likeScale, h = 120 * likeScale;
		sheet_draw(&shHelp, HELPME_MAIN, bx(cx - w / 2), by(cy - h / 2), KDS * likeScale / 0.7f, WHITE, 31);
	}
	if (black2On && black2A > 0.004f) gfx_rect(0, 0, 256, 192, 0, A31(black2A));
}

static void drawHud(void) {
	if (!MDP || !shUi.loaded) return;
	float a = P.camHudAlpha;
	float bxp = 640 - 105, byp = 600;
	// batteryBG
	gfx_rect(hx(bxp), hy(byp), hx(bxp + 210), hy(byp + 100), 0, A31(a * 0xAA / 255.0f));
	// batteryGradient clipped to health
	float w = 210 * healthLerp;
	if (w > 0.5f) {
		float gy = save.downscroll ? byp - 40 : byp;
		sheet_draw_sub(&shUi, OLBUI_BATTGRAD, hx(bxp), hy(gy), hx(bxp + w), hy(gy + 100), 0, 0, w / 210, 1, WHITE, A31(a));
	}
	// batteryBorder (scale 0.7 + updateHitbox)
	sheet_draw(&shUi, OLBUI_BATTBORDER, hx(bxp - 5), hy(byp - 10), P.hudZoomEff * KDS, WHITE, A31(a));
}

static void drawSub(void) {
	char b[48], sc[24];
	if (likeOn) {
		sub_text_centered(FONT_MDP13O, 128, 14, "TAP TO HELP HIM!", RGB(255, 255, 255));
		extern Img16 olb_imgHelp;
		int o = tapFlash > 0 ? 2 : 0;
		sub_blit(&olb_imgHelp, 12 + o, 40 + o);
		snprintf(b, sizeof(b), "%d", likeCount);
		sub_text_centered(FONT_MDP13O, 128, 110, b, RGB(0xF9, 0xCF, 0x51));
	}
	if (!MDP && scoreUiVisible) {
		int idx = healthLerp > 0.75f ? 0 : healthLerp > 0.5f ? 1 : healthLerp > 0.25f ? 2 : 3;
		extern Img16 olb_icons[4];
		sub_blit(&olb_icons[idx], 4, 192 - olb_icons[idx].h - 4);
	}
	if (!MDP && scoreUiVisible) {
		format_score(sc, P.score);
		snprintf(b, sizeof(b), "Score: %s", sc);
		sub_text(FONT_MDP13O, 90, 140, b, RGB(0xF7, 0xF1, 0xDF));
		snprintf(b, sizeof(b), "Misses: %d", P.misses);
		sub_text(FONT_MDP13O, 90, 160, b, RGB(0xF7, 0xF1, 0xDF));
	}
	if (MDP) {
		int w = (int)(120 * healthLerp);
		sub_fill(68, 150, 120, 24, RGB(30, 30, 30));
		sub_fill(68, 150, w, 24, healthLerp > 0.5f ? RGB(0x3C, 0xD0, 0x4A) : healthLerp > 0.25f ? RGB(0xF1, 0xC0, 0x30) : RGB(0xE0, 0x30, 0x30));
		sub_rect(66, 148, 124, 28, RGB(240, 240, 240));
		sub_fill(190, 156, 4, 12, RGB(240, 240, 240));
	}
}

Img16 olb_imgHelp, olb_icons[4];

static bool songEnd(void) {
	if (unlockedGoodEnding) save.olbGood = true;
	else save.olbBad = true;
	if (P.score > save.scoreOlb) { save.scoreOlb = P.score; save.missesOlb = P.misses; }
	save_write();
	if (!g_outroSeen) state_switch(ST_NEXTUP);
	else state_switch(ST_STORY);
	return true;
}

static void gameOver(void) { state_switch(ST_GAMEOVER); }

static void leave(void) {
	Sheet *all[] = { &shSky, &shClouds, &shCityL, &shCityR, &shRoad, &shBuild, &shShadowM, &shShadowN, &shMdp, &shTreeR, &shTreeL,
		&shUi, &shWatermark, &shLogo, &shBroDied, &shHelp };
	for (unsigned i = 0; i < sizeof(all) / sizeof(all[0]); i++) sheet_unload(all[i]);
	Character *cs[] = { &cMaxMdp, &cNugMdp, &cMax, &cNugget, &cMaxRun, &cNugRun, &cPibby };
	for (unsigned i = 0; i < 7; i++) chr_unload(cs[i]);
	img_free(&olb_imgHelp);
	for (int i = 0; i < 4; i++) img_free(&olb_icons[i]);
	shader_tint(1, 1, 1);
}

static void create_wrap(void) {
	create();
	img_load(&olb_imgHelp, "/sub/helpme.b16");
	for (int i = 0; i < 4; i++) {
		char p[32];
		snprintf(p, sizeof(p), "/sub/icon_nug_%d.b16", i);
		img_load(&olb_icons[i], p);
	}
}

static void postUpdate_wrap(float dt) {
	postUpdate(dt);
	// redTint (MULTIPLY 0xff1616 alpha 0.3) emulated on the palettes
	if (redTintOn) shader_tint(1, 1 - 0.3f + 0.3f * 0x16 / 255.0f, 1 - 0.3f + 0.3f * 0x16 / 255.0f);
	else shader_tint(1, 1, 1);
	// onNoteHit: enableCamZooming = !dontZoomYet
	if (dontZoomYet) P.camZooming = false;
}

const SongScript olb_script = {
	.create = create_wrap, .postCreate = postCreate, .songStart = songStart, .update = update,
	.postUpdate = postUpdate_wrap, .beat = beat, .event = event, .drawGameBack = drawGameBack,
	.drawGameFront = drawGameFront, .drawBorder = drawBorder, .drawHud = drawHud, .drawSub = drawSub,
	.songEnd = songEnd, .dadHit = dadHit, .leave = leave, .gameOver = gameOver,
};
