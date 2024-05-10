#!/bin/bash

function setup() {
    if [[ $# -ne 3 ]]; then
        echo "setup must have 3 arguments"
        return 1
    fi

    local DOWNLOAD_URL="$1"
    local FILENAME="$2"
    local TMP_DIR="$3"
    local FILENAME_WITH_EXT="${DOWNLOAD_URL##*/}"

    echo "Creating a temp directory to work in"
    if [ ! -d "$TMP_DIR" ]; then mkdir "$TMP_DIR"; fi
    cd "$TMP_DIR"

    echo "Downloading $FILENAME"
    if ! wget "$DOWNLOAD_URL" || [ ! -f "$FILENAME_WITH_EXT" ]; then
        echo "Failed to download $FILENAME"
        return 1
    fi

    echo "Extracting files"
    if ! tar -xf "$FILENAME_WITH_EXT"; then
        echo "Failed to extract $FILENAME"
        return 1
    fi

    echo "Starting installation"
    cd "$FILENAME"

    if ! ./configure; then
        echo "Failed to configure"
        return 1
    fi

    if ! make; then
        echo "Failed to make"
        return 1
    fi

    if ! make check; then
        echo "Failed the make check"
        return 1
    fi

    if ! sudo make install; then
        echo "Failed to make install"
        return 1
    fi

    echo "Finished! $FILENAME should now be installed as a library."
    echo "Removing temp work directory"
    cd ../..
    rm -r "$TMP_DIR"
    echo "Done!"
    return 0
}

function fail_with_msg() {
    echo "$1"
    exit 1
}

TMP_DIR="tmp-mpfr"
if ! setup "https://www.mpfr.org/mpfr-current/mpfr-4.2.1.tar.xz" "mpfr-4.2.1" "$TMP_DIR" && [[ -d "$TMP_DIR" ]]; then
    rm -r "$TMP_DIR"
    fail_with_msg "Failed to install MPFR"
fi