ARCHS = armv7 arm64 arm64e
TARGET := iphone:clang:latest:10

include $(THEOS)/makefiles/common.mk

INCLUDE_DIRS := $(shell find . ./src/libobjsee -type d)
INCLUDE_FLAGS := $(INCLUDE_DIRS:%=-I%)

FRAMEWORK_NAME = libobjsee
libobjsee_INSTALL_PATH = /Library/Frameworks
libobjsee_PUBLIC_HEADERS = src/libobjsee/tracing/tracer.h src/libobjsee/tracing/tracer_types.h src/libobjsee/tracing/tracer_internal.h
libobjsee_RESOURCE_DIRS = ./src/libobjsee/Resources

libobjsee_FILES := $(shell find src/libobjsee -type f \( -name '*.c' -o -name '*.m' \)) $(wildcard ./dependencies/yyjson/src/*.c)

ifneq (,$(filter $(THEOS_CURRENT_ARCH),arm64 arm64e))
libobjsee_FILES += ./src/libobjsee/interception/arm64.s
else ifeq ($(THEOS_CURRENT_ARCH),armv7)
libobjsee_FILES += ./src/libobjsee/interception/armv7.s
endif

libobjsee_CFLAGS = -fobjc-arc $(INCLUDE_FLAGS) -I./dependencies/include -I./dependencies/yyjson/src -Wno-deprecated-declarations
msgSend_hook.c_CFLAGS = -fno-objc-arc -O2
armv7_CFLAGS := -femulated-tls -D_FORTIFY_SOURCE=0

include $(THEOS_MAKE_PATH)/framework.mk

SUBPROJECTS += ./src/objsee-cli
include $(THEOS_MAKE_PATH)/aggregate.mk
