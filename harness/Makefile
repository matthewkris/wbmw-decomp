# Reference harness (headless) and play (live window). Build with GCC and GNU make; on the
# owner's machine both come from MSYS2 UCRT64, along with SDL2 for the window.
CC = gcc
CFLAGS ?= -O2 -Wall -Wno-unused-function -Wno-unused-label -Wno-unused-variable
MUSASHI = ../third_party/musashi
OUT = ../work/harness
CORE = core.c bus.c vdp.c png.c wav.c framelog.c sound.c hybrid.c $(PORT) $(GENO) $(YMO)
GEN = ../work/gen/gen.c ../work/gen/gen_registry.c
GENO = $(OUT)/gen.o $(OUT)/gen_registry.o
PYTHON ?= python
ROM ?= ../rom/Wonder Boy in Monster World (USA, Europe).md
PORT = ../port/kernel.c ../port/entity.c ../port/anim.c ../port/gfx.c ../port/sprites.c ../port/hitbox.c ../port/map.c ../port/movement.c ../port/player.c ../port/magic.c ../port/hud.c ../port/effects.c ../port/items.c ../port/maths.c ../port/enemies.c ../port/spawns.c ../port/objects.c ../port/world.c ../port/coro.c ../port/task.c ../port/registry.c
CPU = $(OUT)/m68kcpu.o $(OUT)/m68kops.o $(OUT)/softfloat.o
CXX = g++
YMFM = ../third_party/ymfm
YMO = $(OUT)/ym2612.o $(OUT)/ymfm_opn.o $(OUT)/ymfm_adpcm.o $(OUT)/ymfm_ssg.o
LDFLAGS = -static-libgcc

all: $(OUT)/harness $(OUT)/play ../work/cycles.bin

# Musashi's 68000 cycle table, for the native build's optional timing (tools/m68k2c.py)
../work/cycles.bin: cycles_dump.c $(CPU)
	$(CC) -O1 -I$(MUSASHI) -I$(OUT) cycles_dump.c $(CPU) -o $(OUT)/cycles_dump -lm
	$(OUT)/cycles_dump $@

$(OUT)/m68kmake: $(MUSASHI)/m68kmake.c
	@mkdir -p $(OUT)
	$(CC) -O1 -o $@ $<

$(OUT)/m68kops.c: $(OUT)/m68kmake $(MUSASHI)/m68k_in.c
	$(OUT)/m68kmake $(OUT) $(MUSASHI)/m68k_in.c

$(OUT)/m68kcpu.o: $(MUSASHI)/m68kcpu.c $(MUSASHI)/m68kconf.h $(OUT)/m68kops.c
	$(CC) $(CFLAGS) -w -I$(MUSASHI) -I$(OUT) -c $< -o $@
$(OUT)/m68kops.o: $(OUT)/m68kops.c $(MUSASHI)/m68kconf.h
	$(CC) $(CFLAGS) -w -I$(MUSASHI) -I$(OUT) -c $< -o $@
$(OUT)/softfloat.o: $(MUSASHI)/softfloat/softfloat.c
	$(CC) $(CFLAGS) -w -I$(MUSASHI) -c $< -o $@

# the translated routines (tools/m68k2c.py): roots, yield and exits from analysis/translate.json; needs work/map.json
$(GEN): ../tools/m68k2c.py ../analysis/translate.json $(PORT) ../work/map.json
	$(PYTHON) -I ../tools/m68k2c.py "$(ROM)" ../work/map.json ../work/gen ../analysis/translate.json

# compiled once for both programs: gen.c is megabytes of C
$(OUT)/gen.o: ../work/gen/gen.c ../port/m68k_rt.h ../port/port.h
	$(CC) $(CFLAGS) -I../port -c $< -o $@
$(OUT)/gen_registry.o: ../work/gen/gen_registry.c ../port/m68k_rt.h ../port/port.h
	$(CC) $(CFLAGS) -I../port -c $< -o $@

# the YM2612: ymfm (C++, third_party/ymfm) behind a C interface
$(OUT)/ym2612.o: ym2612.cpp
	@mkdir -p $(OUT)
	$(CXX) -O2 -std=c++14 -I$(YMFM) -c $< -o $@
$(OUT)/ymfm_%.o: $(YMFM)/ymfm_%.cpp
	@mkdir -p $(OUT)
	$(CXX) -O2 -std=c++14 -w -I$(YMFM) -c $< -o $@

$(OUT)/harness: main.c $(CORE) md.h ../port/port.h $(CPU)
	$(CC) $(CFLAGS) -I$(MUSASHI) -I../port main.c $(CORE) $(CPU) -o $@ $(LDFLAGS) -static-libstdc++ -lstdc++ -lm

# -mconsole: sdl2-config asks for a Windows GUI program, which has no console, so nothing it
# prints (mismatch reports, the report at exit) would be seen
$(OUT)/play: play.c audio.c $(CORE) md.h ../port/port.h $(CPU)
	$(CC) $(CFLAGS) -I$(MUSASHI) -I../port $$(sdl2-config --cflags) play.c audio.c $(CORE) $(CPU) -o $@ $(LDFLAGS) $$(sdl2-config --libs) -mconsole -static-libstdc++ -lstdc++ -lm
	cp "$$(dirname $$(which sdl2-config))/SDL2.dll" $(OUT)/

clean:
	rm -rf $(OUT)
