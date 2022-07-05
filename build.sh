#!/bin/bash
cd "$(dirname "${BASH_SOURCE[0]}")"

archive_filename="nxdumptool-rewrite_poc_$(git rev-parse --short HEAD)"

# Clean-up from last build
rm -f ./*.7z

rm -rf ./code_templates/tmp
mkdir ./code_templates/tmp

mv ./source/main.cpp ./main.cpp

make clean_all

# Build loop
for f in ./code_templates/*.c; do
    basename="$(basename "$f")"
    filename="${basename%.*}"

    if [[ $filename == "dump_title_infos" ]]; then
        continue
    fi

    echo $filename

    rm -f ./source/main.c
    cp $f ./source/main.c

    cp ./romfs/icon/nxdumptool.jpg ./romfs/icon/$filename.jpg

    make BUILD_TYPE="$filename" -j$(nproc)

    rm -f ./romfs/icon/$filename.jpg

    mkdir ./code_templates/tmp/$filename
    cp ./$filename.nro ./code_templates/tmp/$filename/$filename.nro
    cp ./$filename.elf ./code_templates/tmp/$filename/$filename.elf

    make BUILD_TYPE="$filename" clean
done

# Post build clean-up
make clean_all

# Package resulting binaries
cd ./code_templates/tmp
7z a ../../"$archive_filename.7z" */*.nro
7z a ../../"$archive_filename-Debug_ELFs.7z" */*.elf

# Final clean-up
cd ../..
rm -f ./source/main.c
rm -rf ./code_templates/tmp
mv ./main.cpp ./source/main.cpp
