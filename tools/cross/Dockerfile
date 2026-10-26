# The fast build's compiler (build.sh): Debian bullseye (glibc 2.31, as the kit's arm32v7/gcc:11-bullseye image) with
# its armhf cross compiler, so the plugin compiles at full speed on a PC, on every core, instead of under emulation.
FROM debian:bullseye
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++-arm-linux-gnueabihf binutils-arm-linux-gnueabihf make \
 && rm -rf /var/lib/apt/lists/*
