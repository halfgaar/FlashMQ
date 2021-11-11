# build target, used for building the binary, providing shared libraries and could be used as a development env
FROM debian:bullseye-slim as build

# install build dependencies
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install g++ make cmake libssl-dev

# create flashmq user and group for runtime image below
RUN useradd --system --shell /bin/false --user-group --no-log-init flashmq

WORKDIR /usr/src/app
COPY . .
RUN ./build.sh


# from scratch image is empty
FROM scratch as run

# set the user/group to flashmq and copy the passwd/group files from build to make that work
USER flashmq:flashmq
COPY --from=build /etc/passwd /etc/passwd
COPY --from=build /etc/group /etc/group

# copy in the shared libaries in use discovered using ldd on release binary
COPY --from=build /lib/x86_64-linux-gnu/libpthread.so.0 /lib/x86_64-linux-gnu/libpthread.so.0
COPY --from=build /lib/x86_64-linux-gnu/libdl.so.2 /lib/x86_64-linux-gnu/libdl.so.2
COPY --from=build /usr/lib/x86_64-linux-gnu/libssl.so.1.1 /usr/lib/x86_64-linux-gnu/libssl.so.1.1
COPY --from=build /usr/lib/x86_64-linux-gnu/libcrypto.so.1.1 /usr/lib/x86_64-linux-gnu/libcrypto.so.1.1
COPY --from=build /usr/lib/x86_64-linux-gnu/libstdc++.so.6 /usr/lib/x86_64-linux-gnu/libstdc++.so.6
COPY --from=build /lib/x86_64-linux-gnu/libgcc_s.so.1 /lib/x86_64-linux-gnu/libgcc_s.so.1
COPY --from=build /lib/x86_64-linux-gnu/libc.so.6 /lib/x86_64-linux-gnu/libc.so.6
COPY --from=build /lib64/ld-linux-x86-64.so.2 /lib64/ld-linux-x86-64.so.2
COPY --from=build /lib/x86_64-linux-gnu/libm.so.6 /lib/x86_64-linux-gnu/libm.so.6

# copy in the FlashMQ binary itself
COPY --from=build /usr/src/app/FlashMQBuildRelease/FlashMQ /bin/FlashMQ

EXPOSE 1883
CMD ["/bin/FlashMQ"]
