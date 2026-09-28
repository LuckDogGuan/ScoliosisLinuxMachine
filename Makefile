CC = /home/guanjx/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-gcc
TARGET = instrument_app
CFLAGS += -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2 -Ithird_party/cjson
SOURCES = app_controller.c config.c wifi.c storage.c audio.c upload.c capture.c battery.c button.c light.c third_party/cjson/cJSON.c

all: $(TARGET)

$(TARGET): $(SOURCES) app.h
	$(CC) $(CFLAGS) $(SOURCES) -o $@

check-host:
	$(MAKE) CC=gcc TARGET=instrument_app_host
	./instrument_app_host --check-config config.example.json
	python3 tests/test_upload.py

.PHONY: all check-host
