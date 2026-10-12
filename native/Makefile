# The native build: the game as C, no 68000 emulator. Build with GCC and GNU make (MSYS2
# UCRT64 on the owner's machine, with SDL2). Needs work/map.json (make at the repo root).
CC = gcc
CFLAGS ?= -O2 -Wall -Wno-unused-function -Wno-unused-label -Wno-unused-variable
PYTHON ?= python
ROM ?= ../rom/Wonder Boy in Monster World (USA, Europe).md
OUT = ../work/native
GEN = ../work/gen_native/gen.c ../work/gen_native/gen_registry.c
PORT = ../port/kernel.c ../port/entity.c ../port/anim.c ../port/gfx.c ../port/sprites.c ../port/hitbox.c ../port/map.c ../port/movement.c ../port/player.c ../port/magic.c ../port/hud.c ../port/effects.c ../port/items.c ../port/maths.c ../port/enemies.c ../port/world.c ../port/coro.c ../port/task.c ../port/registry.c
MACHINE = ../harness/bus.c ../harness/vdp.c ../harness/png.c ../harness/wav.c ../harness/framelog.c ../harness/sound.c ../harness/audio.c
CXX = g++
YMFM = ../third_party/ymfm
YMO = $(OUT)/ym2612.o $(OUT)/ymfm_opn.o $(OUT)/ymfm_adpcm.o $(OUT)/ymfm_ssg.o

all: $(OUT)/wbmw

# the whole game, translated from the reset vector (analysis/native.json); work/cycles.bin comes
# from the harness build (make in harness/)
$(GEN): ../tools/m68k2c.py ../analysis/native.json ../port/registry.c ../work/map.json ../analysis/hints.json ../work/cycles.bin
	$(PYTHON) -I ../tools/m68k2c.py "$(ROM)" ../work/map.json ../work/gen_native ../analysis/native.json

$(OUT)/gen.o: ../work/gen_native/gen.c ../port/m68k_rt.h ../port/port.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I../port -c $< -o $@
$(OUT)/gen_registry.o: ../work/gen_native/gen_registry.c ../port/m68k_rt.h ../port/port.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I../port -c $< -o $@

# the YM2612: ymfm (C++) behind harness/ym2612.cpp
$(OUT)/ym2612.o: ../harness/ym2612.cpp
	@mkdir -p $(OUT)
	$(CXX) -O2 -std=c++14 -I$(YMFM) -c $< -o $@
$(OUT)/ymfm_%.o: $(YMFM)/ymfm_%.cpp
	@mkdir -p $(OUT)
	$(CXX) -O2 -std=c++14 -w -I$(YMFM) -c $< -o $@

# -mconsole: sdl2-config asks for a Windows GUI program, which has no console for messages
$(OUT)/wbmw: wbmw.c $(MACHINE) $(PORT) $(OUT)/gen.o $(OUT)/gen_registry.o $(YMO) ../harness/md.h
	$(CC) $(CFLAGS) -I../port -I../harness $$(sdl2-config --cflags) wbmw.c $(MACHINE) $(PORT) $(OUT)/gen.o $(OUT)/gen_registry.o $(YMO) \
		-o $@ -static-libgcc $$(sdl2-config --libs) -mconsole -static-libstdc++ -lstdc++ -lm
	cp "$$(dirname $$(which sdl2-config))/SDL2.dll" $(OUT)/

clean:
	rm -rf $(OUT)
