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

Official Docker images aren't available yet, but building your own Docker image can be done with the provided Dockerfile.

```
# To build without downloading the source:
# In the next line, replace <tag name> with the tag name of the latest release.
docker build https://github.com/halfgaar/FlashMQ.git#<tag name> -t halfgaar/flashmq

# It's best to checkout a tagged release. See 'git tag'.
git checkout <tag name>
docker build . -t halfgaar/flashmq

# run using docker (with, as an example, a place for a config file (default
# name flashmq.conf). Create extra volumes as you need, for the persistence DB
# file, logs, password files, auth plugin, etc.
docker run -p 1883:1883 -v /srv/flashmq/etc/:/etc/flashmq --user 1000:1000 halfgaar/flashmq

# for development you can target the build stage to get an image you can use for development
docker build . --target=build
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
