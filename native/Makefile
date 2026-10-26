# The native build: the game as C, no 68000 emulator. Build with GCC and GNU make (MSYS2
# UCRT64 on the owner's machine, with SDL2). All of the game is the hand-written C of port/ and
# machine.c: no translated code, so nothing from work/ is needed to build it.
CC = gcc
CFLAGS ?= -O2 -Wall -Wno-unused-function -Wno-unused-label -Wno-unused-variable
OUT = ../work/native
PORT = ../port/kernel.c ../port/entity.c ../port/anim.c ../port/gfx.c ../port/sprites.c ../port/hitbox.c ../port/map.c ../port/movement.c ../port/player.c ../port/magic.c ../port/hud.c ../port/status.c ../port/message.c ../port/title.c ../port/rooms.c ../port/vblank.c ../port/enemylib.c ../port/objhelp.c ../port/enemygfx.c ../port/kernhelp.c ../port/effects.c ../port/items.c ../port/maths.c ../port/enemies.c ../port/spawns.c ../port/objects.c ../port/people.c ../port/handlers.c ../port/actions.c ../port/world.c ../port/coro.c ../port/task.c ../port/registry.c ../port/sounddrv.c
MACHINE = ../harness/bus.c ../harness/vdp.c ../harness/png.c ../harness/wav.c ../harness/framelog.c ../harness/sound.c ../harness/audio.c
CXX = g++
YMFM = ../third_party/ymfm
YMO = $(OUT)/ym2612.o $(OUT)/ymfm_adpcm.o $(OUT)/ymfm_ssg.o   # (ymfm_opn.cpp is compiled inside ym2612.cpp)

all: $(OUT)/wbmw pack

# the game's data from your ROM, extracted and packed into assets/game.pak, which the game plays
# from (docs/EXTRACT.md): made when missing or out of date; skipped, with a message, without a ROM
# in rom/ or without Python 3 (the game then reads the ROM)
.PHONY: pack clean
pack:
	@sh ../tools/makepack.sh

# the YM2612: ymfm (C++) behind harness/ym2612.cpp
$(OUT)/ym2612.o: ../harness/ym2612.cpp $(YMFM)/ymfm_opn.cpp
	@mkdir -p $(OUT)
	$(CXX) -O2 -std=c++14 -w -I$(YMFM) -c $< -o $@
$(OUT)/ymfm_%.o: $(YMFM)/ymfm_%.cpp
	@mkdir -p $(OUT)
	$(CXX) -O2 -std=c++14 -w -I$(YMFM) -c $< -o $@

# Windows (MSYS2): -mconsole, as sdl2-config asks for a GUI program, which has no console for
# messages; SDL2.dll copied next to the program
ifeq ($(OS),Windows_NT)
WINFLAGS = -mconsole
COPY_DLL = cp "$$(dirname $$(which sdl2-config))/SDL2.dll" $(OUT)/
endif
$(OUT)/wbmw: wbmw.c machine.c $(MACHINE) $(PORT) $(YMO) ../harness/md.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -I../port -I../harness $$(sdl2-config --cflags) wbmw.c machine.c $(MACHINE) $(PORT) $(YMO) \
		-o $@ -static-libgcc $$(sdl2-config --libs) $(WINFLAGS) -static-libstdc++ -lstdc++ -lm
	$(COPY_DLL)

clean:
	rm -rf $(OUT)
