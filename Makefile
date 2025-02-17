ARCHS = arm64
TARGET := iphone:clang:latest:16.5

include $(THEOS)/makefiles/common.mk


INCLUDE_DIRS := $(shell find . ./src/libobjsee -type d)
INCLUDE_FLAGS := $(INCLUDE_DIRS:%=-I%)

FRAMEWORK_NAME = objsee
objsee_INSTALL_PATH = /Library/Frameworks
objsee_FILES := $(shell find src/libobjsee -type f \( -name '*.c' -o -name '*.m' \))
objsee_PUBLIC_HEADERS = src/libobjsee/tracing/tracer.h src/libobjsee/tracing/tracer_types.h src/libobjsee/tracing/tracer_internal.h
objsee_CFLAGS = -fobjc-arc $(INCLUDE_FLAGS) -I./dependencies/include
objsee_LDFLAGS = -L./dependencies/libs/ -ljson-c-ios
objsee_RESOURCE_DIRS = ./src/libobjsee/Resources
msgSend_hook.c_CFLAGS = -fno-objc-arc -O2

include $(THEOS_MAKE_PATH)/framework.mk

SUBPROJECTS += ./src/objsee-cli
include $(THEOS_MAKE_PATH)/aggregate.mk
