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
  static std::size_t latest_id;
  std::size_t id;
  Variable(std::size_t id);
  Variable(const Variable &variable);
};

struct Constant {
  std::size_t value;
  Constant(std::size_t value);
  Constant(const Constant &constant);
};

struct Addition {
  ExprHandle left;
  ExprHandle right;
  Addition(ExprHandle left, ExprHandle right);
  Addition(const Addition &addition);
};

struct Multiplication {
  ExprHandle left;
  ExprHandle right;
  Multiplication(ExprHandle left, ExprHandle right);
  Multiplication(const Multiplication &multiplication);
};

struct Expr : std::variant<Variable, Constant, Addition, Multiplication> {
  using variant = std::variant<Variable, Constant, Addition, Multiplication>;
  Expr(variant base);
};

std::string toString(const ExprHandle expr);
ExprHandle reduce(const ExprHandle expr);
std::ostream &operator<<(std::ostream &os, const ExprHandle expr);
raw_ostream &operator<<(raw_ostream &os, const ExprHandle expr);

inline ExprHandle var(std::size_t id) {
  return std::make_shared<Expr>(Variable(id));
}

inline ExprHandle constant(std::size_t value) {
  return std::make_shared<Expr>(Constant(value));
}

inline ExprHandle mul(ExprHandle left, ExprHandle right) {
  return std::make_shared<Expr>(
      Multiplication(std::move(left), std::move(right)));
}

inline ExprHandle add(ExprHandle left, ExprHandle right) {
  return std::make_shared<Expr>(Addition(std::move(left), std::move(right)));
}
} // namespace EC
