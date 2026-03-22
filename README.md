# llvm-passes

## How to build

In order to build all passes run `cmake -B build -DLLVM_INSTALL_DIR=<llvm-directory>` and then `cmake --build build`. This project is meant to be used with LLVM version 21. If there are compile errors, it could be that you need to add `-DLLVM_DIR=<llvm-cmake-directory>` to directly specify the LLVM directory that contains the LLVM CMake file. You should be able to find this at `<llvm-directory>/lib/cmake`. The reason this might be necessary is that CMake can sometimes find a file from e.g. rocm's LLVM version.

## How to use

To run a passes from a pass plugin on an example.ll file:

```bash
opt -load-pass-plugin=<path-to-pass-plugin.so> -passes='pass1;pass2' -disable-output example.ll
```

To run a pass directly with clang:

```bash
clang++ -fpass-plugin=<path-to-pass-plugin.so> -Xclang -disable-O0-optnone example.cpp
```

## Pass Plugins

- InstructionCount: This plugin has an 'instruction-count' pass which currently outputs the count of each instruction per llvm function. The output is a .csv file, per Module, named after the Module's source file and target-triple. The pass can be configured with a 'config.yaml' and can choose an energy model from and 'energy_models' directory. Configuration options of the 'config.yaml' file are:
  - instructions_to_count (takes a sequence of instruction names)
  - energy_model_name (takes a file name of a file in the 'energy_models' directory)
  - verbose (takes a bool value)
  - run_tests (takes a bool value and runs tests for the Expression code)
