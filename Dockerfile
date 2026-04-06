# Single-stage image: build voicebot; Piper + ONNX + vendor from setup.sh (not git).
# Default: skip Whisper GGML (~1.5GB). Full: docker build --build-arg SKIP_WHISPER_MODEL=0 .
#
# Base: Debian bookworm-slim (glibc). The official Piper tarball is built for glibc Linux;
# Alpine + gcompat often yields broken or silent synthesis — use Debian for reliable TTS.

FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

ARG SKIP_WHISPER_MODEL=1
ARG PIPER_VOICE_SPECS=
ENV SKIP_WHISPER_MODEL=${SKIP_WHISPER_MODEL}
ENV PIPER_VOICE_SPECS=${PIPER_VOICE_SPECS}

WORKDIR /app

COPY Makefile setup.sh ./
COPY src/ ./src/

RUN set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        wget \
        git \
        bash \
        build-essential \
        cmake \
        pkg-config \
        libasound2-dev \
        libcurl4-openssl-dev \
        libmicrohttpd-dev \
    ; \
    chmod +x setup.sh; \
    ./setup.sh; \
    make -j"$(nproc)" RNNOISE_CPUFLAGS="-mtune=generic"; \
    apt-get purge -y \
        build-essential \
        cmake \
        pkg-config \
        libasound2-dev \
        libcurl4-openssl-dev \
        libmicrohttpd-dev \
    ; \
    apt-get autoremove -y; \
    apt-get install -y --no-install-recommends \
        libasound2 \
        libcurl4 \
        libmicrohttpd12 \
    ; \
    rm -rf /var/lib/apt/lists/* /root/.cache

EXPOSE 8080

ENTRYPOINT ["/app/build/voicebot"]
CMD ["--api", "--listen", "8080"]
