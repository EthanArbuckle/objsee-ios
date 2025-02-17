ARCHS = arm64
TARGET := iphone:clang:latest:16.5

include $(THEOS)/makefiles/common.mk

INCLUDE_DIRS := $(shell find . ../libobjsee -type d)
INCLUDE_FLAGS := $(INCLUDE_DIRS:%=-I%)

TOOL_NAME = objsee-cli
objsee-cli_INSTALL_PATH = /usr/local/bin
objsee-cli_FILES := $(shell find . -type f \( -name '*.c' -o -name '*.m' \)) $(shell find ../libobjsee -type f \( -name '*.c' -o -name '*.m' \)) $(wildcard ../../dependencies/kat/*.c)
objsee-cli_CFLAGS := -fobjc-arc $(INCLUDE_FLAGS) -I./../../dependencies/include -I./../../dependencies/kat -Wno-unused-but-set-variable
objsee-cli_LDFLAGS = -L../../dependencies/libs/ -ljson-c-ios -lncursesw-ios -lcapstone-ios
objsee-cli_CODESIGN_FLAGS = -S./objsee-entitlements.xml

include $(THEOS_MAKE_PATH)/tool.mk
