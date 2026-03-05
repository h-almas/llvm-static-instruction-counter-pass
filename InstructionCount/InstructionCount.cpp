#include "Expression.hpp"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <fstream>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/Analysis.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Support/CommandLine.h>
#include <map>
#include <sstream>
#include <vector>

using namespace llvm;

namespace EC {

const std::map<std::string, std::size_t> energy_model{
    {"alloca", 3}, {"load", 6}, {"mul", 10},
    {"fmul", 15},  {"add", 2},  {"fadd", 5}};

struct ECFunctionAnalysis : public AnalysisInfoMixin<ECFunctionAnalysis> {
  struct Result {
    const Function *function;
    std::map<std::string, std::size_t> energy_per_instruction_type{};
    std::map<std::string, ExprHandle> instruction_costs{};
    std::set<Function *> outgoing_calls{};
    std::map<Function *, ExprHandle> outgoing_calls_costs{};
    std::set<Function *> outgoing_invokes{};
    std::map<Function *, ExprHandle> outgoing_invokes_costs{};

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

    // for (auto &[k, _] : energy_model) {
    //   result.energy_per_instruction_type[k] =
    //       (result.instruction_counts.count(k) ? result.instruction_counts[k]
    //                                           : 0) *
    //       energy_model.at(k);
    // }

    return result;
  }
  static AnalysisKey Key;

  using BlockToLoops = std::map<BasicBlock *, std::vector<Loop *>>;

  void assignLoopsToBasicBlocks(BlockToLoops &BTL, Loop *loop) {
    for (Loop *l_inner : *loop) {
      assignLoopsToBasicBlocks(BTL, l_inner);
    }
    for (BasicBlock *BB : loop->getBlocks()) {
      if (BTL.count(BB) == 0) {
        BTL[BB].push_back(loop);
      }
    }
  }

  void getInstructionCounts(Function &F, FunctionAnalysisManager &FAM,
                            Result &result) {
    if (F.isDeclaration())
      return;
    std::string output{};
    raw_string_ostream ostream{output};

    std::map<std::string, std::size_t> inst_counts{};

    // errs() << "Counting function " << demangle(F.getName()) << "\n";

    BlockToLoops BTL{};

    // errs() << "Creating the map\n";
    std::map<Loop *, ExprHandle> loop_exprs{};
    // errs() << "Created the map\n";

    LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
    for (Loop *loop : LI) {
      assignLoopsToBasicBlocks(BTL, loop);
    }

    // assign variables to the loops
    // errs() << "Assigning vars to the loops:\n";
    for (Loop *loop : LI.getLoopsInPreorder()) {
      ExprHandle loop_expr{var(Variable::latest_id++)};
      // with this every loop gets its own variable, even those that actually
      // share numbers of iteration
      loop_exprs[loop] = std::move(loop_expr);
      // errs() << "Added " << loop_exprs[loop] << " to a loop\n";
    }

    for (auto &BB : F) {
      for (auto &inst : BB) {
        std::string opcode_name = std::string{inst.getOpcodeName()};
        ExprHandle expr = constant(1);
        if (BTL.count(&BB)) {
          for (auto loop : BTL[&BB]) {
            // CostRelation old_cr{std::move(new_cr)};
            // new_cr = CostRelation{loop_crs[loop]};
            // new_cr.add_subrelation(old_cr);
            expr = mul({loop_exprs[loop], expr});
          }
        }
        if (result.instruction_costs.count(opcode_name)) {
          result.instruction_costs[opcode_name] =
              add({result.instruction_costs[opcode_name], expr});

        } else {
          result.instruction_costs[opcode_name] = expr;
        }

        Function *called_F = nullptr;
        if (isa<CallInst>(inst)) {
          called_F = cast<CallInst>(inst).getCalledFunction();
          result.outgoing_calls.emplace(called_F);

          if (result.outgoing_calls_costs.count(called_F)) {
            // errs() << "Adding expr: " << expr << " to "
            //        << result.outgoing_calls_costs[called_F] << "\n";
            result.outgoing_calls_costs[called_F] =
                add({result.outgoing_calls_costs[called_F], expr});
            // errs() << "Now it's at: " <<
            // result.outgoing_invokes_costs[called_F]
            //        << "\n";
          } else {
            result.outgoing_calls_costs[called_F] = expr;
          }
        } else if (isa<InvokeInst>(inst)) {
          called_F = cast<InvokeInst>(inst).getCalledFunction();
          result.outgoing_invokes.emplace(called_F);

          if (result.outgoing_invokes_costs.count(called_F)) {
            // errs() << "Adding expr: " << expr << " to "
            //        << result.outgoing_invokes_costs[called_F] << "\n";
            result.outgoing_invokes_costs[called_F] =
                add({result.outgoing_invokes_costs[called_F], expr});
            // errs() << "Now it's at: " <<
            // result.outgoing_invokes_costs[called_F]
            //        << "\n";
          } else {
            result.outgoing_invokes_costs[called_F] = expr;
          }
        }
      }
    }

    // ostream << "Instructions:\n";
    // for (auto &bb : F) {
    //   for (auto &inst : bb) {
    //     std::string opcode_name = std::string{inst.getOpcodeName()};
    //     Function *called_F = nullptr;
    //     if (isa<CallInst>(inst)) {
    //       called_F = cast<CallInst>(inst).getCalledFunction();
    //       result.outgoing_calls.push_back(called_F);
    //     } else if (isa<InvokeInst>(inst)) {
    //       called_F = cast<InvokeInst>(inst).getCalledFunction();
    //       result.outgoing_invokes.push_back(called_F);
    //     }
    //
    //     addOrSetEntry(result.instruction_counts, opcode_name, 1);
    //
    //     // result.instruction_costs[opcode_name].value += 1;
    //   }
    // }
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

  void doAccumulation(Result &prev_result, Result &called_result,
                      ExprHandle call_expr) {
    for (auto &[k, v] : called_result.instruction_costs) {
      // errs() << "For inst: " << k << " and its expr: " << v
      //        << " from previous function\n";
      if (prev_result.instruction_costs.count(k)) {
        // errs() << "Was already in this functions result with value: "
        //        << prev_result.instruction_costs[k] << ". Modifying it.\n";
        prev_result.instruction_costs[k] =
            add({prev_result.instruction_costs[k], mul({call_expr, v})});
      } else {
        // errs() << "Was not yet in this functions result. Adding it.\n";
        prev_result.instruction_costs[k] = mul({call_expr, v});
      }
    }
  }

  Result run(Function &F, FunctionAnalysisManager &FAM) {
    // errs() << "In ECAFA:\n";
    Result prev_result = getECFunctionAnalysisResult(&F, FAM);

    for (auto *called_F : prev_result.outgoing_calls) {
      // errs() << "For call to function " << called_F->getName() << ":\n";
      if (called_F == &F) {
        // errs() << "Skipping a recursion at Function: " << called_F->getName()
        //        << "\n";
        continue;
      }
      // errs() << "Getting its results\n";
      Result called_F_result =
          FAM.getResult<ECAccumulationFunctionAnalysis>(*called_F);
      // errs() << "Got its results\n";
      // for (auto &[k, v] : called_F_result.instruction_counts) {
      //   prev_result.instruction_counts[k] += v;
      // }
      for (auto &[k, v] : called_F_result.instruction_costs) {
        doAccumulation(prev_result, called_F_result,
                       prev_result.outgoing_calls_costs[called_F]);
      }
    }
    for (auto *invoked_F : prev_result.outgoing_invokes) {
      // errs() << "For invoke to function " << invoked_F->getName() << ":\n";
      if (invoked_F == &F) {
        // errs() << "Skipping a recursion at Function: " <<
        // invoked_F->getName()
        //        << "\n";
        continue;
      }
      // errs() << "Getting its results\n";
      Result invoked_F_result =
          FAM.getResult<ECAccumulationFunctionAnalysis>(*invoked_F);
      // errs() << "Got its results\n";
      // for (auto &[k, v] : invoked_F_result.instruction_counts) {
      //   prev_result.instruction_counts[k] += v;
      // }
      doAccumulation(prev_result, invoked_F_result,
                     prev_result.outgoing_invokes_costs[invoked_F]);
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

    // errs() << "-- Function Pass!" << "\n";
    // errs() << "This pass currently does nothing" << "\n";
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

      // Name
      const auto name = FR.function->getName();
      ostream << name;
      const auto demangled_name = demangle(name);
      // if (name != demangled_name) {
      ostream << ",\"" << demangled_name << "\"";

      for (auto &inst : insts_to_record) {
        if (FR.instruction_costs.count(inst)) {

          ostream << ",(" << FR.instruction_costs.at(inst) << ":";
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

      for (auto &inst : insts_to_record) {
        ExprHandle expr;
        if (FR.instruction_costs.count(inst)) {
          expr = FR.instruction_costs[inst];
        } else {
          expr = constant(0);
        }
      }
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

} // namespace EC

/* New PM Registration */
llvm::PassPluginLibraryInfo getInstructionCountPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "InstructionCount", LLVM_VERSION_STRING,
          EC::registerPassBuilderCallbacks};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getInstructionCountPluginInfo();
}
