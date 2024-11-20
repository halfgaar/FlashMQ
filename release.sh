#!/bin/bash

SEMVER_REGEXP="^([0-9]+)\.([0-9]+)\.([0-9]+)$"

SCRIPT_NAME=$(basename "$0")
SCRIPT_DIR="$(dirname "$(realpath "$(readlink -f "$0")")")"
CMAKEFILE="$SCRIPT_DIR/CMakeLists.txt"

usage() {
    echo -e "\e[1m$SCRIPT_NAME\e[22m – Tag a new release of \e[1;38;2;0;55;112;48;2;255;255;255m\e[39;49mFlashMQ\e[0m.

Usage:
    $SCRIPT_NAME <semver>
    $SCRIPT_NAME -h\e[2m|\e[22m--help

Options & arguments:
    <semver>       A semantic version number, like 1.0.12, conforming to
                   \e[4mhttps://semver.org/spec/v2.0.0.html\e[24m.
    -h\e[2m|\e[22m--help      Display this help.
"
}

usage_error() {
    echo -e "\e[31m$1\e[0m" >&2
    echo >&2
    usage >&2
    exit 2
}

while [[ -n "$1" ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -*)
            usage_error "Unknown option: \e[1m$1\e[22m"
            ;;
        *)
            semver="$1"
            shift
            if [[ -n "$1" ]]; then
                usage_error "Extraneous arguments after <semver> (\e[1m$semver\e[22m)."
            fi
            ;;
    esac
done

if [[ -z "$semver" ]]; then
    usage_error "Missing \e[1m<semver>\e[22m argument."
fi

if [[ ! "$semver" =~ $SEMVER_REGEXP ]]; then
    usage_error "\e[1m$semver\e[22m is not a recognizable \e[1msemver\e[22m; \$SEMVER_REGEXP = \e[1m$SEMVER_REGEXP\e[22m"
fi

cmd() {
    msg="$1"; shift
    shift
    echo -e "$msg: \e[1m$(printf '%q ' "$@")\e[22m" >&2
    "$@"
    local retval=$?
    if [[ "$retval" -eq 0 ]]; then
        echo -e "\e[32;1m✔ done\e[39;22m" >&2
    else
        echo -e "\e[31;1m❌failed with exit code \e[4m$retval\e[24m\e[39;22m" >&2
    fi
    echo >&2
    return "$retval"
}

if output=$(git status --porcelain) && [[ -n "$output" ]]; then
    1>&2 echo "Git not clean"
    exit 3
fi


git_tag="v$semver"
git_msg="Version $semver"

if git rev-parse "$git_tag" &>/dev/null; then
    1>&2 echo "Git tag seems already in use"
    exit 3
fi

if ! cmd "Setting project version" -- sed -i -E '/^project\(/s/^(project\([^ ]+ VERSION )[^ ]+( .*)$/\1'"$semver"'\2/' "$CMAKEFILE"; then
    exit 3
fi

if ! cmd "Staging changes to \e[1m$CMAKEFILE\e[22m" -- git add "$CMAKEFILE"; then
    exit 3
fi

if ! cmd "Committing release $semver" -- git commit -m "$git_msg"; then
    exit 3
fi

cmd "Tagging release $semver" -- git tag -a -m "$git_msg" "$git_tag"

# vim: set expandtab tabstop=4 shiftwidth=4:
