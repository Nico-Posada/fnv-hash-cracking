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

If you do not have the fplll library installed, run the following
```bash
chmod +x scripts/setup.sh
sudo scripts/setup.sh
```

To compile [the sample script](src/main.cpp), run the following
```bash
make build
```
The binary will be output in the `build/` directory.

To compile and run the sample script, run the following 
```bash
make run
```