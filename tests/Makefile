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

# One binary per subject. Keeping them separate means a failure names the area
# in the make output before you've read a single line of test text.
HR_SOURCES    := test_hr_zones.cpp $(ROOT)/hr_zones.cpp
STEPS_SOURCES := test_steps_layout.cpp   # steps_layout.h is header-only
HRLAYOUT_SOURCES := test_hr_layout.cpp   # hr_layout.h is header-only too
BINARIES      := $(BUILD)/test_hr_zones $(BUILD)/test_steps_layout \
                 $(BUILD)/test_hr_layout

# The binary is rebuilt unconditionally rather than by timestamp. It's a
# single-file compile that takes well under a second, and make's 1-second
# mtime granularity otherwise lets a quick edit-and-rerun silently run the
# PREVIOUS build — which is a genuinely confusing way to be told your change
# did nothing.
.PHONY: test clean compile
# Every binary runs even if an earlier one fails, so one broken area doesn't
# hide the state of the others; the recipe still exits non-zero if any failed.
test: compile
	@rc=0; for t in $(BINARIES); do $$t || rc=1; echo; done; exit $$rc

compile: | $(ROOT)/secrets.h $(BUILD)
	@$(CXX) $(CXXFLAGS) -o $(BUILD)/test_hr_zones $(HR_SOURCES)
	@$(CXX) $(CXXFLAGS) -o $(BUILD)/test_steps_layout $(STEPS_SOURCES)
	@$(CXX) $(CXXFLAGS) -o $(BUILD)/test_hr_layout $(HRLAYOUT_SOURCES)

$(BUILD):
	@mkdir -p $(BUILD)

# Order-only prerequisite: create secrets.h from the example if it's missing,
# never overwrite a real one.
$(ROOT)/secrets.h:
	@cp $(ROOT)/secrets.h.example $@

clean:
	@rm -rf $(BUILD)
