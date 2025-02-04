# fnv-hash-cracking
Crack hashes hashed with the [FNV-1a](https://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function#FNV-1a_hash) algorithm without full brute force

# CREDITS
Huge thank you to [ConnorM](https://connor-mccartney.github.io) for his [incredible writeup](https://connor-mccartney.github.io/cryptography/other/Trying-to-crack-COD-FNV-hashes) and writing the original python proof of concept of which this is based off of. He does some incredible work with cryptography and his writeups are worth a read.

# Requirements
This project requires the following libraries:<br/>
    - [mpfr](https://www.mpfr.org)<br/>
    - [gmp](https://gmplib.org)<br/>
    - [fplll](https://github.com/fplll/fplll)<br/>
    
NOTE: This project uses the fplll library which does *not* support Windows. If you are looking to use this on Windows, look into [WSL](https://learn.microsoft.com/en-us/windows/wsl/install).

# Usage
Run the following commands to get started
```bash
git clone https://github.com/Nico-Posada/fnv-hash-cracking.git
cd fnv-hash-cracking
```

If you do not have the fplll library installed, run the following:
## Ubuntu and Debian
```bash
sudo apt install fplll-tools
```

## Conda
```bash
conda install fplll
```

## MacOS
```bash
brew install fplll
```

or if you want to build it from source (will probably be better optimized for your machine), run the following
```bash
chmod +x scripts/setup.sh
sudo scripts/setup.sh
```

More information on compilation and installation specifics can be found in the [fplll repository](https://github.com/fplll/fplll?tab=readme-ov-file#compilation).

To compile [the test cases](src/main.cpp), run the following
```bash
make build
```
The binary will be output in the `build/` directory.

To compile and run the test script, run the following 
```bash
make run
```

To use in your own program, just copy over the `src` directory to your project directory and you can include it in your build script as normal. This uses header files only, so no need to make any object files beforehand.

# TODO
- fplll has docker containers. Figure out how to make this work in them and provide steps.
- Clean up code, add more tests and actual test outputs.
- Add multithreading
- Maybe add a python module for this?