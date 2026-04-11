#pragma once

#include "Expression.hpp"
#include <llvm/IR/Analysis.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Support/YAMLTraits.h>
#include <llvm/TargetParser/Triple.h>
#include <map>
#include <string>
#include <vector>

using namespace llvm;

namespace IC {

struct FunctionPointerComparator {
  bool operator()(const Function *lhs, const Function *rhs) const {
    return lhs->getName() < rhs->getName();
  }
};

struct Config {
  enum AggregationLevel {
    FID,
    Constants,
    All,
  };

  std::vector<std::string> instructions_to_count;
  std::vector<std::string> targets;
  std::string energy_model_name;
  AggregationLevel aggregate_level;
  bool verbose = false;
  bool run_tests = false;
  bool loaded = false;
  std::map<std::string, std::size_t> energy_model{};

  bool isTargetValid(const Triple &target);
  bool invalidate(Module &M, const PreservedAnalyses &PA,
                  ModuleAnalysisManager::Invalidator &Invalidator);
};

struct ICConfigReader : public AnalysisInfoMixin<ICConfigReader> {
  using Result = Config;

  void loadConfig(Config &config);

  Result run(Module &M, ModuleAnalysisManager &MAM);
  static AnalysisKey Key;
};

struct ICFunctionAnalysis : public AnalysisInfoMixin<ICFunctionAnalysis> {
  struct Result {
    Function *function;
    std::map<std::string, ExprHandle> instruction_costs{};
    std::map<Function *, ExprHandle>
        outgoing_calls_costs{}; // counts as both calls and invokes
    // std::map<Function *, ExprHandle> outgoing_invokes_costs{};
    ExprHandle recursion_expr = constant(1);
  };

  Result run(Function &F, FunctionAnalysisManager &FAM);
  static AnalysisKey Key;

  using BlockToLoops = std::map<BasicBlock *, std::vector<Loop *>>;
  using BoundsToLoops = std::map<std::string, std::vector<Loop *>>;

  void createExpressionsForLoops(const BoundsToLoops &BoTL,
                                 const std::vector<Loop *> &unbounded_loops,
                                 std::map<Loop *, ExprHandle> &loop_exprs,
                                 Config &config);

  void assignLoopsToLoopBounds(BoundsToLoops &BTL,
                               std::vector<Loop *> &unbounded_loops, Loop *loop,
                               ScalarEvolution &SE);

  void assignLoopsToBasicBlocks(BlockToLoops &BTL, Loop *loop);
  void countInstructions(Result &result,
                         std::map<Loop *, ExprHandle> &loop_exprs,
                         BlockToLoops &BlTL, Config &config);
};

struct ICAggregationFunctionAnalysis
    : public AnalysisInfoMixin<ICAggregationFunctionAnalysis> {
  using Result = ICFunctionAnalysis::Result;

  ICFunctionAnalysis::Result
  getICFunctionAnalysisResult(Function *F, FunctionAnalysisManager &FAM);

  void doAggregation(Result &prev_result, Result &called_result,
                     ExprHandle call_expr, Config &config);

  Result run(Function &F, FunctionAnalysisManager &FAM);
  static AnalysisKey Key;
};

struct ICModuleAnalysis : public AnalysisInfoMixin<ICModuleAnalysis> {
  struct Result {
    std::map<Function *, ICFunctionAnalysis::Result, FunctionPointerComparator>
        function_results{};
  };

  Result run(Module &M, ModuleAnalysisManager &MAM);
  static AnalysisKey Key;
};

} // namespace IC

template <> struct llvm::yaml::MappingTraits<IC::Config> {
  static void mapping(IO &io, IC::Config &config);
};

template <> struct yaml::ScalarEnumerationTraits<IC::Config::AggregationLevel> {
  static void enumeration(IO &io, IC::Config::AggregationLevel &value);
};
