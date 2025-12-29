#!/bin/bash

if [ ! -d "./build" ]; then
    mkdir ./build
else
    rm -rf ./build
    mkdir ./build
fi

printf "execution options: %s\n" "$1"
case "$1" in
    "driver")
        ;;
    "kernel")
        ;;
    "test")
        cd ./build || exit 36
        cmake ../test/
        # shellcheck disable=SC2154
        make -j"${nproc}"
        ;;
    "x86asm_att")
        ;;
    *)
        printf "none exec\n"
        ;;
esac

printf "exec end\n"
