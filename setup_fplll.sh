#!/bin/bash

fplll_download_link="https://github.com/fplll/fplll/releases/download/5.4.5/fplll-5.4.5.tar.gz"

echo "Creating a temp directory to work in"
if [ ! -d tmp ]; then mkdir temp; fi
cd temp

echo "Downloading fplll"
if ! wget "$fplll_download_link" || [ ! -f fplll-5.4.5.tar.gz ]; then
    echo "Failed to download fplll"
    exit
fi

echo "Extracting files"
if ! tar -xf fplll-5.4.5.tar.gz; then
    echo "Failed to extract fplll"
    exit
fi

echo "Starting installation"
cd fplll-5.4.5
./configure && make && sudo make install

echo "Finished! fplll should now be installed as a library."
echo "Removing temp work directory"
cd ../..
rm -r ./temp
echo "Done!"