FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
      clang make cmake ninja-build python3 guile-3.0 file ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
