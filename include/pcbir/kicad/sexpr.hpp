// SPDX-License-Identifier: Apache-2.0
#ifndef PCBIR_KICAD_SEXPR_HPP
#define PCBIR_KICAD_SEXPR_HPP

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pcbir::kicad {

// Thrown for a syntactic S-expression error (unbalanced parens, unterminated
// string, no expression at all) -- distinct from a *semantic* KiCad-import
// error (unknown section, unsupported version), which the importer built on
// top of this parser reports separately (docs/rfcs/0003-kicad-importer-exporter.md).
class SExprParseError : public std::runtime_error {
public:
  explicit SExprParseError(const std::string& message) : std::runtime_error(message) {}
};

// One node of a generic S-expression tree. KiCad's `.kicad_pcb`/`.kicad_mod`
// grammar is exactly parenthesized lists of atoms -- this type carries no
// KiCad-specific semantics at all (docs/rfcs/0003-kicad-importer-exporter.md
// -- "S-expression parser"), so it's independently testable without any real
// board fixture and reusable for any S-expression-based format. Whether an
// unquoted Symbol's text should be read as an integer, a float, or a literal
// keyword (e.g. `yes`/`no`/`signal`) is a semantic-layer decision the
// importer makes per field, not something the parser can know.
struct SExpr {
  enum class Kind : uint8_t { List, Symbol, String };

  Kind kind = Kind::List;
  // Symbol/String only: the token's text, already unescaped for String
  // (surrounding quotes stripped, `\"`/`\\` resolved). Empty and unused for
  // List.
  std::string text;
  // List only: the list's child expressions, in file order. Empty and
  // unused for Symbol/String.
  std::vector<SExpr> children;

  [[nodiscard]] bool is_list() const { return kind == Kind::List; }
  [[nodiscard]] bool is_symbol() const { return kind == Kind::Symbol; }
  [[nodiscard]] bool is_string() const { return kind == Kind::String; }

  // A tree type's structural equality is inherently recursive (comparing
  // `children` recurses into each child's own `==`); depth is bounded by
  // whatever produced the tree -- parse_sexpr()'s own MAX_NESTING_DEPTH for
  // any parsed result -- the same "bounded, so safe" reasoning as
  // geometry/bezier.cpp's subdivide().
  // NOLINTNEXTLINE(misc-no-recursion)
  friend bool operator==(const SExpr&, const SExpr&) = default;
};

// Parses exactly one top-level S-expression from `text` (a `.kicad_pcb` file
// is always a single root list, `(kicad_pcb ...)`). Throws SExprParseError
// on any syntax error: unbalanced parens, an unterminated string, trailing
// non-whitespace content after the root expression, or no expression at
// all.
[[nodiscard]] SExpr parse_sexpr(std::string_view text);

// Renders `root` back to S-expression text: a Symbol writes as its bare
// text, a String writes quoted with `"`/`\` escaped, and a List writes each
// atom child inline with its siblings but breaks before and indents (2
// spaces per nesting level) each list child -- the same two-style
// formatting real `pcbnew`-authored files use (a `(pad "1" smd rect (at 1
// 2) (size 1.6 1.2))`-style leaf line, vs. a section like `(layers ...)`
// whose children are each their own line). This is purely cosmetic:
// `parse_sexpr` is whitespace-insensitive, so the exporter (export.hpp)
// need not match `pcbnew`'s exact byte-for-byte layout for the output to be
// valid, reparseable, and reloadable in KiCad. `parse_sexpr(write_sexpr(x))
// == x` for any `x` this parser could have produced.
[[nodiscard]] std::string write_sexpr(const SExpr& root);

} // namespace pcbir::kicad

#endif // PCBIR_KICAD_SEXPR_HPP
