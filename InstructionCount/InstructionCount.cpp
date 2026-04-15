#include "Expression.hpp"
#include "ICAnalyses.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Demangle/Demangle.h>
#include <llvm/IR/Analysis.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Mangler.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Pass.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/YAMLTraits.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <string>
#include <vector>

using namespace llvm;

namespace IC {

struct InstructionCount : PassInfoMixin<InstructionCount> {

  bool run_test(std::string test_name, std::string actual,
                std::string expected) {
    if (actual != expected) {
      errs() << test_name << " failed, expected: " << expected
             << "\n\tactual: " << actual << "\n";
      return false;
    }
    errs() << test_name << " passed\n";
    return true;
  }
  void tests() {
    // expressions

    // 0 constant
    {
      ExprHandle c0 = constant(0);
      std::string expected = "0";
      std::string actual = toString(c0);
      run_test("0 constant", actual, expected);
    }

    {
      ExprHandle c0 = constant(0);
      ExprHandle c1 = constant(1);
      std::string expected = "1";
      std::string actual = toString(add({c0, c1}));
      run_test("0 plus 1", actual, expected);
    }

    {
      ExprHandle v0 = var(0);
      ExprHandle c1 = constant(1);
      ExprHandle a1 = add({constant(1), v0});
      ExprHandle c2 = constant(2);
      ExprHandle a2 = add({a1, c2});
      run_test("nested addition", toString(a2), "3+n0");
      run_test("c1 unchanged", toString(c1), "1");
      run_test("a1 unchanged", toString(a1), "1+n0");
    }

    {
      ExprHandle m0 = mul({var(0), add({constant(1), var(1)})});
      run_test("distributive law and mult with 1", toString(m0), "n0+(n0*n1)");
      ExprHandle c21 = constant(21);
      ExprHandle c2 = constant(2);
      ExprHandle m1 = mul({m0, c21, c2});
      run_test("distributive, nested mult and constant propagation",
               toString(m1), "42n0+(42n0*n1)");
      run_test("mul 0", toString(mul({m1, constant(0)})), "0");
      run_test("m1 unchanged", toString(m1), "42n0+(42n0*n1)");
    }

    {
      ExprHandle n0 = var(0);
      ExprHandle n0p2 = var(0, 3, 2);
      ExprHandle m0 = mul({n0p2, n0});
      run_test("multiplying same variable", toString(m0), "3n0^3");
    }
    {
      Variable::latest_id = {};
      ExprHandle r0 = var(Variable::latest_id["r"]++, 1, 1, "r");
      ExprHandle n0 = var(Variable::latest_id["n"]++);

      ExprHandle m0 = mul({r0, n0});
      ExprHandle a0 = add({m0, n0});
      ExprHandle t0 = substituteRecursionVariables(r0);
      run_test("original variable unchanged after substitute 1", toString(r0),
               "r0");
      run_test("substituted variable", toString(t0), "n1");
      ExprHandle t1 = substituteRecursionVariables(m0);
      run_test("original variable unchanged after substitute 2", toString(r0),
               "r0");
      run_test("original multiplication unchanged after substitute",
               toString(m0), "r0*n0");
      run_test("substituted variable in multiplication", toString(t1), "n2*n0");
      ExprHandle t2 = substituteRecursionVariables(a0);
      run_test("original variable unchanged after substitute 3", toString(r0),
               "r0");
      run_test("original addition unchanged after substitute", toString(a0),
               "(r0*n0)+n0");
      run_test("substituted variable in addition", toString(t2), "(n3*n0)+n0");
    }
  }

  bool outputToCsv(Module &M, const CounterModuleAnalysis::Result &MR,
                   Config &config, const std::string &energy_model_name) {
    std::string output_str{};
    raw_string_ostream ostream{output_str};

    ostream << "Function Name,Demangled Name,fid,total";
    for (auto &inst : config.instructions_to_count) {
      ostream << "," << inst;
    }
    ostream << "\n";

    std::map<Function *, ExprHandle> total_costs;
    for (auto &[F, _] : MR.function_results) {
      total_costs[F] = constant(0);
    }
    for (auto &[F, FR] : MR.function_results) {
      for (auto &[_, cost] : FR.instruction_costs) {
        total_costs[F] = add({total_costs[F], cost});
      }
    }

    for (auto &[function, FR] : MR.function_results) {
      if (function->isDeclaration())
        continue;

      // Name
      const auto name = function->getName();
      ostream << name;
      ostream << ",\"" << demangle(name) << "\"";
      ostream << ",f" << FR.fid;
      ostream << "," << total_costs[function];

      for (auto &inst : config.instructions_to_count) {
        ExprHandle expr;
        if (FR.instruction_costs.count(inst)) {
          expr = mul({FR.instruction_costs.at(inst),
                      constant(config.energy_model[energy_model_name][inst])});
        } else {
          expr = constant(0);
        }
        ostream << "," << expr;
      }
      ostream << "\n";
    }

    std::filesystem::path file_path("./output");

    std::string output_filename;
    raw_string_ostream ofn(output_filename);
    std::filesystem::path source_file_path = M.getSourceFileName();
    ofn << source_file_path.filename() << "-" << M.getTargetTriple().getTriple()
        << "-" << energy_model_name << ".csv";

    auto icconfigdir_result = std::getenv("IC_OUTPUT_DIR");
    if (icconfigdir_result) {
      file_path = icconfigdir_result;
    }
    if (!std::filesystem::exists(file_path)) {
      if (!std::filesystem::create_directories(file_path)) {
        errs() << "Could not create parent directories of path " << file_path
               << "\n";
        return false;
      };
    }
    file_path /= output_filename;

    std::ofstream csv_file{};
    csv_file.open(file_path);
    if (!csv_file.is_open()) {
      errs() << "Error while trying to open output file at " << file_path
             << "\n";
      return false;
    }

    std::string str;
    raw_string_ostream stros{str};
    stros << M;

    csv_file << str << "\n";

    csv_file << output_str;
    csv_file.close();
    return true;
  }

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    // read config
    Config &config = MAM.getResult<ConfigReader>(M);
    if (!config.loaded) {
      errs() << "Exiting Pass Early\n";
      return PreservedAnalyses::all();
    }

    // run tests
    if (config.run_tests) {
      tests();
      exit(0);
    }

    auto &triple = M.getTargetTriple();
    if (!config.isTargetValid(triple)) {
      if (config.verbose)
        errs() << "Skipping non-device module\n";
      return PreservedAnalyses::all();
    }
    if (config.verbose) {
      errs() << "Analysing a Module with Target Triple: " << triple.getTriple()
             << "\n";
    }

    if (config.verbose)
      errs() << "Running analysis for Module " << M.getName() << "\n";

    // for (auto &F : M) {
    //   function_variable_ids[&F] = Variable::latest_id["f"]++;
    // }

    auto MR = MAM.getResult<CounterModuleAnalysis>(M);

    for (auto &[energy_model_name, _] : config.energy_model)
      if (!outputToCsv(M, MR, config, energy_model_name)) {
        errs() << "Exiting Pass Early\n";
        return PreservedAnalyses::all();
      }

    return PreservedAnalyses::all();
  }

  // static bool isRequired() { return true; }
};

void registerPassBuilderCallbacks(llvm::PassBuilder &PB) {
  PB.registerAnalysisRegistrationCallback(
      [](llvm::FunctionAnalysisManager &FAM) {
        FAM.registerPass([&] { return CounterFunctionAnalysis(); });
        FAM.registerPass([&] { return CountAggregationFunctionAnalysis(); });
      });
  PB.registerAnalysisRegistrationCallback([](llvm::ModuleAnalysisManager &MAM) {
    MAM.registerPass([&] { return CounterModuleAnalysis(); });
    MAM.registerPass([&] { return ConfigReader(); });
  });
  PB.registerPipelineParsingCallback(
      [](llvm::StringRef Name, llvm::ModulePassManager &MPM,
         ArrayRef<llvm::PassBuilder::PipelineElement>) {
        if (Name == "instruction-count") {
          MPM.addPass(InstructionCount());
          return true;
        }
        return false;
      });
  PB.registerOptimizerLastEPCallback([](llvm::ModulePassManager &MPM,
                                        llvm::OptimizationLevel Level,
                                        llvm::ThinOrFullLTOPhase TOF) {
    // FunctionPassManager FPM;
    // FPM.addPass(InstructionCount());

    if (TOF == llvm::ThinOrFullLTOPhase::None ||
        TOF == llvm::ThinOrFullLTOPhase::ThinLTOPostLink ||
        TOF == llvm::ThinOrFullLTOPhase::FullLTOPostLink) {
      MPM.addPass(InstructionCount());
    }
  });
}

} // namespace IC

/* New PM Registration */
llvm::PassPluginLibraryInfo getInstructionCountPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "InstructionCount", LLVM_VERSION_STRING,
          IC::registerPassBuilderCallbacks};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getInstructionCountPluginInfo();
}
