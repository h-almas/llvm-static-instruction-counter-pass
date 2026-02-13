#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <fstream>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <map>
#include <sstream>
#include <vector>

using namespace llvm;

namespace {

std::map<std::string, std::size_t> getInstructionCounts(Function &F) {

  std::string output{};
  raw_string_ostream ostream{output};

  auto map =
      std::map<std::string,
               std::vector<SmallVector<std::pair<unsigned int, MDNode *>>>>{};
  std::map<std::string, std::size_t> inst_counts{};

  if (F.getInstructionCount() > 0) {
    // ostream << "Instructions:\n";
    for (auto &bb : F) {
      for (auto &inst : bb) {
        std::string opcode_name = std::string{inst.getOpcodeName()};
        auto count = map.count(opcode_name);
        SmallVector<std::pair<unsigned int, MDNode *>> MDs;
        inst.getAllMetadata(MDs);
        if (count == 0) {
          map[opcode_name] = {MDs};
        } else {
          map[opcode_name].push_back(MDs);
          inst_counts[opcode_name] = map[opcode_name].size();
        }
        // oss << "  " << opcode_name << ": " << inst << "\n";
      }
    }

    // for (auto &entry : instruction_map) {
    //   ostream << " " << entry.first << ": " << entry.second.size() << "\n";
    // }
  }

  return inst_counts;
}

struct InstructionCount : PassInfoMixin<InstructionCount> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    getInstructionCounts(F);

    errs() << "-- Function Pass!" << "\n";
    return PreservedAnalyses::all();
  }

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {

    std::ofstream file{};

    std::string output_str{};
    raw_string_ostream ostream{output_str};

    std::vector<std::string> insts_to_record{
        "alloca", "load", "store", "getelementptr", "call", "mul", "add",
    };

    ostream << "Function Name, Demangled Name";
    for (auto &inst : insts_to_record) {
      ostream << "," << inst;
    }
    ostream << "\n";

    for (auto &F : M) {
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
      std::map<std::string, std::size_t> instruction_counts =
          getInstructionCounts(F);

      for (auto &inst : insts_to_record) {
        ostream << "," << instruction_counts[inst];
      }
      ostream << "\n";
    }

    file.open("./output.csv");
    file << output_str << "\n";
    file.close();

    return PreservedAnalyses::all();
  }

  // static bool isRequired() { return true; }
};

void registerPassBuilderCallbacks(PassBuilder &PB) {
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
