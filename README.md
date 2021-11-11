# FlashMQ
[![Build Status](https://travis-ci.com/halfgaar/FlashMQ.svg?branch=master)](https://travis-ci.com/halfgaar/FlashMQ)

FlashMQ is a light-weight MQTT broker/server, designed to take good advantage of multi-CPU environments.

Build with build.sh.

## Docker
```
# build flashmq docker image
docker build . -t halfgaar/flashmq

# run using docker
docker run -p 1883:1883 halfgaar/flashmq

# for development you can target the build stage to get an image you can use for development
docker build . --target=build
```
See [www.flashmq.org](https://www.flashmq.org)
