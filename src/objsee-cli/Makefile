ARCHS = arm64
TARGET := iphone:clang:latest:16.5

include $(THEOS)/makefiles/common.mk

TOOL_NAME = objsee-cli
objsee-cli_INSTALL_PATH = /usr/local/bin
objsee-cli_FILES = $(wildcard ./*.c) $(wildcard ./*.m) $(wildcard ../libobjsee/*/*.c)
objsee-cli_CFLAGS = -fobjc-arc -I./ -I./../libobjsee/config -I./../libobjsee/formatting -I./../libobjsee/interception -I./../libobjsee/tracing -I./../libobjsee/transport -I./../dependencies/include -Wno-unused-but-set-variable
objsee-cli_LDFLAGS = -L./../dependencies/libs/ -ljson-c -lncursesw
objsee-cli_CODESIGN_FLAGS = -S./objsee-entitlements.xml

include $(THEOS_MAKE_PATH)/tool.mk
