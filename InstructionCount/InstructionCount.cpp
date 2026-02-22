#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <fstream>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/Analysis.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <map>
#include <vector>

using namespace llvm;

namespace {
const std::map<std::string, std::size_t> energy_model{
    {"alloca", 3}, {"load", 6}, {"mul", 10},
    {"fmul", 15},  {"add", 2},  {"fadd", 5}};

struct FunctionInstructionCount {
  const Function *F;
  std::map<std::string, std::size_t> instruction_counts{};
  std::map<std::string, std::size_t> energy_per_instruction_type{};

  std::size_t get_total_energy_consumption() {
    std::size_t sum{};
    for (auto &p : energy_per_instruction_type) {
      sum += p.second;
    }
    return sum;
  }
};

struct InstructionCountModuleAnalysis
    : public AnalysisInfoMixin<InstructionCountModuleAnalysis> {
  using Result = FunctionInstructionCount;

  Result run(Function &F, FunctionAnalysisManager &FAM) {
    Result result;
    result.F = &F;
    result.instruction_counts = getInstructionCounts(F, FAM);
    for (auto &[k, _] : energy_model) {
      result.energy_per_instruction_type[k] =
          (result.instruction_counts.count(k) ? result.instruction_counts[k]
                                              : 0) *
          energy_model.at(k);
    }
    return result;
  }
  static AnalysisKey Key;

  std::map<std::string, std::size_t>
  getInstructionCounts(Function &F, FunctionAnalysisManager &FAM) {

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
        } else if (isa<InvokeInst>(inst)) {
          called_F = cast<InvokeInst>(inst).getCalledFunction();
        }
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
                inst_counts[k] += v;
                // errs() << "added " << v << " to " << k << ". Count was " <<
                // prev
                //        << ". Count is at: " << inst_counts.at(k) << "\n";
              } else {
                inst_counts[k] = v;
                // errs() << "added " << v << " to " << k
                //        << ". Count is at: " << inst_counts.at(k) << "\n";
              }
            }
          }
        }

        if (inst_counts.count(opcode_name)) {
          // unsigned long prev = inst_counts.at(opcode_name);
          inst_counts[opcode_name]++;
          // errs() << "added 1 to " << opcode_name << ". Count was " << prev
          //        << ". Count is at: " << inst_counts.at(opcode_name) << "\n";
        } else {
          inst_counts[opcode_name] = 1;
          // errs() << "added 1 to " << opcode_name
          //        << ". Count is at: " << inst_counts.at(opcode_name) << "\n";
        }
        // oss << "  " << opcode_name << ": " << inst << "\n";
      }

      // for (auto &entry : instruction_map) {
      //   ostream << " " << entry.first << ": " << entry.second.size() << "\n";
      // }
    }

    return inst_counts;
  }
};

AnalysisKey InstructionCountModuleAnalysis::Key;

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

    FunctionAnalysisManager &FAM =
        MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

    ostream << "Function Name, Demangled Name";
    for (auto &inst : insts_to_record) {
      ostream << "," << inst;
    }
    ostream << "\n";

    for (auto &F : M) {
      // errs() << F.getName() << "\n";
      if (F.isDeclaration())
        continue;

      // LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
      //
      // if (!LI.empty()) {
      //   ScalarEvolution &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
      //
      //   errs() << "For Function " << F.getName() << "\n";
      //   errs() << "Loops:\n";
      //   for (auto &L : LI) {
      //     errs() << L->getName() << "\n";
      //     errs() << "Is rotated: " << (L->isRotatedForm() ? "true" : "false")
      //            << "\n";
      //     if (auto bounds = L->getBounds(SE)) {
      //       errs() << "Initial Value: " << bounds->getInitialIVValue() <<
      //       "\n"; errs() << "Step Value: " << bounds->getStepValue() << "\n";
      //       errs() << "Final Value: " << bounds->getFinalIVValue() << "\n";
      //       errs() << "Direction "
      //              << (bounds->getDirection() ==
      //                          Loop::LoopBounds::Direction::Increasing
      //                      ? "Increasing"
      //                      : "Decreasing")
      //              << "\n";
      //     } else {
      //       errs() << "Could not determine loop bounds\n";
      //     }
      //   }
      //   errs() << "Loops end\n";
      // }

      // Name
      const auto name = F.getName();
      ostream << name;
      const auto demangled_name = demangle(name);
      // if (name != demangled_name) {
      ostream << ",\"" << demangled_name << "\"";
      // }
      // Args
      // if (F.arg_size() > 0) {
      //   ostream << "Args:";
      //   for (auto &arg : F.args()) {
      //     ostream << " " << arg;
      //   }
      // }

      // Instructions
      // errs() << "Getting Instruction counts" << "\n";
      auto &function_inst_count =
          FAM.getResult<InstructionCountModuleAnalysis>(F);
      // errs() << "Got Instruction counts" << "\n";

      for (auto &inst : insts_to_record) {
        if (function_inst_count.instruction_counts.count(inst)) {

          ostream << ",(" << function_inst_count.instruction_counts.at(inst)
                  << ":";
          if (function_inst_count.energy_per_instruction_type.count(inst)) {
            ostream << function_inst_count.energy_per_instruction_type.at(inst)
                    << ")";
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
    FAM.registerPass([&] { return InstructionCountModuleAnalysis(); });
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
