#include <windows.h>
#include <stdio.h>

#include "../common/lprintf.h"
#include "../common/plat.h"
#include "../common/emu.h"
#include "../../pico/pico.h"
#include "version.h"
#include "direct.h"
#include "dsnd.h"
#include "main.h"

static unsigned short screen_buff[320 * 240];
static unsigned char PicoDraw2FB_[(8+320) * (8+240+8)];
unsigned char *PicoDraw2FB = PicoDraw2FB_;

char cpu_clk_name[] = "unused";

void plat_init(void)
{
	g_screen_ptr = (void *)screen_buff;
}

int plat_is_dir(const char *path)
{
	return (GetFileAttributes(path) & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
}

unsigned int plat_get_ticks_ms(void)
{
	return GetTickCount();
}

unsigned int plat_get_ticks_us(void)
{
	// XXX: maybe performance counters?
	return GetTickCount() * 1000;
}

void plat_wait_till_us(unsigned int us)
{
	int msdiff = (int)(us - plat_get_ticks_us()) / 1000;
	if (msdiff > 6)
		Sleep(msdiff - 6);
	while (plat_get_ticks_us() < us)
		;
}

void plat_sleep_ms(int ms)
{
	Sleep(ms);
}

int plat_wait_event(int *fds_hnds, int count, int timeout_ms)
{
	return -1;
}

void pemu_prep_defconfig(void)
{
	memset(&defaultConfig, 0, sizeof(defaultConfig));
	defaultConfig.EmuOpt    = 0x9d | EOPT_RAM_TIMINGS|EOPT_CONFIRM_SAVE|EOPT_EN_CD_LEDS;
	defaultConfig.s_PicoOpt = POPT_EN_STEREO|POPT_EN_FM|POPT_EN_PSG|POPT_EN_Z80 |
				  POPT_EN_MCD_PCM|POPT_EN_MCD_CDDA|POPT_ACC_SPRITES |
				  POPT_EN_32X|POPT_EN_PWM;
	defaultConfig.s_PicoOpt|= POPT_6BTN_PAD; // for xmen proto
	defaultConfig.s_PsndRate = 44100;
	defaultConfig.s_PicoRegion = 0; // auto
	defaultConfig.s_PicoAutoRgnOrder = 0x184; // US, EU, JP
	defaultConfig.s_PicoCDBuffers = 0;
	defaultConfig.Frameskip = 0;
}

static int EmuScanBegin16(unsigned int num)
{
	DrawLineDest = (unsigned short *) g_screen_ptr + g_screen_width * num;

	return 0;
}

void pemu_loop_prep(void)
{
	PicoDrawSetColorFormat(1);
	PicoScanBegin = EmuScanBegin16;
	pemu_sound_start();
}

void pemu_loop_end(void)
{
	pemu_sound_stop();
}

void pemu_forced_frame(int opts)
{
}

void pemu_update_display(const char *fps, const char *notice_msg)
{
	DirectScreen(g_screen_ptr);
	DirectPresent();
}

void plat_video_wait_vsync(void)
{
}

void plat_video_toggle_renderer(int is_next, int force_16bpp, int is_menu)
{
	// this will auto-select SMS/32X renderers
	PicoDrawSetColorFormat(1);
}

void emu_video_mode_change(int start_line, int line_count, int is_32cols)
{
	EmuScreenRect.left = is_32cols ? 32 : 0;
	EmuScreenRect.right = is_32cols ? 256+32 : 320;
	EmuScreenRect.top = start_line;
	EmuScreenRect.bottom = start_line + line_count;

	PostMessage(FrameWnd, WM_COMMAND, 0x20000 | 2000, 0);
}

static int sndbuff[2*44100/50/2 + 4];

static void update_sound(int len)
{
	/* avoid writing audio when lagging behind to prevent audio lag */
	if (PicoSkipFrame != 2)
		DSoundUpdate(sndbuff, (currentConfig.EmuOpt & EOPT_NO_FRMLIMIT) ? 0 : 1);
}

void pemu_sound_start(void)
{
	int ret;

	PsndOut = NULL;
	currentConfig.EmuOpt &= ~EOPT_EXT_FRMLIMIT;

	// prepare sound stuff
	if (currentConfig.EmuOpt & EOPT_EN_SOUND)
	{
		PsndRerate(0);

		ret = DSoundInit(FrameWnd, PsndRate, (PicoOpt & POPT_EN_STEREO) ? 1 : 0, PsndLen);
		if (ret != 0) {
			lprintf("dsound init failed\n");
			return;
		}

		PsndOut = (void *)sndbuff;
		PicoWriteSound = update_sound;
		currentConfig.EmuOpt |= EOPT_EXT_FRMLIMIT;
	}
}

void pemu_sound_stop(void)
{
	DSoundExit();
}

void pemu_sound_wait(void)
{
}

int plat_get_root_dir(char *dst, int len)
{
	int ml;

	ml = GetModuleFileName(NULL, dst, len);
	while (ml > 0 && dst[ml] != '\\')
		ml--;
	if (ml != 0)
		ml++;

	dst[ml] = 0;
	return ml;
}

void plat_status_msg_busy_first(const char *msg)
{
}

void plat_status_msg_busy_next(const char *msg)
{
}

void plat_status_msg_clear(void)
{
}

void plat_video_menu_enter(int is_rom_loaded)
{
}

void plat_video_menu_begin(void)
{
}

void plat_video_menu_end(void)
{
}

void plat_validate_config(void)
{
}

void plat_update_volume(int has_changed, int is_up)
{
}

const char *plat_get_credits(void)
{
	return "PicoDrive v" VERSION " minibeta (c) notaz, 2006-2009\n\n"
		"Credits:\n"
		"fDave: base code of PicoDrive\n"
		"Chui: Fame/C\n"
		"NJ: CZ80\n"
		"MAME devs: YM2612, SN76496 and SH2 cores\n"
		"St�phane Dallongeville: base of Fame/C (C68K), CZ80\n\n"
		"Special thanks (ideas, valuable information and stuff):\n"
		"Charles MacDonald, Eke, Exophase, Haze, Lordus, Nemesis,\n"
		"Pierpaolo Prazzoli, Rokas, Steve Snake, Tasco Deluxe.\n";
}

void plat_debug_cat(char *str)
{
}

// required by pico
int mp3_get_bitrate(FILE *f, int size)
{
	return 128;
}

void mp3_start_play(FILE *f, int pos)
{
}

void mp3_update(int *buffer, int length, int stereo)
{
}

// other
void lprintf(const char *fmt, ...)
{
  char buf[512];
  va_list val;

  va_start(val, fmt);
  vsnprintf(buf, sizeof(buf), fmt, val);
  va_end(val);
  OutputDebugString(buf);
  printf("%s", buf);
}

// fake
int alphasort() { return 0; }
int scandir() { return 0; }

