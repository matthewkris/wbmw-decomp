# Reference harness (headless) and play (live window). Build with GCC and GNU make; on the
# owner's machine both come from MSYS2 UCRT64, along with SDL2 for the window.
CC = gcc
CFLAGS ?= -O2 -Wall -Wno-unused-function -Wno-unused-label -Wno-unused-variable
MUSASHI = ../third_party/musashi
OUT = ../work/harness
CORE = core.c bus.c vdp.c png.c hybrid.c $(PORT) $(GENO)
GEN = ../work/gen/gen.c ../work/gen/gen_registry.c
GENO = $(OUT)/gen.o $(OUT)/gen_registry.o
PYTHON ?= python
ROM ?= ../rom/Wonder Boy in Monster World (USA, Europe).md
PORT = ../port/kernel.c ../port/entity.c ../port/anim.c ../port/gfx.c ../port/sprites.c ../port/hitbox.c ../port/map.c ../port/registry.c
CPU = $(OUT)/m68kcpu.o $(OUT)/m68kops.o $(OUT)/softfloat.o
LDFLAGS = -static-libgcc

all: $(OUT)/harness $(OUT)/play

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
$(GEN): ../tools/m68k2c.py ../analysis/translate.json ../port/registry.c ../work/map.json
	$(PYTHON) -I ../tools/m68k2c.py "$(ROM)" ../work/map.json ../work/gen ../analysis/translate.json

# compiled once for both programs: gen.c is megabytes of C
$(OUT)/gen.o: ../work/gen/gen.c ../port/m68k_rt.h ../port/port.h
	$(CC) $(CFLAGS) -I../port -c $< -o $@
$(OUT)/gen_registry.o: ../work/gen/gen_registry.c ../port/m68k_rt.h ../port/port.h
	$(CC) $(CFLAGS) -I../port -c $< -o $@

$(OUT)/harness: main.c $(CORE) md.h ../port/port.h $(CPU)
	$(CC) $(CFLAGS) -I$(MUSASHI) -I../port main.c $(CORE) $(CPU) -o $@ $(LDFLAGS) -lm

# -mconsole: sdl2-config asks for a Windows GUI program, which has no console, so nothing it
# prints (mismatch reports, the report at exit) would be seen
$(OUT)/play: play.c $(CORE) md.h ../port/port.h $(CPU)
	$(CC) $(CFLAGS) -I$(MUSASHI) -I../port $$(sdl2-config --cflags) play.c $(CORE) $(CPU) -o $@ $(LDFLAGS) $$(sdl2-config --libs) -mconsole -lm
	cp "$$(dirname $$(which sdl2-config))/SDL2.dll" $(OUT)/

clean:
	rm -rf $(OUT)
