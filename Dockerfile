# Single-stage Alpine image: fetch piper/vendor/models (ignored by git), build voicebot.
# Target: linux/amd64 — setup.sh downloads Piper's x86_64 glibc binary; gcompat runs it on musl.

FROM alpine:3.20

WORKDIR /app

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
