CC = /home/guanjx/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-gcc
TARGET = instrument_app
CFLAGS += -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2
CFLAGS += -DINSTRUMENT_BUILD='"$(shell git rev-parse --short=12 HEAD)"'
CFLAGS += -Iinclude -Ithird_party/cjson -Ithird_party/lwbtn/include

# Application policy -> middleware services -> Linux/Luckfox platform adapters
SOURCES = \
	src/app/app_controller.c \
	src/app/policy.c \
	src/middleware/legacy.c \
	src/platform/process.c \
	src/platform/trace.c \
	src/platform/luckfox_gpio.c \
	src/platform/luckfox_camera.c \
	src/platform/camera_profiles.c \
	src/platform/diagnostics_health.c \
	src/app/runtime_v3.c \
	src/middleware/persistence.c \
	src/platform/clock.c \
	src/middleware/buttons.c \
	third_party/lwbtn/lwbtn.c \
	src/config/config.c \
	src/platform/luckfox_wifi.c \
	src/middleware/upload.c \
	src/middleware/discover.c \
	src/storage/storage.c \
	src/platform/luckfox_audio.c \
	src/middleware/capture.c \
	src/platform/luckfox_battery.c \
	src/platform/luckfox_button.c \
	src/platform/luckfox_light.c \
	src/platform/light_test.c \
	third_party/cjson/cJSON.c

all: $(TARGET)

$(TARGET): $(SOURCES) include/app.h include/config.h
	$(CC) $(CFLAGS) $(SOURCES) -o $@

check-host:
	$(MAKE) CC=gcc TARGET=instrument_app_host
	./instrument_app_host --check-config config.example.json
	python3 tests/test_upload.py
	python3 tests/test_pending_upload.py
	python3 tests/test_capture_discard.py
	gcc $(CFLAGS) tests/test_buttons.c src/middleware/buttons.c src/platform/luckfox_gpio.c third_party/lwbtn/lwbtn.c -o tests/.buttons_check
	./tests/.buttons_check

.PHONY: all check-host

# Separate in-process RKiPC adapter; never linked into the application.
mux-adapter:
	$(CC) -shared -fPIC -Wall -Wextra -Werror -O2 src/platform/luckfox_mux_adapter.c -pthread -o luckfox_mux_adapter.so
