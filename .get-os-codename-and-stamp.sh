#!/bin/bash

my_codename=""
my_int_version=""

if [[ -e "/etc/os-release" ]]; then
  eval "$(cat "/etc/os-release")"
  my_codename="$VERSION_CODENAME"
  my_int_version=${VERSION_ID//./}
elif [[ -e "/etc/lsb-release" ]]; then
  eval "$(cat "/etc/lsb-release")"
  my_codename="$DISTRIB_CODENAME"
  my_int_version=${DISTRIB_RELEASE//./}
else
  1>&2 echo "Error in determing os codename"
  exit 1
fi

if [[ -z "$my_codename" ]]; then
  1>&2 echo "ERROR in determining OS codename"
  exit 1
fi

if [[ ! "$my_int_version" =~ ^[0-9]+$ ]]; then
  1>&2 echo "ERROR: int version '$my_int_version' is not an int. We need a numeric string for proper debian-revision version comparison."
  exit 1
fi

# Sequence numbers makes sure that when one upgrades the OS, the package for
# the new distro version is selected.
sequence="$my_int_version"

if [[ -z "$sequence" ]]; then
  1>&2 echo "ERROR: no OS sequence defined for $my_codename"
  exit 2
fi

echo -n "${sequence}+${my_codename}+${EPOCHSECONDS}"
