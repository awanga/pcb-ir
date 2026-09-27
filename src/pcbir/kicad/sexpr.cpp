// SPDX-License-Identifier: Apache-2.0
#include "pcbir/kicad/sexpr.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace pcbir::kicad {

namespace {

// A real `.kicad_pcb` file nests only a few dozen levels deep at most; this
// bounds recursion depth against a maliciously or accidentally deeply
// nested input (parsing untrusted files, per the same discipline as the
// serialization fuzz target) rather than risking a stack overflow.
constexpr std::size_t MAX_NESTING_DEPTH = 256;

[[nodiscard]] bool is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

[[nodiscard]] bool is_atom_terminator(char c) {
  return is_whitespace(c) || c == '(' || c == ')' || c == '"';
}

// Recursive-descent parser over a fixed input buffer. Holds no KiCad
// semantics (see sexpr.hpp) -- purely the generic parenthesized-atom
// grammar every S-expression format shares.
class Parser {
public:
  explicit Parser(std::string_view text) : text_(text) {}

  [[nodiscard]] SExpr parse_root() {
    skip_whitespace();
    if (pos_ >= text_.size()) {
      throw SExprParseError("no expression found (empty input)");
    }
    SExpr root = parse_expr();
    skip_whitespace();
    if (pos_ != text_.size()) {
      throw error("trailing content after the root expression");
    }
    return root;
  }

private:
  [[nodiscard]] SExprParseError error(std::string_view message) const {
    return SExprParseError(std::string(message) + " (at byte offset " + std::to_string(pos_) + ")");
  }

  void skip_whitespace() {
    while (pos_ < text_.size() && is_whitespace(text_.at(pos_))) {
      ++pos_;
    }
  }

  // NOLINTNEXTLINE(misc-no-recursion)
  [[nodiscard]] SExpr parse_expr() {
    const char c = text_.at(pos_);
    if (c == '(') {
      return parse_list();
    }
    if (c == ')') {
      throw error("unexpected ')'");
    }
    if (c == '"') {
      return parse_string();
    }
    return parse_symbol();
  }

  // Mutually recursive with parse_expr(); depth is bounded by
  // MAX_NESTING_DEPTH, so this is safe despite misc-no-recursion (mirrors
  // geometry/bezier.cpp's subdivide()).
  // NOLINTNEXTLINE(misc-no-recursion)
  [[nodiscard]] SExpr parse_list() {
    if (depth_ >= MAX_NESTING_DEPTH) {
      throw error("exceeded maximum nesting depth");
    }
    ++pos_; // consume '('
    ++depth_;
    SExpr node{.kind = SExpr::Kind::List, .text = {}, .children = {}};
    while (true) {
      skip_whitespace();
      if (pos_ >= text_.size()) {
        throw error("unterminated list, missing ')'");
      }
      if (text_.at(pos_) == ')') {
        ++pos_; // consume ')'
        --depth_;
        return node;
      }
      node.children.push_back(parse_expr());
    }
  }

  [[nodiscard]] SExpr parse_string() {
    const std::size_t start = pos_;
    ++pos_; // consume opening '"'
    std::string value;
    while (true) {
      if (pos_ >= text_.size()) {
        pos_ = start;
        throw error("unterminated string literal");
      }
      const char c = text_.at(pos_);
      if (c == '"') {
        ++pos_; // consume closing '"'
        return SExpr{.kind = SExpr::Kind::String, .text = std::move(value), .children = {}};
      }
      if (c == '\\' && pos_ + 1 < text_.size()) {
        const char next = text_.at(pos_ + 1);
        if (next == '"' || next == '\\') {
          value.push_back(next);
          pos_ += 2;
          continue;
        }
      }
      value.push_back(c);
      ++pos_;
    }
  }

  [[nodiscard]] SExpr parse_symbol() {
    const std::size_t start = pos_;
    while (pos_ < text_.size() && !is_atom_terminator(text_.at(pos_))) {
      ++pos_;
    }
    return SExpr{.kind = SExpr::Kind::Symbol,
                 .text = std::string(text_.substr(start, pos_ - start)),
                 .children = {}};
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  std::size_t depth_ = 0;
};

} // namespace

SExpr parse_sexpr(std::string_view text) {
  return Parser(text).parse_root();
}

} // namespace pcbir::kicad
