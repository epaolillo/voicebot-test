# Single-stage Alpine: build voicebot; Piper + ONNX voices + vendor come from setup.sh (not git).
# Default image skips the ~1.5GB Whisper GGML blob (TTS API does not need it).
# Build full assistant image: docker build --build-arg SKIP_WHISPER_MODEL=0 .
#
# Target: linux/amd64 — Piper tarball is x86_64 glibc; gcompat runs it on musl.
#
# Runtime libraries are installed first, then a virtual ".build-deps" bundle; removing the
# virtual package avoids apk del failing on dependency edges (common cause of exit code 2).

FROM alpine:3.20

WORKDIR /app

ARG SKIP_WHISPER_MODEL=1
ARG PIPER_VOICE_SPECS=
ENV SKIP_WHISPER_MODEL=${SKIP_WHISPER_MODEL}

COPY Makefile setup.sh ./
COPY src/ ./src/

RUN set -eux; \
    apk add --no-cache \
        ca-certificates \
        gcompat \
        libstdc++ \
        libgcc \
        alsa-lib \
        libcurl \
        libmicrohttpd \
    ; \
    apk add --no-cache --virtual .build-deps \
        bash \
        coreutils \
        curl \
        wget \
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
    make -j"$(nproc)" RNNOISE_CPUFLAGS="-mtune=generic"; \
    apk del .build-deps; \
    rm -rf /root/.cache

EXPOSE 8080

ENTRYPOINT ["/app/build/voicebot"]
CMD ["--api", "--listen", "8080"]
