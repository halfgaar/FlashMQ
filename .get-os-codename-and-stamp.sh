#!/bin/bash

stamp=$(date +%s)
MY_CODENAME=""

if [[ -e "/etc/os-release" ]]; then
  eval "$(cat "/etc/os-release")"
  MY_CODENAME="$VERSION_CODENAME"
elif [[ -e "/etc/lsb-release" ]]; then
  eval "$(cat "/etc/lsb-release")"
  MY_CODENAME="$DISTRIB_CODENAME"
else
  echo "Error in determing os codename"
  exit 1
fi

if [[ -z "$MY_CODENAME" ]]; then
  echo "ERROR in determining OS codename"
  exit 1
fi

echo -n "${stamp}+${MY_CODENAME}"
