RACK_DIR ?= ../Rack-SDK
ifeq ("$(wildcard $(RACK_DIR)/plugin.mk)","")
RACK_DIR := /Users/lazuli/Documents/PROGRAMMING/TEENSY/KSZ_TEENSY_PLATFORMIO/Teensy_Chord_Gen/Rack-SDK
endif

PLUGIN_SLUG := CHORD_GEN
LOCAL_RACK_PLUGIN_DIR ?= $(HOME)/Library/Application Support/Rack2/plugins-mac-arm64
BIG_MAC_MOUNT_DIR ?= /Volumes/music
BIG_MAC_FALLBACK_MOUNT_DIR ?= $(HOME)/Volumes/music
BIG_MAC_RACK_SUBDIR ?= Library/Application Support/Rack2/plugins-mac-arm64
DEPLOY_BIG_SCRIPT := scripts/deploy_big.sh

LOCAL_DEPLOY_DIR := $(LOCAL_RACK_PLUGIN_DIR)/$(PLUGIN_SLUG)
BIG_MAC_DEPLOY_DIR := $(BIG_MAC_MOUNT_DIR)/$(BIG_MAC_RACK_SUBDIR)/$(PLUGIN_SLUG)

BUILD_BUMP_SCRIPT := scripts/bump_build.sh
NO_BUMP_GOALS := clean cleandist
SHOULD_BUMP := 0
ifeq ($(strip $(MAKECMDGOALS)),)
SHOULD_BUMP := 1
else ifneq ($(strip $(filter-out $(NO_BUMP_GOALS),$(MAKECMDGOALS))),)
SHOULD_BUMP := 1
endif

ifeq ($(SHOULD_BUMP),1)
BUMP_RESULT := $(shell $(BUILD_BUMP_SCRIPT))
ifneq ($(strip $(BUMP_RESULT)),)
$(info $(BUMP_RESULT))
endif
endif

FLAGS += -std=c++17

SOURCES += src/plugin.cpp
SOURCES += src/ChordGenModule.cpp

DISTRIBUTABLES += res
DISTRIBUTABLES += BUILD_NUMBER.txt

include $(RACK_DIR)/plugin.mk

.PHONY: deploy-local deploy-big deploy-both

deploy-local: dist
	rsync -av --delete dist/$(PLUGIN_SLUG)/ "$(LOCAL_DEPLOY_DIR)/"

deploy-big: dist
	"$(DEPLOY_BIG_SCRIPT)" "dist/$(PLUGIN_SLUG)" "$(BIG_MAC_MOUNT_DIR)" "$(BIG_MAC_FALLBACK_MOUNT_DIR)" "$(BIG_MAC_RACK_SUBDIR)" "$(PLUGIN_SLUG)"

deploy-both: dist
	rsync -av --delete dist/$(PLUGIN_SLUG)/ "$(LOCAL_DEPLOY_DIR)/"
	"$(DEPLOY_BIG_SCRIPT)" "dist/$(PLUGIN_SLUG)" "$(BIG_MAC_MOUNT_DIR)" "$(BIG_MAC_FALLBACK_MOUNT_DIR)" "$(BIG_MAC_RACK_SUBDIR)" "$(PLUGIN_SLUG)"
