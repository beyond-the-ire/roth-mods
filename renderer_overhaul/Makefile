ROTH_SDK ?= ../../ROTH.C/sdk

SOURCES := renderer_overhaul.c hwr_settings.c hwr_schema.c hwr_gl.c hwr_world.c hwr_das.c \
           hwr_scene.c hwr_backdrop.c hwr_visibility.c hwr_sprites.c hwr_objects.c \
           hwr_billboards.c hwr_meshes.c hwr_perf.c hwr_config.c hwr_thumbnail.c hwr_util.c \
           hwr_projection.c hwr_input.c hwr_composite.c hwr_chrome.c \
           hwr_ownership.c hwr_overlay.c hwr_overlay_draw.c hwr_sysinfo.c \
           hwr_enh_truecolor.c hwr_look.c hwr_lights.c hwr_json.c
HEADERS := hwr_settings.h hwr_schema.h hwr_gl.h hwr_world.h hwr_das.h hwr_scene.h \
           hwr_backdrop.h hwr_visibility.h hwr_sprites.h hwr_objects.h hwr_billboards.h \
           hwr_perf.h hwr_meshes.h hwr_config.h hwr_thumbnail.h hwr_util.h hwr_projection.h \
           hwr_input.h hwr_composite.h hwr_chrome.h hwr_ownership.h \
           hwr_overlay.h hwr_overlay_draw.h hwr_sysinfo.h \
           hwr_enh_truecolor.h hwr_look.h hwr_lights.h hwr_json.h \
           $(ROTH_SDK)/include/roth_sdk.h \
           $(ROTH_SDK)/include/roth_sdk_overrides.h

ifeq ($(CROSS),mingw)
  CC := i686-w64-mingw32-gcc
  LIB := plugin.dll
  CFLAGS := -O2 -shared -std=c11 -Wall -Wextra -Werror -I$(ROTH_SDK)/include
  LDLIBS := -lopengl32 -lgdi32 -luser32 -lm -static-libgcc
else
  CC := gcc
  LIB := plugin.so
  CFLAGS := -O2 -m32 -shared -fPIC -fvisibility=hidden -std=c11 -Wall -Wextra -Werror -I$(ROTH_SDK)/include
  LDLIBS := -lEGL -lGL -lm
endif

.PHONY: all clean

all: $(LIB)

$(LIB): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS)

clean:
	rm -f plugin.so plugin.dll
