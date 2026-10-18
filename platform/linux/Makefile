LVGL_DIR ?= ../lvgl
PROJECT_ROOT := ../..
CUSTOM_DIR ?= $(PROJECT_ROOT)/custom
BUILD_DIR ?= build-armhf
# Keep dependency targets identical for build-armhf.sh (absolute BUILD_DIR)
# and direct make invocations, so lv_conf.h changes rebuild LVGL as well.
BUILD_DIR := $(abspath $(BUILD_DIR))
TARGET := $(BUILD_DIR)/bin/hachimi_desktop

CC ?= arm-linux-gnueabihf-gcc
CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter -std=gnu99
HACHIMI_USE_NEON ?= 1
HACHIMI_USE_PXP ?= 1
CFLAGS += -DHACHIMI_USE_NEON=$(HACHIMI_USE_NEON) -DHACHIMI_USE_PXP=$(HACHIMI_USE_PXP)
ifeq ($(HACHIMI_USE_NEON),1)
CFLAGS += -mfpu=neon
endif
CFLAGS += -I$(CURDIR)/pxp/include
CFLAGS += -I$(LVGL_DIR) -I$(LVGL_DIR)/src -I$(PROJECT_ROOT) -I$(CUSTOM_DIR) -DLV_CONF_PATH=\"$(CURDIR)/lv_conf.h\"
LDFLAGS += -lm -pthread -ldl

LVGL_SOURCES := $(shell find $(LVGL_DIR)/src -type f -name '*.c')
LVGL_OBJECTS := $(patsubst $(LVGL_DIR)/src/%.c,$(BUILD_DIR)/lvgl/%.o,$(LVGL_SOURCES))
# Preserve module paths in object names to avoid collisions such as services/time.c.
RUNTIME_SOURCES := $(addprefix $(PROJECT_ROOT)/,$(shell sed -e '/^[[:space:]]*\#/d' -e '/^[[:space:]]*$$/d' $(PROJECT_ROOT)/cmake/hachimi_runtime_sources.list))
RUNTIME_SOURCES += $(PROJECT_ROOT)/platform/linux/led_backend.c
APP_OBJECTS := $(patsubst $(PROJECT_ROOT)/%.c,$(BUILD_DIR)/runtime/%.o,$(RUNTIME_SOURCES))
PXP_OBJECTS :=
ifeq ($(HACHIMI_USE_PXP),1)
PXP_OBJECTS := $(BUILD_DIR)/pxp/pxp_engine.o $(BUILD_DIR)/pxp/pxp_display.o
endif
FB_TRANSITION_OBJECT := $(BUILD_DIR)/fb_transition.o
OBJECTS := $(LVGL_OBJECTS) $(APP_OBJECTS) $(BUILD_DIR)/main.o $(BUILD_DIR)/service_job.o $(PXP_OBJECTS) $(FB_TRANSITION_OBJECT)

.PHONY: all assets smoke buzzer-smoke pxp-smoke transition-smoke fb-handoff-smoke maps-smoke demo-apps-smoke clean
all: $(TARGET) assets

SERVICE_OBJECTS := $(filter $(BUILD_DIR)/runtime/services/%.o,$(APP_OBJECTS)) $(BUILD_DIR)/runtime/platform/linux/led_backend.o
smoke: $(BUILD_DIR)/bin/services_smoke $(BUILD_DIR)/bin/runtime_smoke $(BUILD_DIR)/bin/clock_smoke $(BUILD_DIR)/bin/calendar_smoke
buzzer-smoke: $(BUILD_DIR)/bin/buzzer_smoke
pxp-smoke: $(BUILD_DIR)/bin/pxp_smoke
transition-smoke: $(BUILD_DIR)/bin/transition_smoke

fb-handoff-smoke: $(BUILD_DIR)/bin/fb_transition_handoff_smoke

maps-smoke: $(BUILD_DIR)/bin/maps_smoke

demo-apps-smoke: $(BUILD_DIR)/bin/messages_mail_smoke

.PHONY: weather-smoke
weather-smoke: $(BUILD_DIR)/bin/weather_smoke

$(BUILD_DIR)/bin/weather_smoke: $(BUILD_DIR)/runtime/tests/weather_smoke.o $(APP_OBJECTS) $(LVGL_OBJECTS) $(BUILD_DIR)/service_job.o
	@mkdir -p $(dir $@)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/bin/messages_mail_smoke: $(BUILD_DIR)/runtime/tests/messages_mail_smoke.o $(APP_OBJECTS) $(LVGL_OBJECTS) $(BUILD_DIR)/service_job.o
	@mkdir -p $(dir $@)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/bin/maps_smoke: $(BUILD_DIR)/runtime/tests/maps_smoke.o $(APP_OBJECTS) $(LVGL_OBJECTS) $(BUILD_DIR)/service_job.o
	@mkdir -p $(dir $@)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/bin/fb_transition_handoff_smoke: $(BUILD_DIR)/runtime/tests/fb_transition_handoff_smoke.o $(APP_OBJECTS) $(LVGL_OBJECTS) $(BUILD_DIR)/service_job.o
	@mkdir -p $(dir $@)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/bin/transition_smoke: $(BUILD_DIR)/runtime/tests/transition_smoke.o $(APP_OBJECTS) $(LVGL_OBJECTS) $(BUILD_DIR)/service_job.o
	@mkdir -p $(dir $@)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/bin/pxp_smoke: $(BUILD_DIR)/runtime/tests/pxp_smoke.o $(BUILD_DIR)/pxp/pxp_engine.o
	@mkdir -p $(dir $@)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/bin/buzzer_smoke: $(BUILD_DIR)/runtime/tests/buzzer_smoke.o $(BUILD_DIR)/runtime/services/alert.o
	@mkdir -p $(dir $@)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/bin/services_smoke: $(BUILD_DIR)/runtime/tests/services_smoke.o $(SERVICE_OBJECTS) $(BUILD_DIR)/service_job.o
	@mkdir -p $(dir $@)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/bin/runtime_smoke: $(BUILD_DIR)/runtime/tests/runtime_smoke.o $(APP_OBJECTS) $(LVGL_OBJECTS) $(BUILD_DIR)/service_job.o
	@mkdir -p $(dir $@)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/bin/clock_smoke: $(BUILD_DIR)/runtime/tests/clock_smoke.o $(APP_OBJECTS) $(LVGL_OBJECTS) $(BUILD_DIR)/service_job.o
	@mkdir -p $(dir $@)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/bin/calendar_smoke: $(BUILD_DIR)/runtime/tests/calendar_smoke.o $(APP_OBJECTS) $(LVGL_OBJECTS) $(BUILD_DIR)/service_job.o
	@mkdir -p $(dir $@)
	$(CC) $^ $(LDFLAGS) -o $@

$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD_DIR)/lvgl/%.o: $(LVGL_DIR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/runtime/%.o: $(PROJECT_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

assets:
	@mkdir -p $(BUILD_DIR)/bin/assets
	@cp -a $(CUSTOM_DIR)/assets/ios $(BUILD_DIR)/bin/assets/
	@cp -a $(CUSTOM_DIR)/assets/maps $(BUILD_DIR)/bin/assets/

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJECTS:.o=.d)
-include $(wildcard $(BUILD_DIR)/runtime/tests/*.d)

.PHONY: music-smoke
music-smoke: $(BUILD_DIR)/bin/music_smoke
$(BUILD_DIR)/bin/music_smoke: $(BUILD_DIR)/runtime/tests/music_smoke.o $(APP_OBJECTS) $(LVGL_OBJECTS) $(BUILD_DIR)/service_job.o
	@mkdir -p $(dir $@)
	$(CC) $^ $(LDFLAGS) -o $@

.PHONY: music-audio-capture
music-audio-capture: $(BUILD_DIR)/bin/music_audio_capture
$(BUILD_DIR)/bin/music_audio_capture: $(BUILD_DIR)/runtime/tests/music_audio_capture.o $(BUILD_DIR)/runtime/services/music.o
	@mkdir -p $(dir $@)
	$(CC) $^ $(LDFLAGS) -o $@

.PHONY: architecture-smoke led-service-smoke
architecture-smoke: $(BUILD_DIR)/bin/architecture_smoke
$(BUILD_DIR)/bin/architecture_smoke: $(BUILD_DIR)/runtime/tests/architecture_smoke.o $(APP_OBJECTS) $(LVGL_OBJECTS) $(BUILD_DIR)/service_job.o
	@mkdir -p $(dir $@)
	$(CC) $^ $(LDFLAGS) -o $@

led-service-smoke: $(BUILD_DIR)/bin/led_service_smoke
$(BUILD_DIR)/bin/led_service_smoke: $(BUILD_DIR)/runtime/tests/led_service_smoke.o $(BUILD_DIR)/runtime/services/led.o
	@mkdir -p $(dir $@)
	$(CC) $^ $(LDFLAGS) -o $@
