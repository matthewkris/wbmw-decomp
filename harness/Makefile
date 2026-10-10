# Reference harness. Build with GCC (MSYS2 UCRT64 on the owner's machine) and GNU make.
CC = gcc
CFLAGS ?= -O2 -Wall -Wno-unused-function
MUSASHI = ../third_party/musashi
OUT = ../work/harness

all: $(OUT)/harness

$(OUT)/m68kmake: $(MUSASHI)/m68kmake.c
	@mkdir -p $(OUT)
	$(CC) -O1 -o $@ $<

$(OUT)/m68kops.c: $(OUT)/m68kmake $(MUSASHI)/m68k_in.c
	$(OUT)/m68kmake $(OUT) $(MUSASHI)/m68k_in.c

$(OUT)/harness: main.c bus.c vdp.c png.c md.h $(OUT)/m68kops.c $(MUSASHI)/m68kcpu.c
	$(CC) $(CFLAGS) -w -I$(MUSASHI) -I$(OUT) -c $(MUSASHI)/m68kcpu.c -o $(OUT)/m68kcpu.o
	$(CC) $(CFLAGS) -w -I$(MUSASHI) -I$(OUT) -c $(OUT)/m68kops.c -o $(OUT)/m68kops.o
	$(CC) $(CFLAGS) -I$(MUSASHI) -I$(OUT) -c $(MUSASHI)/softfloat/softfloat.c -o $(OUT)/softfloat.o -w
	$(CC) $(CFLAGS) -I$(MUSASHI) main.c bus.c vdp.c png.c $(OUT)/m68kcpu.o $(OUT)/m68kops.o $(OUT)/softfloat.o -o $@ -lm

clean:
	rm -rf $(OUT)
