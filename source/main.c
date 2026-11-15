// WWCD DS - entry point, state machine, save data
#include "engine.h"
#include <filesystem.h>
#include <fat.h>

u32 kDown, kHeld, kUp;
touchPosition touch;
bool touchDown;
float g_fade = 0;
float g_subFade = 0;
int g_song = SONG_OLB;
bool g_storyMode = true;
bool g_eyeIntroSeen = false;
bool g_outroSeen = false;
bool g_retroMusic = false;
bool g_restartPending = false;
int perf_drops, perf_frames;

Save save;
static int curState = -1, nextState = -1;
static bool fatOk;

static const StateDef states[ST_COUNT] = {
	[ST_INTRO] = { intro_enter, intro_update, intro_draw, intro_leave },
	[ST_MAINMENU] = { mainmenu_enter, mainmenu_update, mainmenu_draw, mainmenu_leave },
	[ST_STORY] = { story_enter, story_update, story_draw, story_leave },
	[ST_BONUS] = { bonus_enter, bonus_update, bonus_draw, bonus_leave },
	[ST_OPTIONS] = { options_enter, options_update, options_draw, options_leave },
	[ST_PLAY] = { play_enter, play_update, play_draw, play_leave },
	[ST_NEXTUP] = { nextup_enter, nextup_update, nextup_draw, nextup_leave },
	[ST_GAMEOVER] = { gameover_enter, gameover_update, gameover_draw, gameover_leave },
	[ST_SOCCERMENU] = { soccermenu_enter, soccermenu_update, soccermenu_draw, soccermenu_leave },
	[ST_SOCCERMODE] = { soccermode_enter, soccermode_update, soccermode_draw, soccermode_leave },
	[ST_SOCCER] = { soccer_enter, soccer_update, soccer_draw, soccer_leave },
};

void *file_load(const char *path, u32 *size) {
	char full[96];
	snprintf(full, sizeof(full), "nitro:%s", path);
	FILE *f = fopen(full, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	void *buf = malloc(n + 4);
	if (buf) fread(buf, 1, n, f);
	fclose(f);
	if (size) *size = n;
	return buf;
}

// ---------------------------------------------------------------------------
// save data (FlxSave "WwcdSave" + FunkinSave highscores + options)
// ---------------------------------------------------------------------------
#define SAVE_PATH "fat:/data/wwcdds.sav"
#define SAVE_MAGIC 0x44435757   // "WWCD"

static void save_defaults(void) {
	memset(&save, 0, sizeof(save));
	save.ghostTapping = true;
	save.lyricSubs = true;
#ifdef TEST_BOTPLAY
	save.botplay = true;
#endif
}

void save_load(void) {
	save_defaults();
	if (!fatOk) return;
	FILE *f = fopen(SAVE_PATH, "rb");
	if (!f) return;
	u32 magic = 0, size = 0;
	if (fread(&magic, 4, 1, f) == 1 && magic == SAVE_MAGIC && fread(&size, 4, 1, f) == 1 && size == sizeof(Save)) {
		Save s;
		if (fread(&s, sizeof(s), 1, f) == 1) save = s;
	}
	fclose(f);
}

void save_write(void) {
	if (!fatOk) return;
	mkdir("fat:/data", 0777);
	FILE *f = fopen(SAVE_PATH, "wb");
	if (!f) return;
	u32 magic = SAVE_MAGIC, size = sizeof(Save);
	fwrite(&magic, 4, 1, f);
	fwrite(&size, 4, 1, f);
	fwrite(&save, sizeof(save), 1, f);
	fclose(f);
}

// CoolUtil.playMenuSong(fadeIn)
void menu_music(bool fadeIn) {
	if (audio_music_playing()) return;
	audio_play_music(g_retroMusic ? "nitro:/snd/menuold.raw" : "nitro:/snd/menu.raw", true, fadeIn ? 0 : 1);
	if (fadeIn) audio_fade_music(0.7f, 4);
}

void format_score(char *out, int score) {
	char b[16];
	snprintf(b, sizeof(b), "%d", score);
	int len = strlen(b);
	int o = 0;
	if (len < 4) { strcpy(out, b); return; }
	for (int i = 0; i < len; i++) {
		int rev = len - 1 - i;
		out[o++] = b[i];
		if (rev > 0 && rev % 3 == 0) out[o++] = ',';
	}
	out[o] = 0;
}

void state_switch(int st) { nextState = st; }

static void do_switch(void) {
	if (curState >= 0) states[curState].leave();
	tween_clear();
	video_close();
	gfx_reset_vram();
	font_unload_all();
	curState = nextState;
	nextState = -1;
	states[curState].enter();
}

static void fatal(const char *msg) {
	consoleDemoInit();
	printf("\n\n  WWCD DS\n\n  %s\n", msg);
	while (pmMainLoop()) swiWaitForVBlank();
	exit(0);
}

int main(int argc, char **argv) {
	defaultExceptionHandler();
	if (!nitroFSInit(NULL)) fatal("NitroFS init failed.\n  Run the .nds with a loader\n  that supports argv/NitroFS.");
	fatOk = fatInitDefault();
	srand(12345);

	gfx_init();
	video_init();
	sub_init();
	audio_init();
	save_load();
#ifdef TEST_BOTPLAY
	save.botplay = true;
#endif

#ifdef START_STATE
	g_song = START_SONG;
	nextState = START_STATE;
#else
	nextState = ST_INTRO;
#endif
	u32 lastTicks = time_ticks();

	while (pmMainLoop()) {
		if (nextState >= 0) {
			do_switch();
			lastTicks = time_ticks();
		}
		scanKeys();
		kDown = keysDown();
		kHeld = keysHeld();
		kUp = keysUp();
		touchDown = (kHeld & KEY_TOUCH) != 0;
		if (touchDown) touchRead(&touch);

		u32 now = time_ticks();
		float dt = (now - lastTicks) / (float)TICK_FREQ;
		lastTicks = now;
		if (curState == ST_PLAY) { perf_frames++; if (dt > 1.5f / 59.83f) perf_drops += (int)(dt * 59.83f + 0.5f) - 1; }
		if (dt > 0.1f) dt = 0.1f;
		if (dt < 0.001f) dt = 1.0f / 60;

		states[curState].update(dt);
		tween_update(dt);

		gfx_frame_begin();
		states[curState].draw();
		gfx_frame_end();

		swiWaitForVBlank();
		gfx_vblank();
		video_vblank();
		sub_present();
		int b = (int)(clampf(g_fade, 0, 1) * 16);
		int bs = (int)(clampf(g_fade > g_subFade ? g_fade : g_subFade, 0, 1) * 16);
		REG_MASTER_BRIGHT = b ? ((2 << 14) | b) : 0;
		REG_MASTER_BRIGHT_SUB = bs ? ((2 << 14) | bs) : 0;
		audio_update();
	}
	return 0;
}
