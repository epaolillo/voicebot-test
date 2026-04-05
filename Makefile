CC      = gcc
CXX     = g++
CFLAGS  = -O2 -Wall -Wextra -Isrc -Ivendor/rnnoise/include
CXXFLAGS= -O2 -Wall -Wextra -std=c++17 -Isrc -Ivendor/rnnoise/include
MHD_CPPFLAGS := $(shell pkg-config --cflags libmicrohttpd 2>/dev/null)
ifneq ($(strip $(MHD_CPPFLAGS)),)
CXXFLAGS += $(MHD_CPPFLAGS)
endif
MHD_LIBS := $(shell pkg-config --libs libmicrohttpd 2>/dev/null)
ifeq ($(strip $(MHD_LIBS)),)
MHD_LIBS = -lmicrohttpd
endif
LDFLAGS = -lasound -lcurl -lm -lstdc++ -lpthread $(MHD_LIBS)

SRCDIR  = src
BUILDDIR= build

# --- whisper.cpp ---
WHISPER_DIR   = vendor/whisper.cpp
WHISPER_BUILD = $(WHISPER_DIR)/build
WHISPER_LINK  = $(WHISPER_BUILD)/src/libwhisper.a \
                $(WHISPER_BUILD)/ggml/src/libggml.a \
                $(WHISPER_BUILD)/ggml/src/libggml-cpu.a \
                $(WHISPER_BUILD)/ggml/src/libggml-base.a

CXXFLAGS += -I$(WHISPER_DIR)/include -I$(WHISPER_DIR)/ggml/include

# --- RNNoise ---
RNNOISE_DIR = vendor/rnnoise/src
RNNOISE_SRCS= $(RNNOISE_DIR)/denoise.c \
              $(RNNOISE_DIR)/celt_lpc.c \
              $(RNNOISE_DIR)/kiss_fft.c \
              $(RNNOISE_DIR)/nnet.c \
              $(RNNOISE_DIR)/nnet_default.c \
              $(RNNOISE_DIR)/pitch.c \
              $(RNNOISE_DIR)/rnn.c \
              $(RNNOISE_DIR)/rnnoise_tables.c \
              $(RNNOISE_DIR)/rnnoise_data.c \
              $(RNNOISE_DIR)/parse_lpcnet_weights.c

RNNOISE_OBJS= $(patsubst $(RNNOISE_DIR)/%.c,$(BUILDDIR)/rnnoise_%.o,$(RNNOISE_SRCS))

C_SRCS  = $(SRCDIR)/env_loader.c \
          $(SRCDIR)/audio_capture.c \
          $(SRCDIR)/noise_reduce.c \
          $(SRCDIR)/vad.c \
          $(SRCDIR)/wav_utils.c \
          $(SRCDIR)/whisper_api.c \
          $(SRCDIR)/llm_stream.c \
          $(SRCDIR)/tts_playback.c \
          $(SRCDIR)/tts_models.c \
          $(SRCDIR)/tts_synthesize.c \
          $(SRCDIR)/fillers.c

CXX_SRCS= $(SRCDIR)/main.cpp \
          $(SRCDIR)/whisper_local.cpp \
          $(SRCDIR)/tts_api_server.cpp

C_OBJS  = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(C_SRCS))
CXX_OBJS= $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(CXX_SRCS))
OBJS    = $(C_OBJS) $(CXX_OBJS) $(RNNOISE_OBJS)

TARGET  = $(BUILDDIR)/voicebot

# RNNoise: default -march=native for local CPU; override for Docker (e.g. RNNOISE_CPUFLAGS=-mtune=generic).
RNNOISE_CPUFLAGS ?= -march=native

.PHONY: all clean whisper

all: $(TARGET)

# One representative file; `make whisper` produces all static libs. CMakeLists keeps rule from always firing.
$(WHISPER_BUILD)/src/libwhisper.a: $(WHISPER_DIR)/CMakeLists.txt
	@$(MAKE) whisper

whisper:
	@if [ ! -f "$(WHISPER_BUILD)/src/libwhisper.a" ]; then \
		echo "=== Building whisper.cpp (CPU-only, no OpenMP) ===" ; \
		cmake -B $(WHISPER_BUILD) -S $(WHISPER_DIR) \
			-DCMAKE_BUILD_TYPE=Release \
			-DBUILD_SHARED_LIBS=OFF \
			-DWHISPER_BUILD_EXAMPLES=OFF \
			-DWHISPER_BUILD_TESTS=OFF \
			-DWHISPER_BUILD_SERVER=OFF \
			-DGGML_OPENMP=OFF \
			-DGGML_CUDA=OFF \
			-DGGML_METAL=OFF \
			-DGGML_VULKAN=OFF ; \
		cmake --build $(WHISPER_BUILD) -j$$(nproc) --config Release ; \
	else \
		echo "=== whisper.cpp already built ===" ; \
	fi

$(TARGET): $(OBJS) $(WHISPER_BUILD)/src/libwhisper.a
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(WHISPER_LINK) $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILDDIR)/rnnoise_%.o: $(RNNOISE_DIR)/%.c | $(BUILDDIR)
	$(CC) -O2 $(RNNOISE_CPUFLAGS) -I$(RNNOISE_DIR) -Ivendor/rnnoise/include -DCOMPILE_OPUS -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
