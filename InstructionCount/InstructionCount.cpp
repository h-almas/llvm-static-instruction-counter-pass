#include "Expression.hpp"
#include <algorithm>
#include <cstddef>
#include <fstream>
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
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

using namespace llvm;
using llvm::yaml::IO;

struct Config {
  std::vector<std::string> instructions_to_count;
  std::vector<std::string> targets;
  std::string energy_model_name;
  bool verbose = false;
  bool run_tests = false;
  bool loaded = false;

  bool isTargetValid(const Triple &target) {
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
};

template <> struct llvm::yaml::MappingTraits<Config> {
  static void mapping(IO &io, Config &config) {
    io.mapRequired("energy_model_name", config.energy_model_name);
    io.mapRequired("instructions_to_count", config.instructions_to_count);
    io.mapRequired("targets_allowed", config.targets);
    io.mapOptional("verbose", config.verbose, false);
    io.mapOptional("run_tests", config.run_tests, false);
  }
};

namespace EC {

std::map<std::string, std::size_t> energy_model{};
Config config;
std::map<Function *, std::size_t> function_variable_ids{};

struct ICFunctionAnalysis : public AnalysisInfoMixin<ICFunctionAnalysis> {
  struct Result {
    Function *function;
    std::map<std::string, ExprHandle> instruction_costs{};
    std::map<Function *, ExprHandle>
        outgoing_calls_costs{}; // counts as both calls and invokes
    // std::map<Function *, ExprHandle> outgoing_invokes_costs{};
    ExprHandle recursion_expr = constant(1);
  };

  Result run(Function &F, FunctionAnalysisManager &FAM) {
    Result result;
    result.function = &F;
    if (F.isDeclaration())
      return result;

    if (config.verbose)
      errs() << "Counting function " << demangle(F.getName()) << "\n";

    BlockToLoops BlTL{};
    BoundsToLoops BoTL{};
    std::vector<Loop *> unbounded_loops{};

    std::map<Loop *, ExprHandle> loop_exprs{};

    LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
    ScalarEvolution &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
    for (Loop *loop : LI) {
      assignLoopsToBasicBlocks(BlTL, loop);
      assignLoopsToLoopBounds(BoTL, unbounded_loops, loop, SE);
    }

    // assign variables to the loops
    createExpressionsForLoops(BoTL, unbounded_loops, loop_exprs);

    countInstructions(result, loop_exprs, BlTL);

    return result;
  }
  static AnalysisKey Key;

  using BlockToLoops = std::map<BasicBlock *, std::vector<Loop *>>;

  using BoundsToLoops = std::map<std::string, std::vector<Loop *>>;

  void createExpressionsForLoops(const BoundsToLoops &BoTL,
                                 const std::vector<Loop *> &unbounded_loops,
                                 std::map<Loop *, ExprHandle> &loop_exprs) {
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

  void assignLoopsToLoopBounds(BoundsToLoops &BTL,
                               std::vector<Loop *> &unbounded_loops, Loop *loop,
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

  void assignLoopsToBasicBlocks(BlockToLoops &BTL, Loop *loop) {
    for (Loop *l_inner : *loop) {
      assignLoopsToBasicBlocks(BTL, l_inner);
    }
    for (BasicBlock *BB : loop->getBlocks()) {
      BTL[BB].push_back(loop);
    }
  }

  void countInstructions(Result &result,
                         std::map<Loop *, ExprHandle> &loop_exprs,
                         BlockToLoops &BlTL) {

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
                     mul({expr, var(Variable::latest_id["n"]++)})});
            continue;
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
};

struct ICAggregationFunctionAnalysis
    : public AnalysisInfoMixin<ICAggregationFunctionAnalysis> {
  using Result = ICFunctionAnalysis::Result;

  ICFunctionAnalysis::Result
  getECFunctionAnalysisResult(Function *F, FunctionAnalysisManager &FAM) {
    ICFunctionAnalysis::Result *result_ptr =
        FAM.getCachedResult<ICFunctionAnalysis>(*F);
    if (!result_ptr) {
      errs() << "There was no cached result for Function: " << F->getName()
             << "!\n";
      return ICFunctionAnalysis::Result();
    }
    return *result_ptr;
  }

  void doAccumulation(Result &prev_result, Result &called_result,
                      ExprHandle call_expr) {
    for (auto &[k, v] : called_result.instruction_costs) {
      if (Constant *c = std::get_if<Constant>(v.get())) {
        if (c->value == 0) {
          continue;
        }
      } else {
        std::size_t id;
        if (function_variable_ids.count(called_result.function)) {
          id = function_variable_ids[called_result.function];
        } else {
          id = Variable::latest_id["f"]++;
          function_variable_ids[called_result.function] = id;
        }
        ExprHandle expr =
            mul({called_result.recursion_expr, call_expr, var(id, 1, 1, "f")});

        if (prev_result.instruction_costs.count(k)) {
          prev_result.instruction_costs[k] =
              add({prev_result.instruction_costs[k], expr});
        } else {
          prev_result.instruction_costs[k] = expr;
        }
      }
      // if (config.verbose)
      //   errs() << "For inst: " << k << " and its expr: " << v
      //          << " from previous function\n";
      // if (prev_result.instruction_costs.count(k)) {
      //   if (config.verbose)
      //     errs() << "Was already in this functions result with value: "
      //            << prev_result.instruction_costs[k] << ". Modifying it.\n";
      //   prev_result.instruction_costs[k] =
      //       add({prev_result.instruction_costs[k],
      //            mul({called_result.recursion_expr, call_expr, v})});
      // } else {
      //   if (config.verbose)
      //     errs() << "Was not yet in this functions result. Adding it.\n";
      //   prev_result.instruction_costs[k] =
      //       mul({called_result.recursion_expr, call_expr, v});
      // }
    }
  }

  Result run(Function &F, FunctionAnalysisManager &FAM) {
    if (config.verbose)
      errs() << "In ECAccumulationFunctionAnalysis for " << F.getName()
             << ":\n";
    Result prev_result = getECFunctionAnalysisResult(&F, FAM);

    for (auto &[called_F, _] : prev_result.outgoing_calls_costs) {
      if (config.verbose)
        errs() << "For call to function " << called_F->getName() << ":\n";
      if (config.verbose)
        errs() << "Getting result of " << called_F->getName() << "\n";
      Result called_F_result =
          FAM.getResult<ICAggregationFunctionAnalysis>(*called_F);
      if (config.verbose)
        errs() << "Got result of " << called_F->getName() << "\n";
      doAccumulation(prev_result, called_F_result,
                     prev_result.outgoing_calls_costs[called_F]);
    }

    return prev_result;
  }
  static AnalysisKey Key;
};

struct ICModuleAnalysis : public AnalysisInfoMixin<ICModuleAnalysis> {
  struct Result {
    std::map<Function *, ICFunctionAnalysis::Result> function_results{};
  };

  Result run(Module &M, ModuleAnalysisManager &MAM) {

    // follow the calls and add
    Result result;
    FunctionAnalysisManager &FAM =
        MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

    for (auto &F : M) {
      result.function_results[&F] = FAM.getResult<ICFunctionAnalysis>(F);
    }

    // ideally I should be building a call graph
    // Then removing all cycles by grouping them as special nodes
    // And then I can run my analysis on those graph nodes

    for (auto &F : M) {
      if (config.verbose) {
        errs() << "Running ECAccumulationFunctionAnalysis for " << F.getName()
               << "\n";
      }
      result.function_results[&F] =
          FAM.getResult<ICAggregationFunctionAnalysis>(F);
    }

    return result;
  }
  static AnalysisKey Key;
};

AnalysisKey ICFunctionAnalysis::Key;
AnalysisKey ICAggregationFunctionAnalysis::Key;
AnalysisKey ICModuleAnalysis::Key;

struct InstructionCount : PassInfoMixin<InstructionCount> {

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    // getInstructionCounts(F);

    // errs() << "-- Function Pass!" << "\n";
    // errs() << "This pass currently does nothing" << "\n";
    return PreservedAnalyses::all();
  }

  bool loadConfig() {
    if (!config.loaded) {

      std::string config_file_path{"./config.yaml"};

      ErrorOr<std::unique_ptr<MemoryBuffer>> mb =
          MemoryBuffer::getFile(config_file_path);
      if (!mb) {
        errs() << "Error opening file at " << config_file_path << "\n";
        errs() << "Exiting pass early\n";
        return false;
      }

      yaml::Input yin((*mb)->getBuffer());

      yin >> config;
      if (auto error = yin.error()) {
        errs() << error.message() << "\n";
        errs() << "Error reading config file at " << config_file_path << ".\n";
        errs() << "Exiting pass early\n";
        return false;
      }

      // errs() << "Instructions to count are:\n";
      // for (const auto &str : config.instructions_to_count) {
      //   errs() << str << ",";
      // }
      // errs() << "\n";
      // errs() << "Chosen energy model: " << config.energy_model_name << "\n";
      // load energy model:
      std::ifstream energy_model_file;
      std::string energy_model_dir_path = "./energy_models/";
      std::string energy_model_file_path =
          energy_model_dir_path + config.energy_model_name + ".txt";

      energy_model_file.open(energy_model_file_path);
      if (!energy_model_file.is_open()) {
        errs() << "Error opening energy model file at "
               << energy_model_file_path << "\n";
        errs() << "Exiting pass early\n";
        return false;
      }

      std::ostringstream osstr;
      osstr << energy_model_file.rdbuf();
      std::string energy_model_contents = osstr.str();
      // errs() << "Energy model file contents:\n" << energy_model_contents <<
      // "\n";

      energy_model_file.close();

      std::string line;
      std::istringstream isstr{energy_model_contents};
      while (std::getline(isstr, line)) {
        std::size_t colon_pos = line.find(":", 0);
        if (colon_pos == std::string::npos) {
          errs() << "Line \"" << line << "\" in energy model file at "
                 << energy_model_file_path << " is malformed. Skipping\n";
          continue;
        }
        std::string instruction_name = line.substr(0, colon_pos);
        std::string energy_usage_str = line.substr(colon_pos + 1);
        if (energy_usage_str.empty()) {
          errs() << "Line \"" << line << "\" in energy model file at "
                 << energy_model_file_path << "is malformed. Skipping\n";
          continue;
        }
        std::size_t energy_usage = std::stoull(energy_usage_str);
        // errs() << "inst name: " << instruction_name
        //        << " energy usage: " << energy_usage << "\n";

        energy_model[instruction_name] = energy_usage;
      }
      config.loaded = true;
    }
    return true;
  }

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
  }

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    // read config
    if (!loadConfig()) {
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

    for (auto &F : M) {
      function_variable_ids[&F] = Variable::latest_id["f"]++;
    }

    std::string output_str{};
    raw_string_ostream ostream{output_str};

    ostream << "Function Name,Demangled Name,fid";
    for (auto &inst : config.instructions_to_count) {
      ostream << "," << inst;
    }
    ostream << "\n";

    auto MR = MAM.getResult<ICModuleAnalysis>(M);

    for (auto &[F, FR] : MR.function_results) {
      auto &costs = FR.instruction_costs;
      for (auto &[inst, cost] : costs) {
        costs[inst] = mul({cost, constant(energy_model[inst])});
      }
    }

    for (auto &[function, FR] : MR.function_results) {
      if (function->isDeclaration())
        continue;

      // Name
      const auto name = FR.function->getName();
      ostream << name;
      const auto demangled_name = demangle(name);
      // if (name != demangled_name) {
      ostream << ",\"" << demangled_name << "\"";
      ostream << ",f" << function_variable_ids[function];

      for (auto &inst : config.instructions_to_count) {
        if (FR.instruction_costs.count(inst)) {
          ostream << "," << FR.instruction_costs.at(inst);
        } else {
          ostream << ",0";
        }
      }
      ostream << "\n";

      for (auto &inst : config.instructions_to_count) {
        ExprHandle expr;
        if (FR.instruction_costs.count(inst)) {
          expr = FR.instruction_costs[inst];
        } else {
          expr = constant(0);
        }
      }
    }

    std::ofstream csv_file{};

    // errs() << "opening file\n";

    std::string output_content;
    raw_string_ostream os(output_content);
    os << M.getName() << "-" << M.getTargetTriple().getTriple() << ".csv";
    csv_file.open(os.str());

    csv_file << output_str << "\n";
    csv_file.close();
    // errs() << "closed file\n";

    return PreservedAnalyses::all();
  }

  // static bool isRequired() { return true; }
};

void registerPassBuilderCallbacks(PassBuilder &PB) {
  PB.registerAnalysisRegistrationCallback([](FunctionAnalysisManager &FAM) {
    FAM.registerPass([&] { return ICFunctionAnalysis(); });
    FAM.registerPass([&] { return ICAggregationFunctionAnalysis(); });
  });
  PB.registerAnalysisRegistrationCallback([](ModuleAnalysisManager &MAM) {
    MAM.registerPass([&] { return ICModuleAnalysis(); });
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
  PB.registerOptimizerLastEPCallback([](ModulePassManager &MPM,
                                        OptimizationLevel Level,
                                        ThinOrFullLTOPhase TOF) {
    // FunctionPassManager FPM;
    // FPM.addPass(InstructionCount());

    if (TOF == ThinOrFullLTOPhase::None ||
        TOF == ThinOrFullLTOPhase::FullLTOPostLink) {
      MPM.addPass(InstructionCount());
    }
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
