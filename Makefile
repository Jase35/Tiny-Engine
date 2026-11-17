CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
CPPFLAGS += -Isrc/common -MMD -MP
LDLIBS   += -lSDL2

TARGET  := tiny_engine.exe
rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))
SRCS    := $(call rwildcard,src/common,*.cpp) $(call rwildcard,src/interfaces/windows,*.cpp)
OBJDIR  := build
OBJS    := $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(SRCS))
DEPS    := $(OBJS:.o=.d)

# Assets: each filetype in res/ is compiled into one header in src/common/assets/.
ifeq ($(OS),Windows_NT)
PYTHON  ?= py
else
PYTHON  ?= python3
endif
ASSETDIR          := src/common/assets
MODELS            := $(sort $(call rwildcard,res/models,*.glb) $(call rwildcard,res/models,*.gltf))
MAX_TEXTURE_SIZE  ?= 128
ASSET_HEADERS     := $(ASSETDIR)/models.hpp

.PHONY: all clean run debug assets

all: $(TARGET)

assets: $(ASSET_HEADERS)

$(ASSETDIR)/models.hpp: $(MODELS) tools/glb2bin.py
	$(PYTHON) tools/glb2bin.py $(MODELS) -o $@ --max-texture-size $(MAX_TEXTURE_SIZE)

# Generate assets before compiling; the .d files then track which objects include them.
$(OBJS): | $(ASSET_HEADERS)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(OBJDIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

debug: CXXFLAGS := -std=c++17 -Wall -Wextra -g -O0
debug: clean all

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(OBJDIR) $(TARGET)

-include $(DEPS)
