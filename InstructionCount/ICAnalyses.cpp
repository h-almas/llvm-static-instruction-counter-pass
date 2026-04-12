#include "ICAnalyses.hpp"
#include <fstream>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Demangle/Demangle.h>
#include <llvm/IR/Module.h>

using llvm::yaml::IO;

void llvm::yaml::MappingTraits<IC::Config>::mapping(IO &io,
                                                    IC::Config &config) {
  io.mapRequired("energy_model_name", config.energy_model_names);
  io.mapRequired("instructions_to_count", config.instructions_to_count);
  io.mapRequired("targets_allowed", config.targets);
  io.mapOptional("aggregation_level", config.aggregate_level, IC::Config::FID);
  io.mapOptional("verbose", config.verbose, false);
  io.mapOptional("run_tests", config.run_tests, false);
}

void llvm::yaml::ScalarEnumerationTraits<IC::Config::AggregationLevel>::
    enumeration(IO &io, IC::Config::AggregationLevel &value) {
  io.enumCase(value, "fid", IC::Config::FID);
  io.enumCase(value, "constants", IC::Config::Constants);
  io.enumCase(value, "all", IC::Config::All);
}

namespace IC {

AnalysisKey ICFunctionAnalysis::Key;
AnalysisKey ICAggregationFunctionAnalysis::Key;
AnalysisKey ICModuleAnalysis::Key;
AnalysisKey ICConfigReader::Key;

bool Config::isTargetValid(const Triple &target) {
  for (auto &t : targets) {
    std::string tl{t};
    std::transform(tl.begin(), tl.end(), tl.begin(), ::tolower);
    if (tl == "gpu") {
      if (target.isGPU())
        return true;
    } else if (tl == "nvptx") {
      if (target.isNVPTX())
        return true;
    } else if (tl == "amdgpu") {
      if (target.isAMDGPU())
        return true;
    } else if (tl == "spir-v") {
      if (target.isSPIRV())
        return true;
    } else if (tl == "spir") {
      if (target.isSPIR())
        return true;
    } else if (tl == "dxil") {
      if (target.isDXIL())
        return true;
    } else if (tl == "cpu") {
      if (!target.isGPU())
        return true;
    } else if (tl == "riscv") {
      if (target.isRISCV())
        return true;
    } else if (tl == "riscv32") {
      if (target.isRISCV32())
        return true;
    } else if (tl == "riscv64") {
      if (target.isRISCV64())
        return true;
    } else if (tl == "arm") {
      if (target.isARM())
        return true;
    } else if (tl == "x86") {
      if (target.isX86())
        return true;
    }
  }
  return false;
}

bool Config::invalidate(Module &M, const PreservedAnalyses &PA,
                        ModuleAnalysisManager::Invalidator &Invalidator) {
  auto PAC = PA.getChecker(&ICConfigReader::Key);
  return !PAC.preservedWhenStateless();
}
void ICConfigReader::loadEnergyModel(Config &config,
                                     std::filesystem::path base_directory,
                                     std::string energy_model_name) {
  std::ifstream energy_model_file;
  std::filesystem::path energy_model_dir_path =
      base_directory / "energy_models";
  std::filesystem::path energy_model_file_path =
      energy_model_dir_path / (energy_model_name + ".txt");

  energy_model_file.open(energy_model_file_path);
  if (!energy_model_file.is_open()) {
    errs() << "Error opening energy model file at " << energy_model_file_path
           << "\n";
    return;
  }

  std::ostringstream osstr;
  osstr << energy_model_file.rdbuf();
  energy_model_file.close();
  std::string energy_model_contents = osstr.str();

  std::string line;
  std::istringstream isstr{energy_model_contents};
  while (std::getline(isstr, line)) {
    StringRef lineref = line;
    std::size_t colon_pos = lineref.find(":", 0);
    if (colon_pos == std::string::npos) {
      errs() << "Line \"" << line << "\" in energy model file at "
             << energy_model_file_path << " is malformed. Skipping\n";
      continue;
    }
    StringRef instruction_name = lineref.substr(0, colon_pos).trim();
    StringRef energy_usage_str = lineref.substr(colon_pos + 1).trim();
    if (energy_usage_str.empty()) {
      errs() << "Line \"" << line << "\" in energy model file at "
             << energy_model_file_path << "is malformed. Skipping\n";
      continue;
    }
    std::size_t energy_usage;
    if (!llvm::to_integer<std::size_t>(StringRef(energy_usage_str).trim(),
                                       energy_usage)) {
      errs() << "Error while parsing line: \"" << line << "\" in "
             << energy_model_file_path << "\n";
      return;
    };

    // std::size_t energy_usage = (energy_usage_str);
    // errs() << "inst name: " << instruction_name
    //        << " energy usage: " << energy_usage << "\n";

    config.energy_model[energy_model_name][instruction_name.str()] =
        energy_usage;
  }
}

void ICConfigReader::loadConfig(Config &config) {
  std::filesystem::path base_directory{"."};
  auto icconfig_result = std::getenv("IC_CONFIG_DIR");
  if (icconfig_result) {
    base_directory = icconfig_result;
  }
  std::string config_file_path{base_directory / "config.yaml"};

  ErrorOr<std::unique_ptr<MemoryBuffer>> mb =
      MemoryBuffer::getFile(config_file_path);
  if (!mb) {
    errs() << "Error opening file at " << config_file_path << "\n";
    return;
  }

  yaml::Input yin((*mb)->getBuffer());

  yin >> config;
  if (auto error = yin.error()) {
    errs() << error.message() << "\n";
    errs() << "Error reading config file at " << config_file_path << ".\n";
    return;
  }

  for (auto &energy_model_name : config.energy_model_names) {
    loadEnergyModel(config, base_directory, energy_model_name);
  }
  config.loaded = true;
}

ICConfigReader::Result ICConfigReader::run(Module &M,
                                           ModuleAnalysisManager &MAM) {
  Config config;
  loadConfig(config);
  return config;
}

// std::map<Function *, std::size_t> function_variable_ids{};

ICFunctionAnalysis::Result
ICFunctionAnalysis::run(Function &F, FunctionAnalysisManager &FAM) {
  ICFunctionAnalysis::Result result;
  result.function = &F;
  if (F.isDeclaration())
    return result;
  result.fid = Variable::latest_id["f"]++;

  auto &MAMProxy = FAM.getResult<ModuleAnalysisManagerFunctionProxy>(F);
  Module &M = *F.getParent();
  auto &config = *MAMProxy.getCachedResult<ICConfigReader>(M);

  if (config.verbose)
    errs() << "Counting function " << demangle(F.getName()) << "\n";

  ICFunctionAnalysis::BlockToLoops BlTL{};
  ICFunctionAnalysis::BoundsToLoops BoTL{};
  std::vector<Loop *> unbounded_loops{};

  std::map<Loop *, ExprHandle> loop_exprs{};

  LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
  ScalarEvolution &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
  for (Loop *loop : LI) {
    assignLoopsToBasicBlocks(BlTL, loop);
    assignLoopsToLoopBounds(BoTL, unbounded_loops, loop, SE);
  }

  // assign variables to the loops
  createExpressionsForLoops(BoTL, unbounded_loops, loop_exprs, config);

  countInstructions(result, loop_exprs, BlTL, config);

  return result;
}

void ICFunctionAnalysis::createExpressionsForLoops(
    const BoundsToLoops &BoTL, const std::vector<Loop *> &unbounded_loops,
    std::map<Loop *, ExprHandle> &loop_exprs, Config &config) {
  if (config.verbose)
    errs() << "Assigning vars to the loops:\n";
  for (auto &[bounds, loops] : BoTL) {
    ExprHandle loop_expr{var(Variable::latest_id["n"]++)};
    for (auto loop : loops) {
      loop_exprs[loop] = loop_expr;
      if (config.verbose)
        errs() << "Added " << loop_exprs[loop] << " to a loop\n";
    }
  }
  for (auto *loop : unbounded_loops) {
    ExprHandle loop_expr{var(Variable::latest_id["n"]++)};
    loop_exprs[loop] = std::move(loop_expr);
    if (config.verbose)
      errs() << "Added " << loop_exprs[loop] << " to a loop\n";
  }
}

void ICFunctionAnalysis::assignLoopsToLoopBounds(
    BoundsToLoops &BTL, std::vector<Loop *> &unbounded_loops, Loop *loop,
    ScalarEvolution &SE) {
  for (Loop *l_inner : *loop) {
    assignLoopsToLoopBounds(BTL, unbounded_loops, l_inner, SE);
  }

  auto bounds_opt = loop->getBounds(SE);
  if (bounds_opt.has_value()) {
    auto bounds = bounds_opt.value();

    // not ideal solution
    std::string str;
    raw_string_ostream os(str);
    os << bounds.getInitialIVValue() << bounds.getFinalIVValue()
       << bounds.getStepValue();
    BTL[os.str()].push_back(loop);
  } else {
    unbounded_loops.push_back(loop);
  }
}

void ICFunctionAnalysis::assignLoopsToBasicBlocks(BlockToLoops &BTL,
                                                  Loop *loop) {
  for (Loop *l_inner : *loop) {
    assignLoopsToBasicBlocks(BTL, l_inner);
  }
  for (BasicBlock *BB : loop->getBlocks()) {
    BTL[BB].push_back(loop);
  }
}

void ICFunctionAnalysis::countInstructions(
    Result &result, std::map<Loop *, ExprHandle> &loop_exprs,
    BlockToLoops &BlTL, Config &config) {

  for (auto &BB : *result.function) {
    for (auto &inst : BB) {
      std::string opcode_name = std::string{inst.getOpcodeName()};
      auto it = std::find(config.instructions_to_count.begin(),
                          config.instructions_to_count.end(), opcode_name);
      bool dontCount = it == config.instructions_to_count.end();
      bool isCallOrInvoke = isa<CallInst, InvokeInst>(inst);
      if (dontCount && !isCallOrInvoke) {
        continue;
      }

      ExprHandle expr = constant(1);
      if (BlTL.count(&BB)) {
        for (auto loop : BlTL[&BB]) {
          expr = mul({loop_exprs[loop], expr});
        }
      }
      if (!dontCount) {
        if (result.instruction_costs.count(opcode_name)) {
          result.instruction_costs[opcode_name] =
              add({result.instruction_costs[opcode_name], expr});

        } else {
          result.instruction_costs[opcode_name] = expr;
        }
      }

      if (isCallOrInvoke) {
        Function *called_F = nullptr;
        if (isa<CallInst>(inst)) {
          called_F = cast<CallInst>(inst).getCalledFunction();
        } else if (isa<InvokeInst>(inst)) {
          called_F = cast<InvokeInst>(inst).getCalledFunction();
        }

        if (!called_F)
          continue;                        // in case it's null
        if (called_F == result.function) { // if it's a recursion
          result.recursion_expr =
              add({result.recursion_expr,
                   mul({expr, var(Variable::latest_id["r"]++)})});
          continue;
        }

        auto it =
            std::find(config.instructions_to_count.begin(),
                      config.instructions_to_count.end(), called_F->getName());
        if (it != config.instructions_to_count
                      .end()) { // if it should be counted as an instruction
          if (config.verbose)
            errs() << "user specified call.\n";
          std::string fname = called_F->getName().str();
          if (result.instruction_costs.count(fname)) {
            result.instruction_costs[fname] =
                add({result.instruction_costs[fname], expr});

          } else {
            result.instruction_costs[fname] = expr;
          }
          continue;
        }
        if (called_F->isDeclaration()) {
          continue; // if it's a declaration, skip
        }

        if (result.outgoing_calls_costs.count(called_F)) {
          result.outgoing_calls_costs[called_F] =
              add({result.outgoing_calls_costs[called_F], expr});
        } else {
          result.outgoing_calls_costs[called_F] = expr;
        }
      }
    }
  }
}

ICAggregationFunctionAnalysis::Result
ICAggregationFunctionAnalysis::getICFunctionAnalysisResult(
    Function *F, FunctionAnalysisManager &FAM) {
  ICFunctionAnalysis::Result *result_ptr =
      FAM.getCachedResult<ICFunctionAnalysis>(*F);
  if (!result_ptr) {
    errs() << "There was no cached result for Function: " << F->getName()
           << "!\n";
    return ICFunctionAnalysis::Result();
  }
  return *result_ptr;
}

void ICAggregationFunctionAnalysis::doAggregation(Result &prev_result,
                                                  Result &called_result,
                                                  ExprHandle call_expr,
                                                  Config &config) {
  for (auto &[k, v] : called_result.instruction_costs) {
    if (Constant *c = std::get_if<Constant>(v.get())) {
      if (c->value == 0) {
        continue;
      }
    } else {
      ExprHandle expr_base =
          mul({substituteRecursionVariables(called_result.recursion_expr),
               call_expr});
      ExprHandle expr_actual = v;
      if (config.aggregate_level == Config::FID) {
        expr_actual = var(called_result.fid, 1, 1, "f");
      } else if (config.aggregate_level == Config::Constants) {
        if (Constant *c = std::get_if<Constant>(v.get())) {
          expr_actual = std::make_shared<Expr>(*c);
        } else {
          expr_actual = var(called_result.fid, 1, 1, "f");
        }
      }

      ExprHandle expr = mul({expr_base, expr_actual});

      if (prev_result.instruction_costs.count(k)) {
        prev_result.instruction_costs[k] =
            add({prev_result.instruction_costs[k], expr});
      } else {
        prev_result.instruction_costs[k] = expr;
      }
    }
  }
}

ICAggregationFunctionAnalysis::Result
ICAggregationFunctionAnalysis::run(Function &F, FunctionAnalysisManager &FAM) {
  auto &MAMProxy = FAM.getResult<ModuleAnalysisManagerFunctionProxy>(F);
  Module &M = *F.getParent();
  auto &config = *MAMProxy.getCachedResult<ICConfigReader>(M);

  if (config.verbose)
    errs() << "In ICAggregationFunctionAnalysis for " << F.getName() << ":\n";
  Result prev_result = getICFunctionAnalysisResult(&F, FAM);

  for (auto &[called_F, call_expr] : prev_result.outgoing_calls_costs) {
    if (config.verbose)
      errs() << "For call to function " << called_F->getName() << ":\n";
    if (config.verbose)
      errs() << "Getting result of " << called_F->getName() << "\n";
    Result called_F_result =
        FAM.getResult<ICAggregationFunctionAnalysis>(*called_F);
    if (config.verbose)
      errs() << "Got result of " << called_F->getName() << "\n";
    doAggregation(prev_result, called_F_result, call_expr, config);
  }

  return prev_result;
}

ICModuleAnalysis::Result ICModuleAnalysis::run(Module &M,
                                               ModuleAnalysisManager &MAM) {
  auto &config = MAM.getResult<ICConfigReader>(M);

  std::vector<Function *> functions_sorted;
  for (auto &F : M) {
    functions_sorted.push_back(&F);
  }
  llvm::sort(functions_sorted.begin(), functions_sorted.end(),
             FunctionPointerComparator());

  Result result;
  FunctionAnalysisManager &FAM =
      MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  for (auto F : functions_sorted) {
    if (config.verbose) {
      errs() << "Running ICFunctionAnalysis for " << F->getName() << "\n";
    }
    result.function_results[F] = FAM.getResult<ICFunctionAnalysis>(*F);
  }

  for (auto F : functions_sorted) {
    if (config.verbose) {
      errs() << "Running ICAggregationFunctionAnalysis for " << F->getName()
             << "\n";
    }
    result.function_results[F] =
        FAM.getResult<ICAggregationFunctionAnalysis>(*F);
  }

  return result;
}
} // namespace IC
