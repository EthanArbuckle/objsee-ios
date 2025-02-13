ARCHS = arm64
TARGET := iphone:clang:latest:16.5

include $(THEOS)/makefiles/common.mk

FRAMEWORK_NAME = objsee
objsee_INSTALL_PATH = /Library/Frameworks
objsee_FILES = $(wildcard libobjsee/*/*.c)
objsee_PUBLIC_HEADERS = libobjsee/tracing/tracer.h libobjsee/tracing/tracer_types.h libobjsee/tracing/tracer_internal.h
objsee_CFLAGS = -fobjc-arc -I./libobjsee/config -I./libobjsee/formatting -I./libobjsee/interception -I./libobjsee/tracing -I./libobjsee/transport -I./dependencies/include
objsee_LDFLAGS = -L./dependencies/libs/ -ljson-c
objsee_RESOURCE_DIRS = libobjsee/Resources
msgSend_hook.c_CFLAGS = -fno-objc-arc -O2

include $(THEOS_MAKE_PATH)/framework.mk

SUBPROJECTS += objsee-cli
include $(THEOS_MAKE_PATH)/aggregate.mk
