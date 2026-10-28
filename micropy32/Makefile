# Makefile for freestanding 32-bit MicroPython on Aarambh OS

CC = gcc
OBJCOPY = objcopy

EMBED = ../micropython-master/examples/embedding/micropython_embed
UZLIB = ../micropython-master/lib/uzlib

CFLAGS = -m32 -march=i686 -Os -ffreestanding -fno-pic -fno-stack-protector -fomit-frame-pointer -DNDEBUG
CFLAGS += -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable
CFLAGS += -I. -I$(EMBED) -I$(EMBED)/port -I$(UZLIB) -I../micropython-master

SRCS = entry.c libc.c auth.c sha256.c pci.c driver_e1000.c driver_rtl8139.c driver_wifi.c net_driver.c net_stack.c net_config.c net_gui.c http.c git.c graphics.c mouse.c wm.c theme.c login.c app_notepad.c app_paint.c app_filemgr.c app_snake.c app_settings.c sh_runner.c deb_tool.c desktop.c vfs.c elf_loader.c rpm_tool.c pe_tool.c sysinfo.c
SRCS += $(UZLIB)/tinflate.c $(UZLIB)/header.c $(UZLIB)/adler32.c $(UZLIB)/crc32.c
SRCS += $(EMBED)/port/embed_util.c
SRCS += $(EMBED)/shared/runtime/gchelper_generic.c
SRCS += $(wildcard $(EMBED)/py/*.c)

OBJS = $(patsubst %.c,build/%.o,$(notdir $(SRCS)))

all: micropy32.bin

build:
	mkdir -p build

build/entry.o: entry.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/libc.o: libc.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/auth.o: auth.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/sha256.o: sha256.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/pci.o: pci.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/driver_e1000.o: driver_e1000.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/driver_rtl8139.o: driver_rtl8139.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/driver_wifi.o: driver_wifi.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/net_driver.o: net_driver.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/net_stack.o: net_stack.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/net_config.o: net_config.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/net_gui.o: net_gui.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/http.o: http.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/git.o: git.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/graphics.o: graphics.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/mouse.o: mouse.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/desktop.o: desktop.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/wm.o: wm.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/theme.o: theme.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/login.o: login.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/app_notepad.o: app_notepad.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/app_paint.o: app_paint.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/app_filemgr.o: app_filemgr.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/app_snake.o: app_snake.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/app_settings.o: app_settings.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/sh_runner.o: sh_runner.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/deb_tool.o: deb_tool.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/vfs.o: vfs.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/elf_loader.o: elf_loader.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/rpm_tool.o: rpm_tool.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/pe_tool.o: pe_tool.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/sysinfo.o: sysinfo.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/tinflate.o: $(UZLIB)/tinflate.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/header.o: $(UZLIB)/header.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/adler32.o: $(UZLIB)/adler32.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/crc32.o: $(UZLIB)/crc32.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/embed_util.o: $(EMBED)/port/embed_util.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/gchelper_generic.o: $(EMBED)/shared/runtime/gchelper_generic.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: $(EMBED)/py/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

micropy32.exe: $(OBJS) linker.ld
	$(CC) -m32 -nostdlib -Wl,-T,linker.ld -Wl,-s -o $@ $(OBJS) -lgcc

micropy32.bin: micropy32.exe
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf build micropy32.exe micropy32.bin

.PHONY: all clean
