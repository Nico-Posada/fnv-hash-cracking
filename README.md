# fnv-hash-cracking
Crack hashes or find collisions for hashes hashed with the [FNV-1a](https://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function#FNV-1a_hash) algorithm without full brute force

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

or if you want to build it from source (will probably be better optimized for your machine), run the following
```bash
chmod +x scripts/setup.sh
sudo scripts/setup.sh
```

## Conda
```bash
conda install fplll
```

## MacOS
```bash
brew install fplll
```

More information on compilation and installation specifics can be found in the [fplll repository](https://github.com/fplll/fplll?tab=readme-ov-file#compilation).

# Examples
To find examples for different use cases, check out the [examples](./examples/) directory for some files with basic examples.

# Tests and Benchmarks (WIP)
To compile and run the test script, run the following 
```bash
make run-test
```

To compile and run the benchmark script, run the following 
```bash
make run-benchmark
```

# TODO
- Clean up code, add more tests
- Add multithreading support back in.
- Maybe add a python module for this?
- Benchmarks (WIP)
- Add support for setting the hash/prime/bit len dynamically to make this into a proper CLI
    * If this happens, maybe add JIT compilation for some speedup?