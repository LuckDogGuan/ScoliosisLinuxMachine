CC ?= gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2 -I../../third_party/cjson
SOURCES = ruler_vision.c reader.c ../../third_party/cjson/cJSON.c
all: build/ruler_reader
build/ruler_reader: $(SOURCES) ruler_vision.h
	mkdir -p build
	$(CC) $(CFLAGS) $(SOURCES) -lm -o $@
arm:
	$(MAKE) CC=/home/guanjx/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-gcc build/ruler_reader_arm
build/ruler_reader_arm: $(SOURCES) ruler_vision.h
	mkdir -p build
	$(CC) $(CFLAGS) $(SOURCES) -lm -o $@
.PHONY: all arm
