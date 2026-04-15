# LLVM Static Instruction Counter Pass
This is a static analysis pass for LLVM Intermediate Representation (IR), which can estimate the instruction counts of an IR program.
It is meant to be used with the AdaptiveCpp's acpp compiler, compiled against LLVM version 21. 

## How to build

In order to build all passes run `cmake -B build -DLLVM_INSTALL_DIR=<llvm-directory>` and then `cmake --build build` in the git repository's root. This project is meant to be used with LLVM version 21.
## How to use

To run the pass plugin on an example.ll file with opt:

```bash
opt -load-pass-plugin=<path-to-pass-plugin.so> -passes='instruction-count' -disable-output example.ll
```

To run the pass directly with acpp, make sure to explicitly specify the targets in the following way or through the environment variable:

```bash
acpp -fpass-plugin=<path-to-pass-plugin.so> example.cpp --acpp-targets="<targets>"
```

## Configuration Options
The pass expects a config.yaml file and an energy_models/ directory, like the ones in the examples/ directory.
You can specifiy the directory of the config and energy_models directory, by setting the IC_CONFIG_DIR environment variable. You can also specify an output directory with IC_OUTPUT_DIR for the output results.
Make sure to use absolute paths. If you are in the root of the git repo, set them the environment variables in the following way:
```bash
export IC_CONFIG_DIR=$(pwd)/examples
export IC_OUTPUT_DIR=$(pwd)/examples/output
```
The config.yaml file in the examples/ directory can have the following options

- instructions_to_count: Takes a list of instruction names that are to be considered when running
the pass and which will be visible in the output file. This way users can choose which instructions
they are interested in analyzing or don’t want to see in the output file. We also allow specifying
symbol/function names, which the analysis pass counts as "special instructions". This way users
can also analyze for the usage of LLVM intrinsic functions.
- energy_model_names: This option takes a list of names of energy model files under the energy_models
/ directory. This allows the user to choose multiple different energy_models and the pass will create
a separate output file for each specified energy model.
- targets_allowed: Takes a list of targets that are checked during Target-Filtering. Currently available
options are GPU, NVPTX, AMDGPU, SPIR-V, SPIR, DXIL, CPU, RISCV, RISCV32, RISCV64, ARM,
x86. Options such as GPU, CPU, RISCV encompass more than one target architecture.
- aggregation_level: Can be set to either of the three different values fid, constants and all. It
controls the degree of aggregation in the Count Aggregation Function Analysis. Setting it
to fid, will mean every called function will be referenced with an id, such as f0 for the function with
id 0, in the resulting output files of the pass. Setting it to constants will only aggregate a called
function’s counts, when they are a constant, such as 3, while more complex instruction count’s, such
as 2n0, will be referred to by a function id, like with the fid option. Setting the option to all means
all called function’s instruction counts will be aggregated and no fid’s will be used. It is set to fid,
by default.
- verbose: If set to true, outputs additional debugging information. It is set to false, by default.
- run_tests: Used for internal tests of the Expression.cpp and Expression.hpp files 
