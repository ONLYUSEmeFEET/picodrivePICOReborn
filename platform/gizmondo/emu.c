#include <windows.h>
#include <string.h>

#include <sys/stat.h>  // mkdir
#include <sys/types.h>

#include "kgsdk/Framework.h"
#include "kgsdk/Framework2D.h"
#include "kgsdk/FrameworkAudio.h"
#include "../common/emu.h"
#include "../common/lprintf.h"
#include "../common/arm_utils.h"
#include "emu.h"
#include "menu.h"
#include "giz.h"
#include "asm_utils.h"

#include <Pico/PicoInt.h>

#ifdef BENCHMARK
#define OSD_FPS_X 220
#else
#define OSD_FPS_X 260
#endif

// main 300K gfx-related buffer. Used by menu and renderers.
unsigned char gfx_buffer[321*240*2*2];
char romFileName[MAX_PATH];
int engineState;

unsigned char *PicoDraw2FB = gfx_buffer;  // temporary buffer for alt renderer ( (8+320)*(8+240+8) )
int reset_timing = 0;

static DWORD noticeMsgTime = 0;
static short *snd_cbuff = NULL;
static int snd_cbuf_samples = 0, snd_all_samples = 0;


static void blit(const char *fps, const char *notice);
static void clearArea(int full);

void emu_noticeMsgUpdated(void)
{
	noticeMsgTime = GetTickCount();
}

void emu_getMainDir(char *dst, int len)
{
	if (len > 0) *dst = 0;
}

static void emu_msg_cb(const char *msg)
{
	if (giz_screen != NULL) Framework2D_UnlockBuffer();
	giz_screen = Framework2D_LockBuffer(1);

	memset32((int *)((char *)giz_screen + 321*232*2), 0, 321*8*2/4);
	emu_textOut16(4, 232, msg);
	noticeMsgTime = GetTickCount() - 2000;

	/* assumption: emu_msg_cb gets called only when something slow is about to happen */
	reset_timing = 1;

	Framework2D_UnlockBuffer();
	giz_screen = Framework2D_LockBuffer((currentConfig.EmuOpt&0x8000) ? 0 : 1);
}

void emu_stateCb(const char *str)
{
	if (giz_screen != NULL) Framework2D_UnlockBuffer();
	giz_screen = Framework2D_LockBuffer(1);

	clearArea(0);
	blit("", str);

	Framework2D_UnlockBuffer();
	giz_screen = NULL;

	Sleep(0); /* yield the CPU, the system may need it */
}

static void emu_msg_tray_open(void)
{
	strcpy(noticeMsg, "CD tray opened");
	noticeMsgTime = GetTickCount();
}


void emu_Init(void)
{
	// make dirs for saves, cfgs, etc.
	mkdir("mds", 0777);
	mkdir("srm", 0777);
	mkdir("brm", 0777);
	mkdir("cfg", 0777);

	PicoInit();
	PicoMessage = emu_msg_cb;
	PicoMCDopenTray = emu_msg_tray_open;
	PicoMCDcloseTray = menu_loop_tray;
}

void emu_Deinit(void)
{
	// save SRAM
	if ((currentConfig.EmuOpt & 1) && SRam.changed) {
		emu_SaveLoadGame(0, 1);
		SRam.changed = 0;
	}

	if (!(currentConfig.EmuOpt & 0x20))
		config_writelrom(PicoConfigFile);

	PicoExit();
}

void emu_setDefaultConfig(void)
{
	memset(&currentConfig, 0, sizeof(currentConfig));
	currentConfig.lastRomFile[0] = 0;
	currentConfig.EmuOpt  = 0x1f | 0x680; // | confirm_save, cd_leds, 16bit rend
	currentConfig.PicoOpt = 0x07 | 0xc00; // | cd_pcm, cd_cdda
	currentConfig.PsndRate = 22050;
	currentConfig.PicoRegion = 0; // auto
	currentConfig.PicoAutoRgnOrder = 0x184; // US, EU, JP
	currentConfig.Frameskip = -1; // auto
	currentConfig.volume = 50;
	currentConfig.KeyBinds[ 2] = 1<<0; // SACB RLDU
	currentConfig.KeyBinds[ 3] = 1<<1;
	currentConfig.KeyBinds[ 0] = 1<<2;
	currentConfig.KeyBinds[ 1] = 1<<3;
	currentConfig.KeyBinds[ 5] = 1<<4;
	currentConfig.KeyBinds[ 6] = 1<<5;
	currentConfig.KeyBinds[ 7] = 1<<6;
	currentConfig.KeyBinds[ 4] = 1<<7;
	currentConfig.KeyBinds[13] = 1<<26; // switch rend
	currentConfig.KeyBinds[ 8] = 1<<27; // save state
	currentConfig.KeyBinds[ 9] = 1<<28; // load state
	currentConfig.KeyBinds[12] = 1<<29; // vol up
	currentConfig.KeyBinds[11] = 1<<30; // vol down
	currentConfig.PicoCDBuffers = 0;
	currentConfig.scaling = 0;
}


static int EmuScanBegin16(unsigned int num)
{
	if (!(Pico.video.reg[1]&8)) num += 8;
	DrawLineDest = (unsigned short *) giz_screen + 321 * num;

	if ((currentConfig.EmuOpt&0x4000) && (num&1) == 0) // (Pico.m.frame_count&1))
		return 1; // skip next line

	return 0;
}

static int EmuScanBegin8(unsigned int num)
{
	// draw like the fast renderer
	if (!(Pico.video.reg[1]&8)) num += 8;
	HighCol = gfx_buffer + 328 * num;

	return 0;
}

static void osd_text(int x, int y, const char *text)
{
	int len = strlen(text) * 8 / 2;
	int *p, h;
	for (h = 0; h < 8; h++) {
		p = (int *) ((unsigned short *) giz_screen+x+321*(y+h));
		p = (int *) ((int)p & ~3); // align
		memset32(p, 0, len);
	}
	emu_textOut16(x, y, text);
}

/*
void log1(void *p1, void *p2)
{
	lprintf("%p %p %p\n", p1, p2, DrawLineDest);
}
*/

static void cd_leds(void)
{
	static int old_reg = 0;
	unsigned int col_g, col_r, *p;

	if (!((Pico_mcd->s68k_regs[0] ^ old_reg) & 3)) return; // no change
	old_reg = Pico_mcd->s68k_regs[0];

	p = (unsigned int *)((short *)giz_screen + 321*2+4+2);
	col_g = (old_reg & 2) ? 0x06000600 : 0;
	col_r = (old_reg & 1) ? 0xc000c000 : 0;
	*p++ = col_g; *p++ = col_g; p+=2; *p++ = col_r; *p++ = col_r; p += 321/2 - 12/2 + 1;
	*p++ = col_g; p+=3; *p++ = col_r; p += 321/2 - 10/2;
	*p++ = col_g; *p++ = col_g; p+=2; *p++ = col_r; *p++ = col_r;
}


static short localPal[0x100];

static void blit(const char *fps, const char *notice)
{
	int emu_opt = currentConfig.EmuOpt;

	if (PicoOpt&0x10)
	{
		int lines_flags = 224;
		// 8bit fast renderer
		if (Pico.m.dirtyPal) {
			Pico.m.dirtyPal = 0;
			vidConvCpyRGB565(localPal, Pico.cram, 0x40);
		}
		if (!(Pico.video.reg[12]&1)) lines_flags|=0x10000;
		if (currentConfig.EmuOpt&0x4000)
			lines_flags|=0x40000; // (Pico.m.frame_count&1)?0x20000:0x40000;
		vidCpy8to16((unsigned short *)giz_screen+321*8, PicoDraw2FB+328*8, localPal, lines_flags);
	}
	else if (!(emu_opt&0x80))
	{
		int lines_flags;
		// 8bit accurate renderer
		if (Pico.m.dirtyPal) {
			Pico.m.dirtyPal = 0;
			vidConvCpyRGB565(localPal, Pico.cram, 0x40);
			if (Pico.video.reg[0xC]&8) { // shadow/hilight mode
				//vidConvCpyRGB32sh(localPal+0x40, Pico.cram, 0x40);
				//vidConvCpyRGB32hi(localPal+0x80, Pico.cram, 0x40); // TODO?
				blockcpy(localPal+0xc0, localPal+0x40, 0x40*2);
				localPal[0xc0] = 0x0600;
				localPal[0xd0] = 0xc000;
				localPal[0xe0] = 0x0000; // reserved pixels for OSD
				localPal[0xf0] = 0xffff;
			}
			/* no support
			else if (rendstatus & 0x20) { // mid-frame palette changes
				vidConvCpyRGB565(localPal+0x40, HighPal, 0x40);
				vidConvCpyRGB565(localPal+0x80, HighPal+0x40, 0x40);
			} */
		}
		lines_flags = (Pico.video.reg[1]&8) ? 240 : 224;
		if (!(Pico.video.reg[12]&1)) lines_flags|=0x10000;
		if (currentConfig.EmuOpt&0x4000)
			lines_flags|=0x40000; // (Pico.m.frame_count&1)?0x20000:0x40000;
		vidCpy8to16((unsigned short *)giz_screen+321*8, PicoDraw2FB+328*8, localPal, lines_flags);
	}

	if (notice || (emu_opt & 2)) {
		int h = 232;
		if (notice)      osd_text(4, h, notice);
		if (emu_opt & 2) osd_text(OSD_FPS_X, h, fps);
	}

	if ((emu_opt & 0x400) && (PicoAHW & PAHW_MCD))
		cd_leds();
}

// clears whole screen or just the notice area (in all buffers)
static void clearArea(int full)
{
	if (giz_screen == NULL)
		giz_screen = Framework2D_LockBuffer(1);
	if (full) memset32(giz_screen, 0, 320*240*2/4);
	else      memset32((int *)((char *)giz_screen + 321*232*2), 0, 321*8*2/4);

	if (currentConfig.EmuOpt&0x8000) {
		Framework2D_UnlockBuffer();
		giz_screen = Framework2D_LockBuffer(0);
		if (full) memset32(giz_screen, 0, 320*240*2/4);
		else      memset32((int *)((char *)giz_screen + 321*232*2), 0, 321*8*2/4);
	}
}

static void vidResetMode(void)
{
	giz_screen = Framework2D_LockBuffer(1);

	if (PicoOpt&0x10) {
	} else if (currentConfig.EmuOpt&0x80) {
		PicoDrawSetColorFormat(1);
		PicoScanBegin = EmuScanBegin16;
	} else {
		PicoDrawSetColorFormat(-1);
		PicoScanBegin = EmuScanBegin8;
	}
	if ((PicoOpt&0x10) || !(currentConfig.EmuOpt&0x80)) {
		// setup pal for 8-bit modes
		localPal[0xc0] = 0x0600;
		localPal[0xd0] = 0xc000;
		localPal[0xe0] = 0x0000; // reserved pixels for OSD
		localPal[0xf0] = 0xffff;
	}
	Pico.m.dirtyPal = 1;

	memset32(giz_screen, 0, 321*240*2/4);
	if (currentConfig.EmuOpt&0x8000) {
		Framework2D_UnlockBuffer();
		giz_screen = Framework2D_LockBuffer(0);
		memset32(giz_screen, 0, 321*240*2/4);
	}
	Framework2D_UnlockBuffer();
	giz_screen = NULL;
}

/*
#include <stdarg.h>
static void stdbg(const char *fmt, ...)
{
	static int cnt = 0;
	va_list vl;

	sprintf(noticeMsg, "%x ", cnt++);
	va_start(vl, fmt);
	vsnprintf(noticeMsg+strlen(noticeMsg), sizeof(noticeMsg)-strlen(noticeMsg), fmt, vl);
	va_end(vl);

	noticeMsgTime = GetTickCount();
}
*/

static void updateSound(int len)
{
	if (PicoOpt&8) len<<=1;

	snd_all_samples += len;
	PsndOut += len;
	if (PsndOut - snd_cbuff >= snd_cbuf_samples)
	{
		//if (PsndOut - snd_cbuff != snd_cbuf_samples)
		//	stdbg("snd diff is %i, not %i", PsndOut - snd_cbuff, snd_cbuf_samples);
		PsndOut = snd_cbuff;
	}
}


static void SkipFrame(void)
{
	PicoSkipFrame=1;
	PicoFrame();
	PicoSkipFrame=0;
}

/* forced frame to front buffer */
void emu_forcedFrame(void)
{
	int po_old = PicoOpt;
	int eo_old = currentConfig.EmuOpt;

	PicoOpt &= ~0x0010;
	PicoOpt |=  0x4080; // soft_scale | acc_sprites
	currentConfig.EmuOpt |= 0x80;

	if (giz_screen == NULL)
		giz_screen = Framework2D_LockBuffer(1);

	PicoDrawSetColorFormat(1);
	PicoScanBegin = EmuScanBegin16;
	Pico.m.dirtyPal = 1;
	PicoFrameDrawOnly();

	Framework2D_UnlockBuffer();
	giz_screen = NULL;

	PicoOpt = po_old;
	currentConfig.EmuOpt = eo_old;
}


static void RunEvents(unsigned int which)
{
	if (which & 0x1800) // save or load (but not both)
	{
		int do_it = 1;

		if (PsndOut != NULL)
			FrameworkAudio_SetPause(1);
		if (giz_screen == NULL)
			giz_screen = Framework2D_LockBuffer(1);
		if ( emu_checkSaveFile(state_slot) &&
				(( (which & 0x1000) && (currentConfig.EmuOpt & 0x800)) || // load
				 (!(which & 0x1000) && (currentConfig.EmuOpt & 0x200))) ) // save
		{
			int keys;
			blit("", (which & 0x1000) ? "LOAD STATE? (PLAY=yes, STOP=no)" : "OVERWRITE SAVE? (PLAY=yes, STOP=no)");
			while( !((keys = Framework_PollGetButtons()) & (BTN_PLAY|BTN_STOP)) )
				Sleep(50);
			if (keys & BTN_STOP) do_it = 0;
			while(  ((keys = Framework_PollGetButtons()) & (BTN_PLAY|BTN_STOP)) ) // wait for release
				Sleep(50);
			clearArea(0);
		}

		if (do_it)
		{
			osd_text(4, 232, (which & 0x1000) ? "LOADING GAME" : "SAVING GAME");
			PicoStateProgressCB = emu_stateCb;
			emu_SaveLoadGame((which & 0x1000) >> 12, 0);
			PicoStateProgressCB = NULL;
			Sleep(0);
		}

		if (PsndOut != NULL)
			FrameworkAudio_SetPause(0);
		reset_timing = 1;
	}
	if (which & 0x0400) // switch renderer
	{
		if (PicoOpt&0x10) { PicoOpt&=~0x10; currentConfig.EmuOpt |=  0x80; }
		else              { PicoOpt|= 0x10; currentConfig.EmuOpt &= ~0x80; }

		vidResetMode();

		if (PicoOpt&0x10) {
			strcpy(noticeMsg, " 8bit fast renderer");
		} else if (currentConfig.EmuOpt&0x80) {
			strcpy(noticeMsg, "16bit accurate renderer");
		} else {
			strcpy(noticeMsg, " 8bit accurate renderer");
		}

		noticeMsgTime = GetTickCount();
	}
	if (which & 0x0300)
	{
		if(which&0x0200) {
			state_slot -= 1;
			if(state_slot < 0) state_slot = 9;
		} else {
			state_slot += 1;
			if(state_slot > 9) state_slot = 0;
		}
		sprintf(noticeMsg, "SAVE SLOT %i [%s]", state_slot, emu_checkSaveFile(state_slot) ? "USED" : "FREE");
		noticeMsgTime = GetTickCount();
	}
}

static void updateKeys(void)
{
	unsigned int keys, allActions[2] = { 0, 0 }, events;
	static unsigned int prevEvents = 0;
	int i;

	keys = Framework_PollGetButtons();
	if (keys & BTN_HOME)
		engineState = PGS_Menu;

	keys &= CONFIGURABLE_KEYS;

	for (i = 0; i < 32; i++)
	{
		if (keys & (1 << i))
		{
			int pl, acts = currentConfig.KeyBinds[i];
			if (!acts) continue;
			pl = (acts >> 16) & 1;
			if (kb_combo_keys & (1 << i))
			{
				int u = i+1, acts_c = acts & kb_combo_acts;
				// let's try to find the other one
				if (acts_c) {
					for (; u < 32; u++)
						if ( (keys & (1 << u)) && (currentConfig.KeyBinds[u] & acts_c) ) {
							allActions[pl] |= acts_c & currentConfig.KeyBinds[u];
							keys &= ~((1 << i) | (1 << u));
							break;
						}
				}
				// add non-combo actions if combo ones were not found
				if (!acts_c || u == 32)
					allActions[pl] |= acts & ~kb_combo_acts;
			} else {
				allActions[pl] |= acts;
			}
		}
	}

	PicoPad[0] = (unsigned short) allActions[0];
	PicoPad[1] = (unsigned short) allActions[1];

	events = (allActions[0] | allActions[1]) >> 16;

	// volume is treated in special way and triggered every frame
	if ((events & 0x6000) && PsndOut != NULL)
	{
		int vol = currentConfig.volume;
		if (events & 0x2000) {
			if (vol < 100) vol++;
		} else {
			if (vol >   0) vol--;
		}
		FrameworkAudio_SetVolume(vol, vol);
		sprintf(noticeMsg, "VOL: %02i ", vol);
		noticeMsgTime = GetTickCount();
		currentConfig.volume = vol;
	}

	events &= ~prevEvents;
	if (events) RunEvents(events);
	if (movie_data) emu_updateMovie();

	prevEvents = (allActions[0] | allActions[1]) >> 16;
}


static void simpleWait(DWORD until)
{
	DWORD tval;
	int diff;

	tval = GetTickCount();
	diff = (int)until - (int)tval;
	if (diff >= 2)
		Sleep(diff - 1);

	while ((tval = GetTickCount()) < until && until - tval < 512) // some simple overflow detection
		spend_cycles(1024*2);
}

void emu_Loop(void)
{
	static int PsndRate_old = 0, PicoOpt_old = 0, pal_old = 0;
	char fpsbuff[24]; // fps count c string
	DWORD tval, tval_prev = 0, tval_thissec = 0; // timing
	int frames_done = 0, frames_shown = 0, oldmodes = 0, sec_ms = 1000;
	int target_fps, target_frametime, lim_time, tval_diff, i;
	char *notice = NULL;

	lprintf("entered emu_Loop()\n");

	fpsbuff[0] = 0;

	// make sure we are in correct mode
	vidResetMode();
	if (currentConfig.scaling) PicoOpt|=0x4000;
	else PicoOpt&=~0x4000;
	Pico.m.dirtyPal = 1;
	oldmodes = ((Pico.video.reg[12]&1)<<2) ^ 0xc;
	emu_findKeyBindCombos();

	// pal/ntsc might have changed, reset related stuff
	target_fps = Pico.m.pal ? 50 : 60;
	target_frametime = Pico.m.pal ? (1000<<8)/50 : (1000<<8)/60+1;
	reset_timing = 1;

	// prepare CD buffer
	if (PicoAHW & PAHW_MCD) PicoCDBufferInit();

	// prepare sound stuff
	PsndOut = NULL;
	if (currentConfig.EmuOpt & 4)
	{
		int ret, snd_excess_add, stereo;
		if (PsndRate != PsndRate_old || (PicoOpt&0x0b) != (PicoOpt_old&0x0b) || Pico.m.pal != pal_old) {
			PsndRerate(Pico.m.frame_count ? 1 : 0);
		}
		stereo=(PicoOpt&8)>>3;
		snd_excess_add = ((PsndRate - PsndLen*target_fps)<<16) / target_fps;
		snd_cbuf_samples = (PsndRate<<stereo) * 16 / target_fps;
		lprintf("starting audio: %i len: %i (ex: %04x) stereo: %i, pal: %i\n",
			PsndRate, PsndLen, snd_excess_add, stereo, Pico.m.pal);
		ret = FrameworkAudio_Init(PsndRate, snd_cbuf_samples, stereo);
		if (ret != 0) {
			lprintf("FrameworkAudio_Init() failed: %i\n", ret);
			sprintf(noticeMsg, "sound init failed (%i), snd disabled", ret);
			noticeMsgTime = GetTickCount();
			currentConfig.EmuOpt &= ~4;
		} else {
			FrameworkAudio_SetVolume(currentConfig.volume, currentConfig.volume);
			PicoWriteSound = updateSound;
			snd_cbuff = FrameworkAudio_56448Buffer();
			PsndOut = snd_cbuff + snd_cbuf_samples / 2; // start writing at the middle
			snd_all_samples = 0;
			PsndRate_old = PsndRate;
			PicoOpt_old  = PicoOpt;
			pal_old = Pico.m.pal;
		}
	}

	// loop?
	while (engineState == PGS_Running)
	{
		int modes;

		tval = GetTickCount();
		if (reset_timing || tval < tval_prev) {
			//stdbg("timing reset");
			reset_timing = 0;
			tval_thissec = tval;
			frames_shown = frames_done = 0;
		}

		// show notice message?
		if (noticeMsgTime) {
			static int noticeMsgSum;
			if (tval - noticeMsgTime > 2000) { // > 2.0 sec
				noticeMsgTime = 0;
				clearArea(0);
				notice = 0;
			} else {
				int sum = noticeMsg[0]+noticeMsg[1]+noticeMsg[2];
				if (sum != noticeMsgSum) { clearArea(0); noticeMsgSum = sum; }
				notice = noticeMsg;
			}
		}

		// check for mode changes
		modes = ((Pico.video.reg[12]&1)<<2)|(Pico.video.reg[1]&8);
		if (modes != oldmodes) {
			oldmodes = modes;
			clearArea(1);
		}

		// second passed?
		if (tval - tval_thissec >= sec_ms)
		{
#ifdef BENCHMARK
			static int bench = 0, bench_fps = 0, bench_fps_s = 0, bfp = 0, bf[4];
			if(++bench == 10) {
				bench = 0;
				bench_fps_s = bench_fps;
				bf[bfp++ & 3] = bench_fps;
				bench_fps = 0;
			}
			bench_fps += frames_shown;
			sprintf(fpsbuff, "%02i/%02i/%02i", frames_shown, bench_fps_s, (bf[0]+bf[1]+bf[2]+bf[3])>>2);
#else
			if(currentConfig.EmuOpt & 2)
				sprintf(fpsbuff, "%02i/%02i", frames_shown, frames_done);
#endif
			//tval_thissec += 1000;
			tval_thissec += sec_ms;

			if (PsndOut != NULL)
			{
				/* some code which tries to sync things to audio clock, the dirty way */
				static int audio_skew_prev = 0;
				int audio_skew, adj, co = 9, shift = 7;
				audio_skew = snd_all_samples*2 - FrameworkAudio_BufferPos();
				if (PsndRate == 22050) co = 10;
				if (PsndRate  > 22050) co = 11;
				if (PicoOpt&8) shift++;
				if (audio_skew < 0) {
					adj = -((-audio_skew) >> shift);
					if (audio_skew > -(6<<co)) adj>>=1;
					if (audio_skew > -(4<<co)) adj>>=1;
					if (audio_skew > -(2<<co)) adj>>=1;
					if (audio_skew > audio_skew_prev) adj>>=2; // going up already
				} else {
					adj = audio_skew >> shift;
					if (audio_skew < (6<<co)) adj>>=1;
					if (audio_skew < (4<<co)) adj>>=1;
					if (audio_skew < (2<<co)) adj>>=1;
					if (audio_skew < audio_skew_prev) adj>>=2;
				}
				audio_skew_prev = audio_skew;
				target_frametime += adj;
				sec_ms = (target_frametime * target_fps) >> 8;
				//stdbg("%i %i %i", audio_skew, adj, sec_ms);
				frames_done = frames_shown = 0;
			}
			else if (currentConfig.Frameskip < 0) {
				frames_done  -= target_fps; if (frames_done  < 0) frames_done  = 0;
				frames_shown -= target_fps; if (frames_shown < 0) frames_shown = 0;
				if (frames_shown > frames_done) frames_shown = frames_done;
			} else {
				frames_done = frames_shown = 0;
			}
		}
#ifdef PFRAMES
		sprintf(fpsbuff, "%i", Pico.m.frame_count);
#endif

		tval_prev = tval;
		lim_time = (frames_done+1) * target_frametime;
		if (currentConfig.Frameskip >= 0) // frameskip enabled
		{
			for (i = 0; i < currentConfig.Frameskip; i++) {
				updateKeys();
				SkipFrame(); frames_done++;
				if (PsndOut) { // do framelimitting if sound is enabled
					int tval_diff;
					tval = GetTickCount();
					tval_diff = (int)(tval - tval_thissec) << 8;
					if (tval_diff < lim_time) // we are too fast
						simpleWait(tval + ((lim_time - tval_diff)>>8));
				}
				lim_time += target_frametime;
			}
		}
		else // auto frameskip
		{
			int tval_diff;
			tval = GetTickCount();
			tval_diff = (int)(tval - tval_thissec) << 8;
			if (tval_diff > lim_time)
			{
				// no time left for this frame - skip
				if (tval_diff - lim_time >= (300<<8)) {
					/* something caused a slowdown for us (disk access? cache flush?)
					 * try to recover by resetting timing... */
					reset_timing = 1;
					continue;
				}
				updateKeys();
				SkipFrame(); frames_done++;
				continue;
			}
		}

		updateKeys();

		if (currentConfig.EmuOpt&0x80)
			/* be sure correct framebuffer is locked */
			giz_screen = Framework2D_LockBuffer((currentConfig.EmuOpt&0x8000) ? 0 : 1);

		PicoFrame();

		if (giz_screen == NULL)
			giz_screen = Framework2D_LockBuffer((currentConfig.EmuOpt&0x8000) ? 0 : 1);

		blit(fpsbuff, notice);

		if (giz_screen != NULL) {
			Framework2D_UnlockBuffer();
			giz_screen = NULL;
		}

		if (currentConfig.EmuOpt&0x2000)
			Framework2D_WaitVSync();

		if (currentConfig.EmuOpt&0x8000)
			Framework2D_Flip();

		// check time
		tval = GetTickCount();
		tval_diff = (int)(tval - tval_thissec) << 8;

		if (currentConfig.Frameskip < 0 && tval_diff - lim_time >= (300<<8)) // slowdown detection
			reset_timing = 1;
		else if (PsndOut != NULL || currentConfig.Frameskip < 0)
		{
			// sleep if we are still too fast
			if (tval_diff < lim_time)
			{
				// we are too fast
				simpleWait(tval + ((lim_time - tval_diff) >> 8));
			}
		}

		frames_done++; frames_shown++;
	}


	if (PicoAHW & PAHW_MCD) PicoCDBufferFree();

	if (PsndOut != NULL) {
		PsndOut = snd_cbuff = NULL;
		FrameworkAudio_Close();
	}

	// save SRAM
	if ((currentConfig.EmuOpt & 1) && SRam.changed) {
		emu_stateCb("Writing SRAM/BRAM..");
		emu_SaveLoadGame(0, 1);
		SRam.changed = 0;
	}
}


void emu_ResetGame(void)
{
	PicoReset();
	reset_timing = 1;
}

