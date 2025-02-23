#include "../pico_int.h"
#include "../sound/ym2612.h"

struct Pico32x Pico32x;
SH2 sh2s[2];

static void sh2_irq_cb(int id, int level)
{
  // diagnostic for now
  elprintf(EL_32X, "%csh2 ack %d @ %08x", id ? 's' : 'm', level, sh2_pc(id));
}

void p32x_update_irls(void)
{
  int irqs, mlvl = 0, slvl = 0;

  // msh2
  irqs = (Pico32x.sh2irqs | Pico32x.sh2irqi[0]) & ((Pico32x.sh2irq_mask[0] << 3) | P32XI_VRES);
  while ((irqs >>= 1))
    mlvl++;
  mlvl *= 2;

  // ssh2
  irqs = (Pico32x.sh2irqs | Pico32x.sh2irqi[1]) & ((Pico32x.sh2irq_mask[1] << 3) | P32XI_VRES);
  while ((irqs >>= 1))
    slvl++;
  slvl *= 2;

  elprintf(EL_32X, "update_irls: m %d, s %d", mlvl, slvl);
  sh2_irl_irq(&msh2, mlvl);
  sh2_irl_irq(&ssh2, slvl);
  mlvl = mlvl ? 1 : 0;
  slvl = slvl ? 1 : 0;
  p32x_poll_event(mlvl | (slvl << 1), 0);
}

void Pico32xStartup(void)
{
  elprintf(EL_STATUS|EL_32X, "32X startup");

  PicoAHW |= PAHW_32X;
  sh2_init(&msh2, 0);
  msh2.irq_callback = sh2_irq_cb;
  sh2_init(&ssh2, 1);
  ssh2.irq_callback = sh2_irq_cb;

  PicoMemSetup32x();

  if (!Pico.m.pal)
    Pico32x.vdp_regs[0] |= P32XV_nPAL;

  PREG8(Pico32xMem->sh2_peri_regs[0], 4) =
  PREG8(Pico32xMem->sh2_peri_regs[1], 4) = 0x84; // SCI SSR

  emu_32x_startup();
}

#define HWSWAP(x) (((x) << 16) | ((x) >> 16))
void p32x_reset_sh2s(void)
{
  elprintf(EL_32X, "sh2 reset");

  sh2_reset(&msh2);
  sh2_reset(&ssh2);

  // if we don't have BIOS set, perform it's work here.
  // MSH2
  if (p32x_bios_m == NULL) {
    unsigned int idl_src, idl_dst, idl_size; // initial data load
    unsigned int vbr;

    // initial data
    idl_src = HWSWAP(*(unsigned int *)(Pico.rom + 0x3d4)) & ~0xf0000000;
    idl_dst = HWSWAP(*(unsigned int *)(Pico.rom + 0x3d8)) & ~0xf0000000;
    idl_size= HWSWAP(*(unsigned int *)(Pico.rom + 0x3dc));
    if (idl_size > Pico.romsize || idl_src + idl_size > Pico.romsize ||
        idl_size > 0x40000 || idl_dst + idl_size > 0x40000 || (idl_src & 3) || (idl_dst & 3)) {
      elprintf(EL_STATUS|EL_ANOMALY, "32x: invalid initial data ptrs: %06x -> %06x, %06x",
        idl_src, idl_dst, idl_size);
    }
    else
      memcpy(Pico32xMem->sdram + idl_dst, Pico.rom + idl_src, idl_size);

    // GBR/VBR
    vbr = HWSWAP(*(unsigned int *)(Pico.rom + 0x3e8));
    sh2_set_gbr(0, 0x20004000);
    sh2_set_vbr(0, vbr);

    // checksum and M_OK
    Pico32x.regs[0x28 / 2] = *(unsigned short *)(Pico.rom + 0x18e);
    // program will set M_OK
  }

  // SSH2
  if (p32x_bios_s == NULL) {
    unsigned int vbr;

    // GBR/VBR
    vbr = HWSWAP(*(unsigned int *)(Pico.rom + 0x3ec));
    sh2_set_gbr(1, 0x20004000);
    sh2_set_vbr(1, vbr);
    // program will set S_OK
  }
}

void Pico32xInit(void)
{
}

void PicoPower32x(void)
{
  memset(&Pico32x, 0, sizeof(Pico32x));

  Pico32x.regs[0] = P32XS_REN|P32XS_nRES; // verified
  Pico32x.vdp_regs[0x0a/2] = P32XV_VBLK|P32XV_HBLK|P32XV_PEN;
  Pico32x.sh2_regs[0] = P32XS2_ADEN;
}

void PicoUnload32x(void)
{
  if (Pico32xMem != NULL)
    free(Pico32xMem);
  Pico32xMem = NULL;

  PicoAHW &= ~PAHW_32X;
}

void PicoReset32x(void)
{
  if (PicoAHW & PAHW_32X) {
    Pico32x.sh2irqs |= P32XI_VRES;
    p32x_update_irls();
    p32x_poll_event(3, 0);
  }
}

static void p32x_start_blank(void)
{
  // enter vblank
  Pico32x.vdp_regs[0x0a/2] |= P32XV_VBLK|P32XV_PEN;

  // FB swap waits until vblank
  if ((Pico32x.vdp_regs[0x0a/2] ^ Pico32x.pending_fb) & P32XV_FS) {
    Pico32x.vdp_regs[0x0a/2] &= ~P32XV_FS;
    Pico32x.vdp_regs[0x0a/2] |= Pico32x.pending_fb;
    Pico32xSwapDRAM(Pico32x.pending_fb ^ 1);
  }

  Pico32x.sh2irqs |= P32XI_VINT;
  p32x_update_irls();
  p32x_poll_event(3, 1);
}

static __inline void run_m68k(int cyc)
{
#if defined(EMU_C68K)
  PicoCpuCM68k.cycles = cyc;
  CycloneRun(&PicoCpuCM68k);
  SekCycleCnt += cyc - PicoCpuCM68k.cycles;
#elif defined(EMU_M68K)
  SekCycleCnt += m68k_execute(cyc);
#elif defined(EMU_F68K)
  SekCycleCnt += fm68k_emulate(cyc+1, 0, 0);
#endif
}

// ~1463.8, but due to cache misses and slow mem
// it's much lower than that
//#define SH2_LINE_CYCLES 735
#define CYCLES_M68K2SH2(x) ((x) * 6 / 4)

#define PICO_32X
#define CPUS_RUN_SIMPLE(m68k_cycles,s68k_cycles) \
{ \
  int slice; \
  SekCycleAim += m68k_cycles; \
  while (SekCycleCnt < SekCycleAim) { \
    slice = SekCycleCnt; \
    run_m68k(SekCycleAim - SekCycleCnt); \
    if (!(Pico32x.regs[0] & P32XS_nRES)) \
      continue; /* SH2s reseting */ \
    slice = SekCycleCnt - slice; /* real count from 68k */ \
    if (SekCycleCnt < SekCycleAim) \
      elprintf(EL_32X, "slice %d", slice); \
    if (!(Pico32x.emu_flags & (P32XF_SSH2POLL|P32XF_SSH2VPOLL))) \
      sh2_execute(&ssh2, CYCLES_M68K2SH2(slice)); \
    if (!(Pico32x.emu_flags & (P32XF_MSH2POLL|P32XF_MSH2VPOLL))) \
      sh2_execute(&msh2, CYCLES_M68K2SH2(slice)); \
  } \
}

#define STEP_68K 24
#define CPUS_RUN_LOCKSTEP(m68k_cycles,s68k_cycles) \
{ \
  int i; \
  for (i = 0; i <= (m68k_cycles) - STEP_68K; i += STEP_68K) { \
    run_m68k(STEP_68K); \
    if (!(Pico32x.emu_flags & (P32XF_MSH2POLL|P32XF_MSH2VPOLL))) \
      sh2_execute(&msh2, CYCLES_M68K2SH2(STEP_68K)); \
    if (!(Pico32x.emu_flags & (P32XF_SSH2POLL|P32XF_SSH2VPOLL))) \
      sh2_execute(&ssh2, CYCLES_M68K2SH2(STEP_68K)); \
  } \
  /* last step */ \
  i = (m68k_cycles) - i; \
  run_m68k(i); \
  if (!(Pico32x.emu_flags & (P32XF_MSH2POLL|P32XF_MSH2VPOLL))) \
    sh2_execute(&msh2, CYCLES_M68K2SH2(i)); \
  if (!(Pico32x.emu_flags & (P32XF_SSH2POLL|P32XF_SSH2VPOLL))) \
    sh2_execute(&ssh2, CYCLES_M68K2SH2(i)); \
}

#define CPUS_RUN CPUS_RUN_SIMPLE
//#define CPUS_RUN CPUS_RUN_LOCKSTEP

#include "../pico_cmn.c"

void PicoFrame32x(void)
{
  pwm_frame_smp_cnt = 0;

  Pico32x.vdp_regs[0x0a/2] &= ~P32XV_VBLK; // get out of vblank
  if ((Pico32x.vdp_regs[0] & P32XV_Mx) != 0) // no forced blanking
    Pico32x.vdp_regs[0x0a/2] &= ~P32XV_PEN; // no palette access

  p32x_poll_event(3, 1);

  PicoFrameStart();
  PicoFrameHints();
  elprintf(EL_32X, "poll: %02x", Pico32x.emu_flags);
}

