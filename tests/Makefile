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
DETECTOR_SOURCES := test_step_detector.cpp   # step_detector.h is header-only
TOTEM_SOURCES := test_totem_layout.cpp $(ROOT)/hr_zones.cpp   # totem/face_layout.h, led_map.h header-only
SHOW_SOURCES  := test_show_layout.cpp   # totem/show_layout.h, show_font.h header-only
PHONE_SOURCES := test_phone_protocol.cpp   # totem/phone_protocol.h header-only
BINARIES      := $(BUILD)/test_hr_zones $(BUILD)/test_steps_layout \
                 $(BUILD)/test_hr_layout $(BUILD)/test_step_detector \
                 $(BUILD)/test_totem_layout $(BUILD)/test_show_layout \
                 $(BUILD)/test_phone_protocol

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
	@$(CXX) $(CXXFLAGS) -o $(BUILD)/test_step_detector $(DETECTOR_SOURCES)
	@$(CXX) $(CXXFLAGS) -o $(BUILD)/test_totem_layout $(TOTEM_SOURCES)
	@$(CXX) $(CXXFLAGS) -o $(BUILD)/test_show_layout $(SHOW_SOURCES)
	@$(CXX) $(CXXFLAGS) -o $(BUILD)/test_phone_protocol $(PHONE_SOURCES)
	@for r in 0 1; do for rv in 0 1; do for s in 0 1; do for fx in 0 1; do for fy in 0 1; do for t in 0 1; do \
	  $(CXX) $(CXXFLAGS) -DTOTEM_RING=$$r -DRING_REVERSE=$$rv -DPANEL_SERPENTINE=$$s -DPANEL_FLIP_X=$$fx -DPANEL_FLIP_Y=$$fy -DPANEL_TRANSPOSE=$$t \
	    -o $(BUILD)/test_totem_map $(TOTEM_SOURCES) && $(BUILD)/test_totem_map > /dev/null \
	    || { echo "totem LED map broken for RING=$$r REVERSE=$$rv SERPENTINE=$$s FLIP_X=$$fx FLIP_Y=$$fy TRANSPOSE=$$t"; exit 1; }; \
	  done; done; done; done; done; done

$(BUILD):
	@mkdir -p $(BUILD)

# Order-only prerequisite: create secrets.h from the example if it's missing,
# never overwrite a real one.
$(ROOT)/secrets.h:
	@cp $(ROOT)/secrets.h.example $@

clean:
	@rm -rf $(BUILD)
