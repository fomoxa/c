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

SRCS     = src/common.c src/event.c src/frame.c src/schema.c src/handshake.c src/session.c src/core.c \
           src/connection.c src/server.c src/transport/socket.c src/transport/tcp.c \
           src/transport/udp.c
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

.PHONY: all test sanitize examples demo-test clean

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

test: $(TEST_BINS) demo-test
	@set -e; for t in $(TEST_BINS); do ./$$t; done
	@if [ -n "$(DEMO_PRESENT)" ]; then ./$(BUILD)/tests/test_demo; fi

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

examples: $(EXAMPLE_BINS)

sanitize:
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory test \
		CFLAGS="-Og -g -fsanitize=address,undefined -fno-omit-frame-pointer"
	@$(MAKE) --no-print-directory clean

clean:
	rm -rf $(BUILD)
