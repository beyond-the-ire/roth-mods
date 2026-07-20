ROTH_SDK ?= ../../ROTH.C/sdk

SOURCES := renderer_overhaul.c hwr_settings.c hwr_gl.c hwr_world.c hwr_das.c hwr_scene.c \
           hwr_backdrop.c
HEADERS := hwr_settings.h hwr_gl.h hwr_world.h hwr_das.h hwr_scene.h hwr_backdrop.h \
           $(ROTH_SDK)/include/roth_sdk.h \
           $(ROTH_SDK)/include/roth_sdk_overrides.h

ifeq ($(CROSS),mingw)
  CC := i686-w64-mingw32-gcc
  LIB := plugin.dll
  CFLAGS := -shared -std=c11 -Wall -Wextra -Werror -I$(ROTH_SDK)/include
  LDLIBS := -lopengl32 -lgdi32 -luser32
else
  CC := gcc
  LIB := plugin.so
  CFLAGS := -m32 -shared -fPIC -fvisibility=hidden -std=c11 -Wall -Wextra -Werror -I$(ROTH_SDK)/include
  LDLIBS := -lEGL -lGL
endif

.PHONY: all clean

all: $(LIB)

$(LIB): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS)

clean:
	rm -f plugin.so plugin.dll
