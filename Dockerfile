# Single-stage Alpine: build voicebot; Piper + ONNX voices + vendor come from setup.sh (not git).
# Default image skips the ~1.5GB Whisper GGML blob (TTS API does not need it).
# Build full assistant image: docker build --build-arg SKIP_WHISPER_MODEL=0 .
#
# Target: linux/amd64 — Piper tarball is x86_64 glibc; gcompat runs it on musl.

FROM alpine:3.20

WORKDIR /app

# Extra voices: docker build --build-arg PIPER_VOICE_SPECS="es/es_AR/daniela/high|es_AR-daniela-high"
ARG SKIP_WHISPER_MODEL=1
ARG PIPER_VOICE_SPECS=
ENV SKIP_WHISPER_MODEL=${SKIP_WHISPER_MODEL}

COPY Makefile setup.sh ./
COPY src/ ./src/

RUN set -eux; \
    apk add --no-cache \
        bash \
        coreutils \
        curl \
        wget \
        ca-certificates \
        git \
        build-base \
        cmake \
        pkgconf \
        make \
        alsa-lib-dev \
        curl-dev \
        libmicrohttpd-dev \
    ; \
    chmod +x setup.sh; \
    ./setup.sh; \
    make -j"$(nproc)"; \
    apk del --purge --no-cache \
        bash \
        coreutils \
        git \
        wget \
        build-base \
        cmake \
        pkgconf \
        make \
        alsa-lib-dev \
        curl-dev \
        libmicrohttpd-dev \
    ; \
    apk add --no-cache \
        libstdc++ \
        libgcc \
        gcompat \
        alsa-lib \
        curl \
        libmicrohttpd \
        ca-certificates \
    ; \
    rm -rf /tmp/* /root/.cache

EXPOSE 8080

ENTRYPOINT ["/app/build/voicebot"]
CMD ["--api", "--listen", "8080"]
