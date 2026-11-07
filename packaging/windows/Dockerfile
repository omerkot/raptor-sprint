# The cross compiler for the Windows build (x86-64). Everything is linked statically, so the program
# needs no DLLs beside it.
FROM debian:bookworm
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
