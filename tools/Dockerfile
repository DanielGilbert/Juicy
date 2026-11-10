# SPDX-License-Identifier: GPL-3.0-only
# The build machine for Windows and macOS: Linux x86-64 with JieLi's toolchain, the three SDK files the
# package needs, a host compiler and Python. The source is mounted at /work, nothing of it is in the image.
#   docker build --platform linux/amd64 -t juicy-build -f tools/Dockerfile tools
#   docker run --rm -v "${PWD}:/work" juicy-build ./build.sh
#   docker run --rm -v "${PWD}:/work" juicy-build tests/run_host.sh
#   docker run --rm -v "${PWD}:/work" juicy-build web/build.sh       (then, on your machine: node web/serve.js)
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl xz-utils git gcc libc6-dev \
       python3 python3-pil python3-mido python3-rtmidi \
    && rm -rf /var/lib/apt/lists/*

COPY get_toolchain.sh /tmp/get_toolchain.sh
RUN sh /tmp/get_toolchain.sh /opt/jieli

RUN git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
        https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git /tmp/sdk \
    && mkdir -p /opt/ac79-sdk/cpu/wl82/tools/cfg \
    && cp /tmp/sdk/cpu/wl82/tools/uboot.boot /tmp/sdk/cpu/wl82/tools/cfg_tool.bin /opt/ac79-sdk/cpu/wl82/tools/ \
    && cp /tmp/sdk/cpu/wl82/tools/cfg/eq_cfg_hw.bin /opt/ac79-sdk/cpu/wl82/tools/cfg/ \
    && rm -rf /tmp/sdk

# for web/build.sh: the app as WebAssembly (its own layer, so the downloads above stay cached)
RUN apt-get update \
    && apt-get install -y --no-install-recommends clang lld \
    && rm -rf /var/lib/apt/lists/*

ENV JIELI_TOOLCHAIN=/opt/jieli/toolchain AC79_SDK=/opt/ac79-sdk JIELI_DOCKER=0
WORKDIR /work
