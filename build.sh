#!/bin/bash
cd "$(dirname "${BASH_SOURCE[0]}")"

tar_filename="nxdumptool-rewrite_poc_$(shell git rev-parse --short HEAD).tar.bz2"

rm -f ./*.tar.bz2

rm -rf ./code_templates/tmp
mkdir ./code_templates/tmp

for f in ./code_templates/*.c; do
    basename="$(basename "$f")"
    filename="${basename%.*}"
    
    if [[ $filename == "dump_title_infos" ]]; then
        continue
    fi
    
    echo $filename
    
    rm -f ./source/main.c
    cp $f ./source/main.c
    
    make BUILD_TYPE="$filename" clean
    make BUILD_TYPE="$filename" -j 12
    
    mkdir ./code_templates/tmp/$filename
    cp ./$filename.nro ./code_templates/tmp/$filename/nxdumptool-rewrite.nro
    #cp ./$filename.elf ./code_templates/tmp/$filename/nxdumptool-rewrite.elf
    
    make BUILD_TYPE="$filename" clean
done

cd ./code_templates/tmp
tar -cjf ../../$tar_filename *

cd ../..
rm -f ./source/main.c
rm -rf ./code_templates/tmp

read -p "Press any key to finish ..."
