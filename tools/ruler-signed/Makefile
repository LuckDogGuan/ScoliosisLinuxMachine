CC ?= gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra
all: build/test_signed
build/test_signed: ruler_signed.c ruler_signed.h test_signed.c
	mkdir -p build
	$(CC) $(CFLAGS) ruler_signed.c test_signed.c -lm -o $@
asan: CFLAGS += -g -fsanitize=address,undefined -fno-omit-frame-pointer
asan: build/test_signed_asan
build/test_signed_asan: ruler_signed.c ruler_signed.h test_signed.c
	mkdir -p build
	$(CC) $(CFLAGS) ruler_signed.c test_signed.c -lm -o $@
.PHONY: all asan
