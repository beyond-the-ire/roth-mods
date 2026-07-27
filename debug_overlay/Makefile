# Build the Debug Overlay plugin against only the public ROTH.C SDK.
ROTH_SDK ?= ../../ROTH.C/sdk

SOURCES := debug_overlay.c debug_config.c debug_metrics.c debug_state.c \
           debug_draw.c debug_input.c
HEADERS := debug_config.h debug_metrics.h debug_state.h debug_draw.h debug_input.h

ifeq ($(CROSS),mingw)
  CC     := i686-w64-mingw32-gcc
  LIB    := plugin.dll
  CFLAGS := -shared -fvisibility=hidden -std=c11 -Wall -Wextra -Werror -I$(ROTH_SDK)/include
else
  CC     := gcc
  LIB    := plugin.so
  CFLAGS := -m32 -shared -fPIC -fvisibility=hidden -std=c11 -Wall -Wextra -Werror -I$(ROTH_SDK)/include
endif

all: $(LIB)

$(LIB): $(SOURCES) $(HEADERS) $(ROTH_SDK)/include/roth_sdk.h
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

check:
	gcc -m32 -std=c11 -Wall -Wextra -Werror -I$(ROTH_SDK)/include \
	    -o test_metrics tests/test_metrics.c debug_metrics.c
	./test_metrics
	gcc -m32 -std=c11 -Wall -Wextra -Werror -I$(ROTH_SDK)/include \
	    -DTEST_PLUGIN_DIR=\"$(CURDIR)\" -o test_plugin tests/test_plugin.c $(SOURCES)
	./test_plugin
	rm -f test_metrics test_plugin

clean:
	rm -f plugin.so plugin.dll test_metrics test_plugin
