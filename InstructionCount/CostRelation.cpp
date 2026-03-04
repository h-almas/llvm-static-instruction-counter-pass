#include "CostRelation.hpp"
#include <llvm/Support/Error.h>
#include <memory>
#include <sstream>

using namespace llvm;

namespace EC {
// special variable for constants. Only the index being 0 matters, the value has
// no meaning
std::size_t Variable::latest_id{0};

Expr::Expr(variant base) : variant(std::move(base)) {}

Variable::Variable(std::size_t id) : id(id) {}
Variable::Variable(const Variable &variable) : id(variable.id) {}

Constant::Constant(std::size_t value) : value(value) {}
Constant::Constant(const Constant &constant) : value(constant.value) {}

Addition::Addition(ExprHandle left, ExprHandle right)
    : left(left), right(right) {}
Addition::Addition(const Addition &addition)
    : left(std::make_unique<Expr>(*addition.left)),
      right(std::make_unique<Expr>(*addition.right)) {}

Multiplication::Multiplication(ExprHandle left, ExprHandle right)
    : left(left), right(right) {}
Multiplication::Multiplication(const Multiplication &multiplication)
    : left(std::make_unique<Expr>(*multiplication.left)),
      right(std::make_unique<Expr>(*multiplication.right)) {}

ExprHandle reduce(const ExprHandle expr) {
  struct Reducer {
    ExprHandle operator()(const Constant &c) const {
      return std::make_shared<Expr>(c);
    }

    ExprHandle operator()(const Variable &v) const {
      return std::make_shared<Expr>(v);
    }

    ExprHandle operator()(Addition &a) const {
      a.left = reduce(a.left);
      a.right = reduce(a.right);
      auto leftConst = std::get_if<Constant>(a.left.get());
      auto rightConst = std::get_if<Constant>(a.right.get());

      auto leftVar = std::get_if<Variable>(a.left.get());
      auto rightVar = std::get_if<Variable>(a.right.get());

      auto leftAdd = std::get_if<Addition>(a.left.get());
      auto rightAdd = std::get_if<Addition>(a.right.get());

      auto leftMul = std::get_if<Multiplication>(a.left.get());
      auto rightMul = std::get_if<Multiplication>(a.right.get());

      if (leftConst && rightConst) {
        return constant(leftConst->value + rightConst->value);
      } else if (leftConst && leftConst->value == 0) {
        return a.right;
      } else if (rightConst && rightConst->value == 0) {
        return a.left;
      }

      return std::make_shared<Expr>(a);
    }

    ExprHandle operator()(Multiplication &m) const {
      m.left = reduce(m.left);
      m.right = reduce(m.right);

      auto leftConst = std::get_if<Constant>(m.left.get());
      auto rightConst = std::get_if<Constant>(m.right.get());

      auto leftVar = std::get_if<Variable>(m.left.get());
      auto rightVar = std::get_if<Variable>(m.right.get());

      auto leftAdd = std::get_if<Addition>(m.left.get());
      auto rightAdd = std::get_if<Addition>(m.right.get());

      auto leftMul = std::get_if<Multiplication>(m.left.get());
      auto rightMul = std::get_if<Multiplication>(m.right.get());

      if (leftConst && rightConst) {
        return constant(leftConst->value * rightConst->value);
      } else if (leftConst && leftConst->value == 0 ||
                 rightConst && rightConst->value == 0) {
        return constant(0);
      } else if (leftConst && leftConst->value == 1) {
        return m.right;
      } else if (rightConst && rightConst->value == 1) {
        return m.left;
      }
      return std::make_shared<Expr>(m);
    }
  };
  errs() << "Reducing " << expr << ":\n";
  ExprHandle expr_new = std::visit(Reducer{}, *expr);
  errs() << "Result: " << expr_new << "\n";
  return expr_new;
}

std::string toString(const ExprHandle expr) {
  struct Printer {
    std::string operator()(const Constant &c) const {
      std::ostringstream oss;
      oss << c.value;
      return oss.str();
    }
    std::string operator()(const Variable &v) const {
      std::ostringstream oss;
      oss << "n" << v.id;
      return oss.str();
    }
    std::string operator()(const Addition &m) const {
      std::ostringstream oss;
      if (std::get_if<Multiplication>(m.left.get())) {
        oss << "(" << toString(m.left) << ")";
      } else {
        oss << toString(m.left);
      }
      oss << "+";
      if (std::get_if<Multiplication>(m.right.get())) {
        oss << "(" << toString(m.right) << ")";
      } else {
        oss << toString(m.right);
      }
      return oss.str();
    }
    std::string operator()(const Multiplication &m) const {
      std::ostringstream oss;
      if (std::get_if<Addition>(m.left.get())) {
        oss << "(" << toString(m.left) << ")";
      } else {
        oss << toString(m.left);
      }
      oss << "*";
      if (std::get_if<Addition>(m.right.get())) {
        oss << "(" << toString(m.right) << ")";
      } else {
        oss << toString(m.right);
      }
      return oss.str();
    }
  };
  return std::visit(Printer{}, *expr);
}

std::ostream &operator<<(std::ostream &os, const ExprHandle expr) {
  os << toString(expr);
  return os;
}
raw_ostream &operator<<(raw_ostream &os, const ExprHandle expr) {
  os << toString(expr);
  return os;
}

} // namespace EC
