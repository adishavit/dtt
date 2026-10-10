FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang-18 libz3-4 bubblewrap python3 util-linux \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /probe
COPY libDTTPlugin.so probe.py ./
CMD ["timeout", "60", "python3", "-u", "probe.py"]
