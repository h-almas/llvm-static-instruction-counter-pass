#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <fstream>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/Analysis.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <map>
#include <vector>

using namespace llvm;

namespace {

// this ensures that the subscript operator is not adding an entry to the map
template <typename T>
void addOrSetEntry(std::map<T, std::size_t> &map, T key, std::size_t value) {
  if (map.count(key)) {
    map[key] += value;
  } else {
    map[key] = value;
  }
}

const std::map<std::string, std::size_t> energy_model{
    {"alloca", 3}, {"load", 6}, {"mul", 10},
    {"fmul", 15},  {"add", 2},  {"fadd", 5}};

struct ECFunctionAnalysis : public AnalysisInfoMixin<ECFunctionAnalysis> {
  struct Result {
    const Function *function;
    std::map<std::string, std::size_t> instruction_counts{};
    std::map<std::string, std::size_t> energy_per_instruction_type{};
    std::vector<Function *> outgoing_calls{};
    std::vector<Function *> outgoing_invokes{};

    std::size_t get_total_energy_consumption() {
      std::size_t sum{};
      for (auto &p : energy_per_instruction_type) {
        sum += p.second;
      }
      return sum;
    }
  };

  Result run(Function &F, FunctionAnalysisManager &FAM) {
    Result result;
    result.function = &F;
    getInstructionCounts(F, FAM, result);

    for (auto &[k, _] : energy_model) {
      result.energy_per_instruction_type[k] =
          (result.instruction_counts.count(k) ? result.instruction_counts[k]
                                              : 0) *
          energy_model.at(k);
    }

    return result;
  }
  static AnalysisKey Key;

  void getInstructionCounts(Function &F, FunctionAnalysisManager &FAM,
                            Result &result) {
    std::string output{};
    raw_string_ostream ostream{output};

    std::map<std::string, std::size_t> inst_counts{};

    // errs() << "Counting function " << demangle(F.getName()) << "\n";

    // ostream << "Instructions:\n";
    for (auto &bb : F) {
      for (auto &inst : bb) {
        std::string opcode_name = std::string{inst.getOpcodeName()};
        Function *called_F = nullptr;
        if (isa<CallInst>(inst)) {
          called_F = cast<CallInst>(inst).getCalledFunction();
          result.outgoing_calls.push_back(called_F);
        } else if (isa<InvokeInst>(inst)) {
          called_F = cast<InvokeInst>(inst).getCalledFunction();
          result.outgoing_invokes.push_back(called_F);
        }

        /*
        if (called_F != nullptr) {
          if (called_F == &F) {
            errs() << "A function is trying is calling itself recursively! "
                      "Results will be wrong.\n";
          } else if (!called_F->isDeclaration()) {
            // errs() << "Following function call to " << called_F->getName()
            //        << " from function " << F.getName() << "\n";
            auto called_inst_counts =
                FAM.getResult<InstructionCountModuleAnalysis>(*called_F)
                    .instruction_counts;
            // errs() << "Finished function call to "
            //        << demangle(called_F->getName()) << " from function "
            //        << demangle(F.getName()) << "\n";
            for (auto &[k, v] : called_inst_counts) {
              if (inst_counts.count(k)) {
                // auto prev = inst_counts.at(k);
                result.instruction_counts[k] += v;
                // errs() << "added " << v << " to " << k << ". Count was "
                <<
                // prev
                //        << ". Count is at: " << inst_counts.at(k) << "\n";
              } else {
                result.instruction_counts[k] = v;
                // errs() << "added " << v << " to " << k
                //        << ". Count is at: " << inst_counts.at(k) << "\n";
              }
            }
          }
        }
        */

        addOrSetEntry(result.instruction_counts, opcode_name, 1);
      }

      // for (auto &entry : instruction_map) {
      //   ostream << " " << entry.first << ": " << entry.second.size() << "\n";
      // }
    }
  }
};

struct ECAccumulationFunctionAnalysis
    : public AnalysisInfoMixin<ECAccumulationFunctionAnalysis> {
  using Result = ECFunctionAnalysis::Result;

  ECFunctionAnalysis::Result
  getECFunctionAnalysisResult(Function *F, FunctionAnalysisManager &FAM) {
    ECFunctionAnalysis::Result *result_ptr =
        FAM.getCachedResult<ECFunctionAnalysis>(*F);
    if (!result_ptr) {
      errs() << "There was no cached result for Function: " << F->getName()
             << "!\n";
      return ECFunctionAnalysis::Result();
    }
    return *result_ptr;
  }

  Result run(Function &F, FunctionAnalysisManager &FAM) {
    Result prev_result = getECFunctionAnalysisResult(&F, FAM);

    for (auto *called_F : prev_result.outgoing_calls) {
      if (called_F == &F) {
        errs() << "Skipping a recursion at Function: " << called_F->getName()
               << "\n";
        continue;
      }
      Result called_F_result =
          FAM.getResult<ECAccumulationFunctionAnalysis>(*called_F);
      for (auto &[k, v] : called_F_result.instruction_counts) {
        prev_result.instruction_counts[k] += v;
      }
    }
    for (auto *invoked_F : prev_result.outgoing_invokes) {
      if (invoked_F == &F) {
        errs() << "Skipping a recursion at Function: " << invoked_F->getName()
               << "\n";
        continue;
      }
      Result invoked_F_result =
          FAM.getResult<ECAccumulationFunctionAnalysis>(*invoked_F);
      for (auto &[k, v] : invoked_F_result.instruction_counts) {
        prev_result.instruction_counts[k] += v;
      }
    }

    return prev_result;
  }
  static AnalysisKey Key;
};

struct ECModuleAnalysis : public AnalysisInfoMixin<ECModuleAnalysis> {
  struct Result {
    std::map<Function *, ECFunctionAnalysis::Result> function_results{};
  };

  Result run(Module &M, ModuleAnalysisManager &MAM) {

    // follow the calls and add
    Result result;
    FunctionAnalysisManager &FAM =
        MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

    for (auto &F : M) {
      result.function_results[&F] = FAM.getResult<ECFunctionAnalysis>(F);
    }

    // ideally I should be building a call graph
    // Then removing all cycles by grouping them as special nodes
    // And then I can run my analysis on those graph nodes

    for (auto &F : M) {
      result.function_results[&F] =
          FAM.getResult<ECAccumulationFunctionAnalysis>(F);
    }

    // auto called_inst_counts =
    //     FAM.getResult<InstructionCountModuleAnalysis>(*called_F)
    //         .instruction_counts;
    // // errs() << "Finished function call to "
    // //        << demangle(called_F->getName()) << " from function "
    // //        << demangle(F.getName()) << "\n";
    // for (auto &[k, v] : called_inst_counts) {
    //   if (inst_counts.count(k)) {
    //     // auto prev = inst_counts.at(k);
    //     result.instruction_counts[k] += v;
    //     // errs() << "added " << v << " to " << k << ". Count was "
    //     <<
    //     // prev
    //     //        << ". Count is at: " << inst_counts.at(k) << "\n";
    //   } else {
    //     result.instruction_counts[k] = v;
    //     // errs() << "added " << v << " to " << k
    //     //        << ". Count is at: " << inst_counts.at(k) << "\n";
    //   }
    // }

    // for (auto &[function, FR] : result.function_results) {
    //   errs() << function->getName() << ":\n";
    //
    //   if (!FR.outgoing_calls.empty()) {
    //     errs() << "calls: ";
    //     for (auto F : FR.outgoing_calls) {
    //       errs() << F->getName() << ", ";
    //     }
    //     errs() << "\n";
    //   }
    //   if (!FR.outgoing_invokes.empty()) {
    //     errs() << "invokes: ";
    //     for (auto F : FR.outgoing_invokes) {
    //       errs() << F->getName() << ",";
    //     }
    //     errs() << "\n";
    //   }
    // }

    return result;
  }
  static AnalysisKey Key;
};

AnalysisKey ECFunctionAnalysis::Key;
AnalysisKey ECAccumulationFunctionAnalysis::Key;
AnalysisKey ECModuleAnalysis::Key;

struct InstructionCount : PassInfoMixin<InstructionCount> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    // getInstructionCounts(F);

    errs() << "-- Function Pass!" << "\n";
    errs() << "This pass currently does nothing" << "\n";
    return PreservedAnalyses::all();
  }

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {

    // errs() << "Running analysis for Module " << M.getName() << "\n";
    std::ofstream file{};

    std::string output_str{};
    raw_string_ostream ostream{output_str};

    std::vector<std::string> insts_to_record{
        "alloca", "load", "store", "getelementptr", "call",
        "mul",    "add",  "fmul",  "fadd",
    };

    ostream << "Function Name, Demangled Name";
    for (auto &inst : insts_to_record) {
      ostream << "," << inst;
    }
    ostream << "\n";

    auto &MR = MAM.getResult<ECModuleAnalysis>(M);

    for (auto &[function, FR] : MR.function_results) {
      // errs() << F.getName() << "\n";
      if (function->isDeclaration())
        continue;

      /*
      LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);

      if (!LI.empty()) {
        ScalarEvolution &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);

        errs() << "For Function " << F.getName() << "\n";
        errs() << "Loops:\n";
        for (auto &L : LI) {
          errs() << L->getName() << "\n";
          errs() << "Is rotated: " << (L->isRotatedForm() ? "true" : "false")
                 << "\n";
          if (auto bounds = L->getBounds(SE)) {
            errs() << "Initial Value: " << bounds->getInitialIVValue() <<
            "\n"; errs() << "Step Value: " << bounds->getStepValue() << "\n";
            errs() << "Final Value: " << bounds->getFinalIVValue() << "\n";
            errs() << "Direction "
                   << (bounds->getDirection() ==
                               Loop::LoopBounds::Direction::Increasing
                           ? "Increasing"
                           : "Decreasing")
                   << "\n";
          } else {
            errs() << "Could not determine loop bounds\n";
          }
        }
        errs() << "Loops end\n";
      }
      */

      // Name
      const auto name = FR.function->getName();
      ostream << name;
      const auto demangled_name = demangle(name);
      // if (name != demangled_name) {
      ostream << ",\"" << demangled_name << "\"";

      for (auto &inst : insts_to_record) {
        if (FR.instruction_counts.count(inst)) {

          ostream << ",(" << FR.instruction_counts.at(inst) << ":";
          if (FR.energy_per_instruction_type.count(inst)) {
            ostream << FR.energy_per_instruction_type.at(inst) << ")";
          } else {
            ostream << "0)";
          }
        } else {
          ostream << ",(0:0)";
        }
      }
      ostream << "\n";
    }

    // errs() << "opening file\n";
    file.open("./output.csv");
    file << output_str << "\n";
    file.close();
    // errs() << "closed file\n";

    return PreservedAnalyses::all();
  }

  // static bool isRequired() { return true; }
};

void registerPassBuilderCallbacks(PassBuilder &PB) {
  PB.registerAnalysisRegistrationCallback([](FunctionAnalysisManager &FAM) {
    FAM.registerPass([&] { return ECFunctionAnalysis(); });
    FAM.registerPass([&] { return ECAccumulationFunctionAnalysis(); });
  });
  PB.registerAnalysisRegistrationCallback([](ModuleAnalysisManager &MAM) {
    MAM.registerPass([&] { return ECModuleAnalysis(); });
  });
  PB.registerPipelineParsingCallback(
      [](StringRef Name, llvm::FunctionPassManager &FPM,
         ArrayRef<llvm::PassBuilder::PipelineElement>) {
        if (Name == "instruction-count") {
          FPM.addPass(InstructionCount());
          return true;
        }
        return false;
      });
  PB.registerPipelineStartEPCallback(
      [](ModulePassManager &MPM, OptimizationLevel Level) {
        // FunctionPassManager FPM;
        // FPM.addPass(InstructionCount());

        MPM.addPass(InstructionCount());
        // MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
      });
}

} // namespace

/* New PM Registration */
llvm::PassPluginLibraryInfo getInstructionCountPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "InstructionCount", LLVM_VERSION_STRING,
          registerPassBuilderCallbacks};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getInstructionCountPluginInfo();
}
