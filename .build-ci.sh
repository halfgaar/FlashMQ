#!/bin/bash

set -e  # If any step reports a problem consider the whole build a failure
wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
sudo mv linuxdeploy-x86_64.AppImage /usr/local/bin
sudo chmod +x /usr/local/bin/linuxdeploy-x86_64.AppImage
sudo apt update
sudo apt install -y shellcheck
shellcheck debian/post* debian/pre*
./build.sh
./FlashMQBuildRelease/FlashMQ --version
sudo dpkg -i ./FlashMQBuildRelease/*.deb
set +e  # Prevent Travis internals from breaking our build, see https://github.com/travis-ci/travis-ci/issues/891
