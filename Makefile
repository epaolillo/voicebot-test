CC      = gcc
CXX     = g++
CFLAGS  = -O2 -Wall -Wextra -Isrc -Ivendor/rnnoise/include
CXXFLAGS= -O2 -Wall -Wextra -std=c++17 -Isrc -Ivendor/rnnoise/include
LDFLAGS = -lasound -lcurl -lm -lstdc++ -lpthread

SRCDIR  = src
BUILDDIR= build

# RNNoise sources (only the core library, not tools)
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
          $(SRCDIR)/fillers.c

CXX_SRCS= $(SRCDIR)/main.cpp

C_OBJS  = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(C_SRCS))
CXX_OBJS= $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(CXX_SRCS))
OBJS    = $(C_OBJS) $(CXX_OBJS) $(RNNOISE_OBJS)

TARGET  = $(BUILDDIR)/voicebot

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILDDIR)/rnnoise_%.o: $(RNNOISE_DIR)/%.c | $(BUILDDIR)
	$(CC) -O2 -march=native -I$(RNNOISE_DIR) -Ivendor/rnnoise/include -DCOMPILE_OPUS -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
