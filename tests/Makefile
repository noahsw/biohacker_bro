# Host-side tests for the hardware-free parts of the firmware.
#
#   make          build and run
#   make clean
#
# These compile with a normal g++/clang++ — no Arduino toolchain, no board
# attached. config.h includes secrets.h, which is gitignored, so fall back to
# the committed example when the real one isn't present (CI, a fresh clone).

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O1
ROOT     := ..
BUILD    := build

SOURCES := test_hr_zones.cpp $(ROOT)/hr_zones.cpp

# The binary is rebuilt unconditionally rather than by timestamp. It's a
# single-file compile that takes well under a second, and make's 1-second
# mtime granularity otherwise lets a quick edit-and-rerun silently run the
# PREVIOUS build — which is a genuinely confusing way to be told your change
# did nothing.
.PHONY: test clean compile
test: compile
	@$(BUILD)/test_hr_zones

compile: | $(ROOT)/secrets.h $(BUILD)
	@$(CXX) $(CXXFLAGS) -o $(BUILD)/test_hr_zones $(SOURCES)

$(BUILD):
	@mkdir -p $(BUILD)

# Order-only prerequisite: create secrets.h from the example if it's missing,
# never overwrite a real one.
$(ROOT)/secrets.h:
	@cp $(ROOT)/secrets.h.example $@

clean:
	@rm -rf $(BUILD)
