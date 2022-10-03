# A Tool for Fixing C/C++ Source Code Style Issues

This is a tool for enclosing single statements in braces (and other cosmetic fixes) in C and C++ code

## Command Line Options

```bash
$ ./code-format --help
OVERVIEW: This is a tool for enclosing single statements in braces (and other cosmetic fixes) in C and C++ code
USAGE: ./code-format file [-o <file>] [--fix-file-endings] [--fix-single-statement] [-h] [-V]
OPTIONS: 
    -o <file>               Output file name.
    --fix-file-endings      Change file ending to one new-line symbol.
    --fix-single-statement  Enclose single-statement blocks in brackets,
                            format `if`-`else if`-`else`-sequences.
    -h, --help              Display this information.
    -V, --version           Display version.
```

## How to Build `code-format`

Perform these steps to build the project (in linux, for other platforms the steps are similar):

1. Clone `code-format` repository and enter it

    ```bash
    git clone https://github.com/gbuzykin/code-format
    cd code-format
    ```

2. Initialize and update `uxs` submodule

    ```bash
    git submodule update --init
    ```

3. Then, compilation script should be created using `cmake` tool.  To use the default C++ compiler
   just issue (for new enough version of `cmake`)

    ```bash
    cmake -S . -B build
    ```

    or to make building scripts for debug or optimized configurations issue the following

    ```bash
    cmake -S . -B build -DCMAKE_BUILD_TYPE="Debug"
    ```

    or

    ```bash
    cmake -S . -B build -DCMAKE_BUILD_TYPE="Release"
    ```

4. Enter created folder `build` and run `make`

    ```bash
    cd build
    make
    ```

    to use several parallel processes (e.g. 8) for building run `make` with `-j` key

    ```bash
    make -j 8
    ```
