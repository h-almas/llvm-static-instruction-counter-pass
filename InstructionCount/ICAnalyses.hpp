#pragma once

#include "Expression.hpp"
#include <filesystem>
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
  std::vector<std::string> energy_model_names;
  AggregationLevel aggregate_level;
  bool verbose = false;
  bool run_tests = false;
  bool loaded = false;
  using EnergyModel = std::map<std::string, std::size_t>;
  std::map<std::string, EnergyModel> energy_model{};

  bool isTargetValid(const Triple &target);
  bool invalidate(Module &M, const PreservedAnalyses &PA,
                  ModuleAnalysisManager::Invalidator &Invalidator);
};

struct ConfigReader : public AnalysisInfoMixin<ConfigReader> {
  using Result = Config;

  void loadConfig(Config &config);
  void loadEnergyModel(Config &config, std::filesystem::path base_directory,
                       std::string energy_model_name);

  Result run(Module &M, ModuleAnalysisManager &MAM);
  static AnalysisKey Key;
};

struct CounterFunctionAnalysis
    : public AnalysisInfoMixin<CounterFunctionAnalysis> {
  struct Result {
    Function *function;
    std::size_t fid;
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

struct CountAggregationFunctionAnalysis
    : public AnalysisInfoMixin<CountAggregationFunctionAnalysis> {
  using Result = CounterFunctionAnalysis::Result;

  CounterFunctionAnalysis::Result
  getICFunctionAnalysisResult(Function *F, FunctionAnalysisManager &FAM);

  void doAggregation(Result &prev_result, Result &called_result,
                     ExprHandle call_expr, Config &config);

  Result run(Function &F, FunctionAnalysisManager &FAM);
  static AnalysisKey Key;
};

struct CounterModuleAnalysis : public AnalysisInfoMixin<CounterModuleAnalysis> {
  struct Result {
    std::map<Function *, CounterFunctionAnalysis::Result,
             FunctionPointerComparator>
        function_results{};
  };

  Result run(Module &M, ModuleAnalysisManager &MAM);
  static AnalysisKey Key;
};

} // namespace IC

template <> struct yaml::MappingTraits<IC::Config> {
  static void mapping(IO &io, IC::Config &config);
};

template <> struct yaml::ScalarEnumerationTraits<IC::Config::AggregationLevel> {
  static void enumeration(IO &io, IC::Config::AggregationLevel &value);
};
