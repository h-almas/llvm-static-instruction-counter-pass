#include "Expression.hpp"
#include <algorithm>
#include <llvm/Support/Error.h>
#include <memory>
#include <sstream>
#include <variant>

using namespace llvm;

namespace EC {
// special variable for constants. Only the index being 0 matters, the value has
// no meaning
std::size_t Variable::latest_id{0};

Expr::Expr(variant base) : variant(std::move(base)) {}

Variable::Variable(std::size_t id, std::size_t factor, std::size_t exponent)
    : id(id), factor(factor), exponent(exponent) {}
Variable::Variable(const Variable &variable)
    : id(variable.id), factor(variable.factor), exponent(variable.exponent) {}

Constant::Constant(std::size_t value) : value(value) {}
Constant::Constant(const Constant &constant) : value(constant.value) {}

Addition::Addition(std::vector<ExprHandle> terms) : terms(std::move(terms)) {}
Addition::Addition(const Addition &addition) : terms(addition.terms) {}

Multiplication::Multiplication(std::vector<ExprHandle> terms)
    : terms{std::move(terms)} {}
Multiplication::Multiplication(const Multiplication &multiplication)
    : terms(multiplication.terms) {}

ExprHandle reduce(const ExprHandle expr) {
  struct Reducer {
    ExprHandle operator()(const Constant &c) const {
      return std::make_shared<Expr>(c);
    }

    ExprHandle operator()(const Variable &v) const {
      if (v.factor == 0) {
        return constant(0);
      } else if (v.exponent == 0) {
        return constant(1);
      }
      return std::make_shared<Expr>(v);
    }

    ExprHandle operator()(Addition &a) const {
      if (a.terms.size() == 1) {
        return a.terms[0];
      }
      for (auto &term : a.terms) {
        term = reduce(term);
      }

      // partition the vector into non-additions and additions
      auto part_it = std::partition(
          a.terms.begin(), a.terms.end(),
          [](const ExprHandle &t) { return !std::get_if<Addition>(t.get()); });
      // then extract the terms from those additions and add them to this
      // one
      std::vector<ExprHandle> to_add{part_it, a.terms.end()};
      a.terms.erase(part_it, a.terms.end());
      for (auto t : to_add) {
        auto sub_terms = std::get_if<Addition>(t.get())->terms;
        a.terms.insert(a.terms.end(), sub_terms.begin(), sub_terms.end());
      }
      to_add.clear();

      // idea: find first constant and just add everything on it
      for (auto it = a.terms.begin(); it < a.terms.end(); it++) {
        if (Constant *const_term = std::get_if<Constant>(it->get())) {
          for (auto jt = it + 1; jt < a.terms.end();) {
            if (Constant *const_term2 = std::get_if<Constant>(jt->get())) {
              const_term->value += const_term2->value;
              jt = a.terms.erase(jt);
            } else {
              jt++;
            }
          }
          // if it's somehow 0 after summing all constants up remove it
          if (const_term->value == 0 && a.terms.size() > 1) {
            a.terms.erase(it);
          }
          break;
        }
      }

      // adding variables up
      for (auto it = a.terms.begin(); it < a.terms.end(); it++) {
        if (Variable *var_term = std::get_if<Variable>(it->get())) {
          for (auto jt = it + 1; jt < a.terms.end();) {
            if (Variable *var_term2 = std::get_if<Variable>(jt->get())) {
              if (var_term->id == var_term2->id &&
                  var_term->exponent == var_term2->exponent) {
                var_term->factor += var_term2->factor;
                jt = a.terms.erase(jt);
              } else {
                jt++;
              }
            } else {
              jt++;
            }
          }
        }
      }

      if (a.terms.size() == 1) {
        return a.terms[0];
      }

      return std::make_shared<Expr>(a);
    }

    ExprHandle operator()(Multiplication &m) const {
      if (m.terms.size() == 1) {
        return m.terms[0];
      }
      for (auto &term : m.terms) {
        term = reduce(term);
      }

      // partition the vector into non-multiplications and multiplications
      auto part_it = std::partition(
          m.terms.begin(), m.terms.end(), [](const ExprHandle &t) {
            return !std::get_if<Multiplication>(t.get());
          });
      // then extract the terms from those multiplications and add them to
      // this one
      std::vector<ExprHandle> to_add{part_it, m.terms.end()};
      m.terms.erase(part_it, m.terms.end());
      for (auto t : to_add) {
        auto sub_terms = std::get_if<Multiplication>(t.get())->terms;
        m.terms.insert(m.terms.end(), sub_terms.begin(), sub_terms.end());
      }
      to_add.clear();

      // idea: find first constant and just mul everything on it
      for (auto it = m.terms.begin(); it < m.terms.end(); it++) {
        if (Constant *const_term = std::get_if<Constant>(it->get())) {
          for (auto jt = it + 1; jt < m.terms.end();) {
            if (Constant *const_term2 = std::get_if<Constant>(jt->get())) {
              const_term->value *= const_term2->value;
              jt = m.terms.erase(jt);
            } else {
              jt++;
            }
          }

          // if it's 0 we can just clear this Multiplication
          if (const_term->value == 0) {
            return constant(0);
          }
          // if it's somehow 1 after multing all constants up remove it
          else if (const_term->value == 1 && m.terms.size() > 1) {
            m.terms.erase(it);
          } else if (m.terms.size() > 1) {
            // since it's not 1, look for the first variable to mult it into its
            // factor and remove the constant
            for (auto &t : m.terms) {
              if (Variable *v = std::get_if<Variable>(t.get())) {
                v->factor *= const_term->value;
                m.terms.erase(it);
                break;
              }
            }
          }
          break;
        }
      }

      if (m.terms.size() == 1) {
        return m.terms[0];
      }

      // distributive law:
      // find an addition and create a multiplication for each of its summands
      std::vector<ExprHandle> factors;
      std::vector<ExprHandle> selected_addition_terms;
      for (auto &t : m.terms) {
        if (Addition *a = std::get_if<Addition>(t.get())) {
          selected_addition_terms = a->terms;
          for (auto &t : m.terms) {
            if (Addition *a2 = std::get_if<Addition>(t.get())) {
              if (a2 == a)
                continue; // we don't want the same one
            }
            factors.push_back(t);
          }
          break;
        }
      }
      std::vector<ExprHandle> new_multiplications;

      // only if an addition was actually found:
      if (!selected_addition_terms.empty()) {
        for (auto &t : selected_addition_terms) {
          std::vector<ExprHandle> new_term{factors};
          new_term.push_back(t);
          new_multiplications.push_back(mul(new_term));
        }
      }

      if (new_multiplications.size() > 0) {
        return add(new_multiplications);
      }

      return std::make_shared<Expr>(m);
    }
  };
  return std::visit(Reducer{}, *expr);
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
      if (v.factor != 1) {
        oss << v.factor;
      }
      oss << "n" << v.id;
      if (v.exponent != 1) {
        oss << "^" << v.exponent;
      }
      return oss.str();
    }
    std::string operator()(const Addition &a) const {
      std::ostringstream oss;
      for (std::size_t i{}; i < a.terms.size() - 1; i++) {
        if (std::get_if<Multiplication>(a.terms[i].get())) {
          oss << "(" << toString(a.terms[i]) << ")";
        } else {
          oss << toString(a.terms[i]);
        }
        oss << "+";
      }
      if (std::get_if<Multiplication>(a.terms[a.terms.size() - 1].get())) {
        oss << "(" << toString(a.terms[a.terms.size() - 1]) << ")";
      } else {
        oss << toString(a.terms[a.terms.size() - 1]);
      }

      return oss.str();
    }
    std::string operator()(const Multiplication &m) const {
      std::ostringstream oss;
      for (std::size_t i{}; i < m.terms.size() - 1; i++) {
        if (std::get_if<Addition>(m.terms[i].get())) {
          oss << "(" << toString(m.terms[i]) << ")";
        } else {
          oss << toString(m.terms[i]);
        }
        oss << "*";
      }
      if (std::get_if<Addition>(m.terms[m.terms.size() - 1].get())) {
        oss << "(" << toString(m.terms[m.terms.size() - 1]) << ")";
      } else {
        oss << toString(m.terms[m.terms.size() - 1]);
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
