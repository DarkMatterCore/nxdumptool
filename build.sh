#!/bin/bash
cd "$(dirname "${BASH_SOURCE[0]}")"

tar_filename="nxdumptool-rewrite_poc_$(git rev-parse --short HEAD).tar.bz2"

rm -f ./*.tar.bz2

rm -rf ./code_templates/tmp
mkdir ./code_templates/tmp

make clean

for f in ./code_templates/*.c; do
    basename="$(basename "$f")"
    filename="${basename%.*}"
    
    if [[ $filename == "dump_title_infos" ]]; then
        continue
    fi
    
    echo $filename
    
    rm -f ./source/main.c
    cp $f ./source/main.c
    
    make BUILD_TYPE="$filename" -j$(nproc)
    
    mkdir ./code_templates/tmp/$filename
    cp ./$filename.nro ./code_templates/tmp/$filename/nxdumptool-rewrite.nro
    #cp ./$filename.elf ./code_templates/tmp/$filename/nxdumptool-rewrite.elf
    
    rm -f ./build/main.o ./build/main.d ./build/utils.o ./build/utils.d ./build/usb.o ./build/usb.d ./$filename.*
done

make clean

cd ./code_templates/tmp
tar -cjf ../../$tar_filename *

cd ../..
rm -f ./source/main.c
rm -rf ./code_templates/tmp

read -p "Press any key to finish ..."
