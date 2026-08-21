CC      ?= cc
AR      ?= ar
WARN     = -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion \
           -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Werror
CFLAGS  ?= -O2 -g
CPPFLAGS = -std=c99 -Iinclude -Isrc

LDLIBS  ?=
ifeq ($(OS),Windows_NT)
LDLIBS += -lws2_32
endif

BUILD    = build
LIB      = $(BUILD)/libcyclone.a

# The library's sources live in sources.txt, one path per line, so this
# Makefile and CMakeLists.txt read the same list instead of keeping two that
# drift. That file must hold nothing but paths - a comment or a blank line
# there becomes a bogus source name on both sides.
SRCS     = $(shell cat sources.txt)
OBJS     = $(SRCS:%.c=$(BUILD)/%.o)

TEST_SRCS = tests/test_frame.c tests/test_handshake.c tests/test_heartbeat.c \
            tests/test_runtime.c tests/test_interop.c
TEST_BINS = $(TEST_SRCS:%.c=$(BUILD)/%)
TEST_CPPFLAGS = $(CPPFLAGS) -Itests

EXAMPLE_SRCS = examples/echo_server.c examples/echo_client.c
EXAMPLE_BINS = $(EXAMPLE_SRCS:%.c=$(BUILD)/%)

DEMO_DIR      = examples/demo
DEMO_PRESENT  = $(wildcard $(DEMO_DIR)/src/generated/handshake.h)
DEMO_CPPFLAGS = $(TEST_CPPFLAGS) -I$(DEMO_DIR) -I$(DEMO_DIR)/src -I$(DEMO_DIR)/src/generated
DEMO_WARN     = -Wall -Wextra

# --- optional C++ wrapper ------------------------------------------------
# Everything C++ lives between this marker and the one below. Delete the
# block, include/cyclone/net.hpp, tests/test_cpp.cpp and examples/*_cpp.cpp,
# and the C library above builds and tests exactly as it did before - no C
# source, target or flag refers to any of it. The build also skips itself
# when either half is missing, so a machine with cc but no c++ still runs
# `make test` green.
CXX          ?= c++
CXXFLAGS     ?= -O2 -g
CXX_CPPFLAGS  = -std=c++17 -Iinclude
CXX_WARN      = -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror

CPP_HEADER    = $(wildcard include/cyclone/net.hpp)
CPP_TEST_SRC  = $(wildcard tests/test_cpp.cpp)
CPP_EX_SRCS   = $(wildcard examples/echo_client_cpp.cpp examples/echo_server_cpp.cpp)
CPP_EX_BINS   = $(CPP_EX_SRCS:%.cpp=$(BUILD)/%)
HAVE_CXX      = $(shell command -v $(CXX) >/dev/null 2>&1 && echo yes)
CPP_TEST_ON   = $(if $(and $(CPP_HEADER),$(CPP_TEST_SRC),$(HAVE_CXX)),yes,)
CPP_EX_ON     = $(if $(and $(CPP_HEADER),$(CPP_EX_SRCS),$(HAVE_CXX),$(DEMO_PRESENT)),yes,)
# --- end optional C++ wrapper --------------------------------------------

.PHONY: all test sanitize examples demo-test cpp-test cpp-examples install uninstall clean

all: $(LIB)

$(LIB): $(OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $(OBJS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -c $< -o $@

$(BUILD)/tests/%: tests/%.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) $(WARN) $< $(LIB) $(LDLIBS) -o $@

test: $(TEST_BINS) demo-test cpp-test
	@set -e; for t in $(TEST_BINS); do ./$$t; done
	@if [ -n "$(DEMO_PRESENT)" ]; then ./$(BUILD)/tests/test_demo; fi
	@if [ -n "$(CPP_TEST_ON)" ]; then ./$(BUILD)/tests/test_cpp; fi

$(BUILD)/tests/test_demo: tests/test_demo.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(DEMO_CPPFLAGS) $(CFLAGS) $(DEMO_WARN) $< $(LIB) $(LDLIBS) -o $@

demo-test:
ifeq ($(DEMO_PRESENT),)
	@echo "SKIP test_demo (no generated tree at $(DEMO_DIR))"
else
	@$(MAKE) --no-print-directory $(BUILD)/tests/test_demo
endif

$(BUILD)/examples/%: examples/%.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(DEMO_CPPFLAGS) $(CFLAGS) $(DEMO_WARN) $< $(LIB) $(LDLIBS) -o $@

examples: $(EXAMPLE_BINS) cpp-examples

# --- optional C++ wrapper ------------------------------------------------
$(BUILD)/tests/test_cpp: tests/test_cpp.cpp $(CPP_HEADER) $(LIB)
	@mkdir -p $(dir $@)
	$(CXX) $(CXX_CPPFLAGS) -Itests $(CXXFLAGS) $(CXX_WARN) $< $(LIB) $(LDLIBS) -o $@

$(BUILD)/examples/%_cpp: examples/%_cpp.cpp $(CPP_HEADER) $(LIB)
	@mkdir -p $(dir $@)
	$(CXX) $(CXX_CPPFLAGS) -I$(DEMO_DIR) -I$(DEMO_DIR)/src -I$(DEMO_DIR)/src/generated \
		$(CXXFLAGS) $(DEMO_WARN) $< $(LIB) $(LDLIBS) -o $@

cpp-test:
ifeq ($(CPP_TEST_ON),)
	@echo "SKIP test_cpp (no C++ wrapper, or no $(CXX))"
else
	@$(MAKE) --no-print-directory $(BUILD)/tests/test_cpp
endif

cpp-examples:
ifeq ($(CPP_EX_ON),)
	@echo "SKIP C++ examples (no C++ wrapper, no $(CXX), or no generated tree)"
else
	@$(MAKE) --no-print-directory $(CPP_EX_BINS)
endif
# --- end optional C++ wrapper --------------------------------------------

sanitize:
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory test \
		CFLAGS="-Og -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
		CXXFLAGS="-Og -g -fsanitize=address,undefined -fno-omit-frame-pointer"
	@$(MAKE) --no-print-directory clean

PREFIX     ?= /usr/local
INCLUDEDIR  = $(DESTDIR)$(PREFIX)/include
LIBDIR      = $(DESTDIR)$(PREFIX)/lib

# include/cyclone.h is deliberately NOT installed. It carries only the
# CYCLONE_MODEL/CYCLONE_FIELD/CYCLONE_CODEC markers, every one of which
# expands to nothing; cyclonec reads them as source text at generation time
# and no compiled byte of this library refers to them. It is a file a
# developer copies into their own project - the C/C++ counterpart of the
# cyclone-attributes crate for Rust - so someone who only generates codecs
# needs it without needing this library at all. Never widen the glob below
# to include/*.h, which would sweep it in.
install: $(LIB)
	install -d $(LIBDIR) $(INCLUDEDIR)/cyclone
	install -m 644 $(LIB) $(LIBDIR)/
	install -m 644 include/cyclone/*.h $(INCLUDEDIR)/cyclone/
	@if [ -n "$(CPP_HEADER)" ]; then \
		echo "install -m 644 $(CPP_HEADER) $(INCLUDEDIR)/cyclone/"; \
		install -m 644 $(CPP_HEADER) $(INCLUDEDIR)/cyclone/; \
	else \
		echo "SKIP cyclone/net.hpp (C++ wrapper not present)"; \
	fi

uninstall:
	rm -f $(LIBDIR)/libcyclone.a
	rm -rf $(INCLUDEDIR)/cyclone

clean:
	rm -rf $(BUILD)
