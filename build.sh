#!/bin/bash
ARG=${1:-'--confirm'}

cd "$(dirname "${BASH_SOURCE[0]}")"

# Clean-up from last build
rm -rf ./code_templates/tmp
mkdir ./code_templates/tmp

mv ./source/main.cpp ./main.cpp

make clean_all

# Build PoC
poc_name="nxdt_rw_poc"
poc_path="./code_templates/$poc_name.c"

rm -f ./source/main.c
cp $poc_path ./source/main.c

cp ./romfs/icon/nxdumptool.jpg ./romfs/icon/$poc_name.jpg

make BUILD_TYPE="$poc_name" -j$(nproc)

rm -f ./romfs/icon/$poc_name.jpg

mv -f ./$poc_name.nro ./code_templates/tmp/$poc_name.nro
mv -f ./$poc_name.elf ./code_templates/tmp/$poc_name.elf

# Post build clean-up
make BUILD_TYPE="$poc_name" clean
make clean_all

rm -f ./source/main.c
mv -f ./main.cpp ./source/main.cpp

if [ ${ARG,,} != "--noconfirm" ]; then
    read -rsp $'Press any key to continue...\n' -n 1 key
fi
