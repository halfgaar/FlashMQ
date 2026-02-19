# build target, used for building the binary, providing shared libraries and could be used as a development env
FROM debian:trixie-slim AS build

# install build dependencies
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install g++ make cmake libssl-dev file

# create flashmq user and group for runtime image below
RUN useradd --system --shell /bin/false --user-group --no-log-init flashmq

WORKDIR /usr/src/app
COPY . .
RUN ./build.sh

# convert docker buildx platform name to Debian platform name
FROM scratch AS run-amd64
ARG PLATFORM=x86_64
ARG LD_LOCATION=/lib64/ld-linux-x86-64.so.2

FROM scratch AS run-arm64
ARG PLATFORM=aarch64
ARG LD_LOCATION=/lib/ld-linux-aarch64.so.1

# from scratch image is empty
FROM run-$TARGETARCH AS run

USER flashmq:flashmq
COPY --from=build /etc/passwd /etc/passwd
COPY --from=build /etc/group /etc/group

# copy in the shared libaries in use discovered using ldd on release binary
COPY --from=build /lib/${PLATFORM}-linux-gnu/libpthread.so.0 /lib/${PLATFORM}-linux-gnu/libpthread.so.0
COPY --from=build /lib/${PLATFORM}-linux-gnu/libdl.so.2 /lib/${PLATFORM}-linux-gnu/libdl.so.2
COPY --from=build /usr/lib/${PLATFORM}-linux-gnu/libssl.so.3 /usr/lib/${PLATFORM}-linux-gnu/libssl.so.3
COPY --from=build /usr/lib/${PLATFORM}-linux-gnu/libcrypto.so.3 /usr/lib/${PLATFORM}-linux-gnu/libcrypto.so.3
COPY --from=build /usr/lib/${PLATFORM}-linux-gnu/libstdc++.so.6 /usr/lib/${PLATFORM}-linux-gnu/libstdc++.so.6
COPY --from=build /lib/${PLATFORM}-linux-gnu/libgcc_s.so.1 /lib/${PLATFORM}-linux-gnu/libgcc_s.so.1
COPY --from=build /lib/${PLATFORM}-linux-gnu/libc.so.6 /lib/${PLATFORM}-linux-gnu/libc.so.6
COPY --from=build ${LD_LOCATION} ${LD_LOCATION}
COPY --from=build /lib/${PLATFORM}-linux-gnu/libm.so.6 /lib/${PLATFORM}-linux-gnu/libm.so.6
COPY --from=build /lib/${PLATFORM}-linux-gnu/libresolv.so.2 /lib/${PLATFORM}-linux-gnu/libresolv.so.2
COPY --from=build /lib/${PLATFORM}-linux-gnu/libanl.so.1 /lib/${PLATFORM}-linux-gnu/libanl.so.1
COPY --from=build /lib/${PLATFORM}-linux-gnu/libz.so.1 /lib/${PLATFORM}-linux-gnu/libz.so.1
COPY --from=build /lib/${PLATFORM}-linux-gnu/libzstd.so.1 /lib/${PLATFORM}-linux-gnu/libzstd.so.1

# copy in the FlashMQ binary itself
COPY --from=build /usr/src/app/FlashMQBuildRelease/flashmq /bin/flashmq

EXPOSE 1883
CMD ["/bin/flashmq"]