# llvm-passes

## How to build
In order to build all passes run `cmake -B build -DLLVM_INSTALL_DIR=<llvm-directory>` and then `cmake --build build`. This project is meant to be used with LLVM version 21.

## How to use
To run a passes from a pass plugin on an example.ll file:
```bash
opt -load-pass-plugin=<path-to-pass-plugin.so> -passes='pass1;pass2' -disable-output example.ll 
```

## Pass Plugins
- InstructionCount: This plugin has an 'instruction-count' pass which currently outputs the count of each instruction type it encounters (as well as some additional information on each function)
