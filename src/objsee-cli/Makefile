ARCHS = armv7 arm64
TARGET := iphone:clang:latest:10

include $(THEOS)/makefiles/common.mk

INCLUDE_DIRS := $(shell find . ../libobjsee -type d)
INCLUDE_FLAGS := $(INCLUDE_DIRS:%=-I%)

TOOL_NAME = objsee
objsee_INSTALL_PATH = /usr/bin
objsee_CODESIGN_FLAGS = -S./objsee-entitlements.xml
objsee_FRAMEWORKS = libobjsee

objsee_FILES := $(filter-out %/mach_excServer.c,$(shell find . -type f \( -name '*.c' -o -name '*.m' \)) $(wildcard ../../dependencies/kat/*.c) $(wildcard ../../dependencies/yyjson/src/*.c))

ifeq ($(THEOS_CURRENT_ARCH),arm64)
objsee_FILES += ./exception_handling/mach_excServer.c
objsee_LDFLAGS = -L../../dependencies/libs/ -lncursesw-ios -lcapstone-ios
endif

objsee_CFLAGS := -fobjc-arc $(INCLUDE_FLAGS) -I./../../dependencies/include -I./../../dependencies/kat -I./../../dependencies/yyjson/src -DBUILDING_CLI_TOOL -Wno-unused-but-set-variable -Wno-deprecated-declarations -Wno-error=#warnings
armv7_CFLAGS := -femulated-tls

include $(THEOS_MAKE_PATH)/tool.mk
