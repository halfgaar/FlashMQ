# build target, used for building the binary, providing shared libraries and could be used as a development env
FROM debian:trixie-slim AS build

# install build dependencies
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y --no-install-recommends install g++ make cmake libssl-dev file

# create flashmq user and group for runtime image below
RUN useradd --system --shell /bin/false --user-group --no-log-init flashmq

ARG BUILD_TYPE=Release
RUN echo "Building ${BUILD_TYPE} version"

WORKDIR /usr/src/app
COPY . .
RUN rm -rf FlashMQBuild* 2>/dev/null
RUN ./build.sh ${BUILD_TYPE}

RUN mkdir /runtime-libs \
    && ldd FlashMQBuildRelease/flashmq | grep -o '/[^ ]*' > /tmp/libs.txt \
    && test -s /tmp/libs.txt \
    && xargs -a /tmp/libs.txt cp --parents -L -t /runtime-libs/ \
    && find /runtime-libs -type f

FROM scratch AS run

USER flashmq:flashmq
COPY --from=build /etc/passwd /etc/passwd
COPY --from=build /etc/group /etc/group

COPY --from=build /runtime-libs/ /

COPY --from=build /usr/src/app/FlashMQBuildRelease/flashmq /bin/flashmq

EXPOSE 1883
CMD ["/bin/flashmq"]
