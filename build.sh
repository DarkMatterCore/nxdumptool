#!/bin/bash

ARG=${1:-'--confirm'}

USE_EXIT_CONFIRMATION=$([ ${ARG,,} != "--noconfirm" ] && echo "true" || echo "false")
NUM_PROC=$([ $USE_EXIT_CONFIRMATION == "true" ] && echo $(nproc) || echo 8)

POC_NAME="nxdt_rw_poc"
POC_PATH="./code_templates/$POC_NAME.c"

pre_build_clean_up() {
    rm -rf ./code_templates/tmp
    mkdir ./code_templates/tmp

    mv ./source/main.cpp ./main.cpp

    >&2 make clean_all
}

post_build_clean_up() {
    >&2 make BUILD_TYPE="$POC_NAME" clean
    >&2 make clean_all

    rm -f ./source/main.c ./romfs/icon/$POC_NAME.jpg
    mv -f ./main.cpp ./source/main.cpp
}

build() {
    rm -f ./source/main.c
    cp $POC_PATH ./source/main.c
    cp ./romfs/icon/nxdumptool.jpg ./romfs/icon/$POC_NAME.jpg

    >&2 make BUILD_TYPE="$POC_NAME" -j$NUM_PROC
    local ret=$?

    if [[ $ret -eq 0 ]]; then
        mv -f ./$POC_NAME.nro ./code_templates/tmp/$POC_NAME.nro
        mv -f ./$POC_NAME.elf ./code_templates/tmp/$POC_NAME.elf
        echo 0
    else
        echo 1
    fi
}

bail_out() {
    if [ $USE_EXIT_CONFIRMATION == "true" ]; then
        read -rsp $'Press any key to continue...\n' -n 1 key
    fi
    exit $1
}

main() {
    # Make sure we're in the right directory
    cd "$(dirname "${BASH_SOURCE[0]}")"

    # Pre-build clean-up
    pre_build_clean_up

    # Build PoC
    local ret="$(build)"

    # Post build clean-up
    post_build_clean_up

    # Exit
    bail_out $ret
}

main
