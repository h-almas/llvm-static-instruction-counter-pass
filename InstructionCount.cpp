#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
#include <vector>

using namespace llvm;

namespace {

void perFunction(Function &F) {
  const auto name = F.getName();
  errs() << "Function: " << name << "\n";
  const auto demangled_name = demangle(name);
  if (name != demangled_name) {
    errs() << "Demangled Name: " << demangled_name << "\n";
  }

  const auto inst_count = F.getInstructionCount();
  errs() << "Instruction Count: " << F.getInstructionCount() << "\n";
  if (inst_count > 0) {

    const auto map = std::map<std::string, std::vector<Instruction &>>{};

    errs() << "Instructions:\n";
    for (auto &inst : F.getEntryBlock().instructionsWithoutDebug()) {
      std::string opcode_name = std::string{inst.getOpcodeName()};
      errs() << "  " << opcode_name << ": " << inst << "\n";
    }
  }
  const auto arg_count = F.arg_size();
  errs() << "Arg Count: " << arg_count << "\n";
  if (arg_count > 0) {
    errs() << "Args: \n";
    for (auto &arg : F.args()) {
      errs() << "\t" << arg << " ";
    }
    errs() << "\n";
  }
  errs() << "\n";
}

struct InstructionCount : PassInfoMixin<InstructionCount> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    perFunction(F);

    return PreservedAnalyses::all();
  }

  static bool isRequired() { return true; }
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
