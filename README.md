# FlashMQ

![building](https://github.com/halfgaar/FlashMQ/actions/workflows/building.yml/badge.svg)
![testing](https://github.com/halfgaar/FlashMQ/actions/workflows/testing.yml/badge.svg)
![linting](https://github.com/halfgaar/FlashMQ/actions/workflows/linting.yml/badge.svg)
![docker](https://github.com/halfgaar/FlashMQ/actions/workflows/docker.yml/badge.svg)

FlashMQ is a high-performance, light-weight MQTT broker/server, designed to take good advantage of multi-CPU environments.

Builds (AppImage and a Debian/Ubuntu apt server) are provided on [www.flashmq.org](https://www.flashmq.org).

## Building from source

Building from source should be done with `build.sh`. It's best to checkout a tagged release first. See `git tag`.

If you build manually with `cmake` with default options, you won't have `-DCMAKE_BUILD_TYPE=Release` and you will have debug code enabled and no compiler optimizations (`-O3`). In other words, it's essentially a debug build, but without debugging symbols.

## Docker

Official Docker images aren't available yet, but building your own Docker image can be done with the provided `Dockerfile`. Be sure to use the new buildx builder, as the `Dockerfile` is not compatible with the legacy build plugin.

You can build using the `Dockerfile` only (replace `<tag_name>` with a proper version tag, like `v1.25.0`):

```
docker build --file Dockerfile https://github.com/halfgaar/FlashMQ.git#<tag_name> -t halfgaar/flashmq
```

Or, build using a local git clone (building a specific version tag is recommended):

```
git checkout <tag_name>
docker build . -t halfgaar/flashmq
```

To run:

```
docker run -p 1883:1883 -v /srv/flashmq/etc/:/etc/flashmq --user 1000:1000 halfgaar/flashmq
```

Create extra volumes as you need, for the persistence DB file, logs, password files, plugin, etc.

For development you can target the build stage to get an image you can use for development:

```
docker build . --build-arg BUILD_TYPE=Debug --target=build
```

## Plugins

A plugin interface is defined and documented in `flashmq_plugin.h`. It allows custom authentication and other behavior.

See the `examples` directory for example implementations of this interface.

## Commercial services

If your company requires commercial FlashMQ services, go to
[www.flashmq.com](https://www.flashmq.com).  Services include:

- the development of custom FlashMQ plugins;
- MQTT integration advice; and
- managed FlashMQ. Send an email to service@flashmq.com to be notified for early trials.
