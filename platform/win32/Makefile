# settings
CROSS=i586-mingw32msvc-

#use_musashi = 1
use_fame = 1
#use_mz80 = 1

-include Makefile.local

CC = $(CROSS)gcc
CXX = $(CROSS)g++
LD = $(CROSS)ld
STRIP = $(CROSS)strip

DEFINES = _UNZIP_SUPPORT IN_VK
CFLAGS += -O2 -Wall -falign-functions=2 -ffast-math
CFLAGS += -I../.. -I. -I../../zlib/ -Idirectx/include/
LDFLAGS += -mwindows -L. -Ldirectx/lib/ -lgdi32 -lcomdlg32 -lddraw -ldsound -ldxguid

# frontend
OBJS += main.o plat.o direct.o dsnd.o in_vk.o

# common
OBJS += platform/common/emu.o platform/common/menu.o \
	platform/common/config.o platform/common/fonts.o platform/common/readpng.o \
	platform/common/input.o

# Pico
OBJS += pico/area.o pico/cart.o pico/memory.o pico/pico.o pico/sek.o \
	pico/videoport.o pico/draw2.o pico/draw.o pico/z80if.o pico/patch.o \
	pico/mode4.o pico/sms.o pico/misc.o pico/eeprom.o pico/debug.o
# Pico - CD
OBJS += pico/cd/pico.o pico/cd/memory.o pico/cd/sek.o pico/cd/LC89510.o \
	pico/cd/cd_sys.o pico/cd/cd_file.o pico/cd/cue.o pico/cd/gfx_cd.o \
	pico/cd/area.o pico/cd/misc.o pico/cd/pcm.o pico/cd/buffering.o
# Pico - 32X
OBJS += pico/32x/32x.o pico/32x/memory.o pico/32x/draw.o pico/32x/pwm.o
# Pico - Pico
OBJS += pico/pico/pico.o pico/pico/memory.o pico/pico/xpcm.o
# Pico - sound
OBJS += pico/sound/sound.o pico/sound/sn76496.o pico/sound/ym2612.o pico/sound/mix.o
# Pico - carthw
OBJS += pico/carthw/carthw.o pico/carthw/svp/svp.o pico/carthw/svp/memory.o \
	pico/carthw/svp/ssp16.o pico/carthw/svp/compiler.o
# zlib
OBJS += zlib/gzio.o zlib/inffast.o zlib/inflate.o zlib/inftrees.o zlib/trees.o \
	zlib/deflate.o zlib/crc32.o zlib/adler32.o zlib/zutil.o zlib/compress.o zlib/uncompr.o
# unzip
OBJS += unzip/unzip.o unzip/unzip_stream.o
# CPU cores
ifeq "$(use_musashi)" "1"
DEFINES += EMU_M68K
OBJS += cpu/musashi/m68kops.o cpu/musashi/m68kcpu.o
#OBJS += cpu/musashi/m68kdasm.o
endif
ifeq "$(use_fame)" "1"
DEFINES += EMU_F68K
OBJS += cpu/fame/famec.o
endif
# z80
ifeq "$(use_mz80)" "1"
DEFINES += _USE_MZ80
OBJS += cpu/mz80/mz80.o
else
DEFINES += _USE_CZ80
OBJS += cpu/cz80/cz80.o
endif
# sh2
OBJS += cpu/sh2mame/sh2pico.o
# misc
ifeq "$(use_fame)" "1"
ifeq "$(use_musashi)" "1"
OBJS += pico/debugCPU.o
endif
endif

CFLAGS += $(addprefix -D,$(DEFINES))
CXXFLAGS = $(CFLAGS)

vpath %.c = ../..

DIRS = platform platform/gp2x platform/common pico pico/cd pico/pico pico/sound pico/carthw/svp \
	pico/32x zlib unzip cpu cpu/musashi cpu/fame cpu/mz80 cpu/cz80 cpu/sh2mame

TARGET = PicoDrive.exe

all: mkdirs $(TARGET)
clean: tidy
	@$(RM) $(TARGET)
tidy:
	$(RM) $(OBJS) $(TARGET).map
	rm -rf $(DIRS)

$(TARGET) : $(OBJS)
	@echo ">>>" $@
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -lm -lpng -Wl,-Map=$(TARGET).map -o $@
	$(STRIP) $@

mkdirs:
	@mkdir -p $(DIRS)

include ../common/revision.mak

pico/carthw/svp/compiler.o : ../../pico/carthw/svp/gen_arm.c
pico/pico.o pico/cd/pico.o : ../../pico/pico_cmn.c ../../pico/pico_int.h
pico/memory.o pico/cd/memory.o : ../../pico/pico_int.h ../../pico/memory.h

../../cpu/musashi/m68kops.c :
	@make -C ../../cpu/musashi

cpu/mz80/mz80.o : ../../cpu/mz80/mz80.asm
	@echo $@
	@nasm -f elf $< -o $@

../../cpu/mz80/mz80.asm :
	@make -C ../../cpu/mz80/

.c.o:
	@echo ">>>" $<
	$(CC) $(CFLAGS) -c $< -o $@
.s.o:
	@echo ">>>" $<
	$(CC) $(CFLAGS) -c $< -o $@


cpu/fame/famec.o : ../../cpu/fame/famec.c ../../cpu/fame/famec_opcodes.h
	@echo ">>>" $<
	$(CC) $(CFLAGS) -Wno-unused -c $< -o $@

