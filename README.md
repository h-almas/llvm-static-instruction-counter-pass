# llvm-passes

## How to use
This project is meant to be added to the llvm-project/llvm/examples/ directory of the llvm-project and be built together with it. It will generate a pass plugin for each subdirectory added to CMakeLists.txt. This directory also has to be added to the CMakeLists.txt of the llvm-project/llvm/examples/CMakeLists.txt file.\
To run a passes from a pass plugin on an example.ll file:
```bash
opt -load-pass-plugin=<path-to-pass-plugin.so> -passes='pass1;pass2' -disable-output example.ll 
```

## Passes
- InstructionCount: This plugin has an 'instruction-count' pass which currently outputs the count of each instruction type it encounters (as well as some additional information on each function)
