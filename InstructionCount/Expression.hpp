#pragma once
#include "llvm/IR/Mangler.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/Analysis.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <map>
#include <memory>
#include <variant>

using namespace llvm;

namespace EC {

struct Variable;
struct Constant;
struct Addition;
struct Multiplication;

struct Expr;
using ExprHandle = std::shared_ptr<Expr>;

struct Variable {
  static std::map<std::string, std::size_t> latest_id;
  std::size_t id;
  std::size_t factor;
  std::size_t exponent;
  std::string letter;
  Variable(std::size_t id, std::size_t factor = 1, std::size_t exponent = 1,
           std::string letter = "n");
  Variable(const Variable &variable);
};

struct Constant {
  std::size_t value;
  Constant(std::size_t value);
  Constant(const Constant &constant);
};

struct Addition {
  std::vector<ExprHandle> terms;
  Addition(std::vector<ExprHandle> terms);
  Addition(const Addition &addition);
};

struct Multiplication {
  std::vector<ExprHandle> terms;
  Multiplication(std::vector<ExprHandle> terms);
  Multiplication(const Multiplication &addition);
};

struct Expr : std::variant<Variable, Constant, Addition, Multiplication> {
  using variant = std::variant<Variable, Constant, Addition, Multiplication>;
  Expr(variant base);
};

std::string toString(const ExprHandle expr);
ExprHandle reduce(const ExprHandle expr);
ExprHandle substituteRecursionVariables(ExprHandle expr);
ExprHandle cloneExpression(const ExprHandle expr);
std::ostream &operator<<(std::ostream &os, const ExprHandle expr);
raw_ostream &operator<<(raw_ostream &os, const ExprHandle expr);

inline ExprHandle var(std::size_t id, std::size_t factor = 1,
                      std::size_t exponent = 1, std::string letter = "n") {
  return std::make_shared<Expr>(Variable(id, factor, exponent, letter));
}

inline ExprHandle constant(std::size_t value) {
  return std::make_shared<Expr>(Constant(value));
}

inline ExprHandle mul(std::vector<ExprHandle> terms) {
  return reduce(std::make_shared<Expr>(Multiplication(terms)));
}

inline ExprHandle add(std::vector<ExprHandle> terms) {
  return reduce(std::make_shared<Expr>(Addition(terms)));
}

} // namespace EC
