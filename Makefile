ARCHS = arm64
TARGET := iphone:clang:latest:16.5

include $(THEOS)/makefiles/common.mk

INCLUDE_DIRS := $(shell find . ./src/libobjsee -type d)
INCLUDE_FLAGS := $(INCLUDE_DIRS:%=-I%)

FRAMEWORK_NAME = libobjsee
libobjsee_INSTALL_PATH = /Library/Frameworks
libobjsee_FILES := $(shell find src/libobjsee -type f \( -name '*.c' -o -name '*.m' \)) $(wildcard ./dependencies/yyjson/src/*.c)
libobjsee_PUBLIC_HEADERS = src/libobjsee/tracing/tracer.h src/libobjsee/tracing/tracer_types.h src/libobjsee/tracing/tracer_internal.h
libobjsee_CFLAGS = -fobjc-arc $(INCLUDE_FLAGS) -I./dependencies/include -I./dependencies/yyjson/src -Wno-deprecated-declarations
libobjsee_LDFLAGS = -L./dependencies/libs/
libobjsee_RESOURCE_DIRS = ./src/libobjsee/Resources
msgSend_hook.c_CFLAGS = -fno-objc-arc -O2

include $(THEOS_MAKE_PATH)/framework.mk

SUBPROJECTS += ./src/objsee-cli
include $(THEOS_MAKE_PATH)/aggregate.mk
