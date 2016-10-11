/*
 * Copyright 2015 Paul Boutros. For details, see COPYING. Our lawyer cats sez:
 *
 * OICR makes no representations whatsoever as to the SOFTWARE contained
 * herein.  It is experimental in nature and is provided WITHOUT WARRANTY OF
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE OR ANY OTHER WARRANTY,
 * EXPRESS OR IMPLIED. OICR MAKES NO REPRESENTATION OR WARRANTY THAT THE USE OF
 * THIS SOFTWARE WILL NOT INFRINGE ANY PATENT OR OTHER PROPRIETARY RIGHT.  By
 * downloading this SOFTWARE, your Institution hereby indemnifies OICR against
 * any loss, claim, damage or liability, of whatsoever kind or nature, which
 * may arise from your Institution's respective use, handling or storage of the
 * SOFTWARE. If publications result from research using this SOFTWARE, we ask
 * that the Ontario Institute for Cancer Research be acknowledged and/or
 * credit be given to OICR scientists, as scientifically appropriate.
 */

#include <iostream>
#include <vector>
#include "bamql-compiler.hpp"

namespace bamql {

typedef std::shared_ptr<AstNode>(*ParseFunc)(ParseState &state);

/**
 * Handle terminal operators (final step in the recursive descent)
 */
static std::shared_ptr<AstNode> parseTerminal(ParseState &state) throw(
    ParseError) {

  state.parseSpace();
  if (state.empty()) {
    throw ParseError(state.where(),
                     "Reached end of input before completing parsing.");
  }
  if (*state == '!') {
    state.next();
    return std::make_shared<NotNode>(parseTerminal(state));
  }
  if (*state == '(') {
    state.next();
    size_t brace_index = state.where();
    auto node = AstNode::parse(state);
    state.parseSpace();
    if (!state.empty() && *state == ')') {
      state.next();
      return node;
    } else {
      throw ParseError(brace_index,
                       "Open brace has no matching closing brace.");
    }
  }
  auto literal = state.parseLiteral();
  if (literal) {
    return literal;
  }
  return state.parsePredicate();
}

class EquivalenceCheck {
public:
  EquivalenceCheck(const std::string &symbol_,
                   CreateICmp integer_compare_,
                   CreateFCmp float_compare_)
      : float_compare(float_compare_), integer_compare(integer_compare_),
        symbol(symbol_) {}

  bool parse(ParseState &state,
             std::shared_ptr<AstNode> &left) throw(ParseError) {
    auto where = state.where();
    if (!state.parseKeyword(symbol)) {
      return false;
    }
    state.parseSpace();
    auto right = parseTerminal(state);
    if (left->type() != right->type()) {
      throw ParseError(where, "Cannot compare different types.");
    }
    if (left->type() == FP) {
      left = std::make_shared<CompareFPNode>(float_compare, left, right, state);
      return true;
    }
    if (left->type() == INT) {
      left =
          std::make_shared<CompareIntNode>(integer_compare, left, right, state);
      return true;
    }
    if (left->type() == STR) {
      left =
          std::make_shared<CompareStrNode>(integer_compare, left, right, state);
      return true;
    }
    throw ParseError(where,
                     "Can only compare integers and float point numbers.");
  }

private:
  CreateFCmp float_compare;
  CreateICmp integer_compare;
  const std::string symbol;
};

std::vector<EquivalenceCheck> equivalence_checks = {
  { "==", &llvm::IRBuilder<>::CreateICmpEQ, &llvm::IRBuilder<>::CreateFCmpOEQ },
  { "!=", &llvm::IRBuilder<>::CreateICmpNE, &llvm::IRBuilder<>::CreateFCmpONE },
  { "<=",
    &llvm::IRBuilder<>::CreateICmpSLE,
    &llvm::IRBuilder<>::CreateFCmpOLE },
  { ">=",
    &llvm::IRBuilder<>::CreateICmpSGE,
    &llvm::IRBuilder<>::CreateFCmpOGE },
  { "<", &llvm::IRBuilder<>::CreateICmpSLT, &llvm::IRBuilder<>::CreateFCmpOLT },
  { ">", &llvm::IRBuilder<>::CreateICmpSGT, &llvm::IRBuilder<>::CreateFCmpOGT },
};

static std::shared_ptr<AstNode> parseComparison(ParseState &state) throw(
    ParseError) {
  auto left = parseTerminal(state);
  state.parseSpace();
  if (state.parseKeyword("~")) {
    if (left->type() != bamql::STR) {
      throw ParseError(state.where(),
                       "Regular expression may only be used on strings.");
    }
    state.parseSpace();
    auto pattern = state.parseRegEx();
    return std::make_shared<RegexNode>(left, std::move(pattern), state);
  }
  if (state.parseKeyword(":")) {
    if (left->type() != bamql::STR) {
      throw ParseError(state.where(),
                       "Regular expression may only be used on strings.");
    }
    state.parseSpace();
    auto start = state.where();
    while (!state.empty() && !isspace(*state)) {
      state.next();
    }
    if (start == state.where()) {
      throw ParseError(state.where(), "Expected valid glob.");
    }

    auto pattern = globToRegEx("^", state.strFrom(start), "$");
    return std::make_shared<RegexNode>(left, std::move(pattern), state);
  }
  for (auto it = equivalence_checks.begin();
       it != equivalence_checks.end() && !it->parse(state, left);
       it++)
    ;
  return left;
}

/**
 * Parse the implication (->) operator.
 */
static std::shared_ptr<AstNode> parseImplication(ParseState &state) throw(
    ParseError) {
  auto antecedent = parseComparison(state);
  state.parseSpace();
  while (state.parseKeyword("->")) {
    auto assertion = parseComparison(state);
    antecedent = std::make_shared<OrNode>(std::make_shared<NotNode>(antecedent),
                                          assertion);
    state.parseSpace();
  }
  return antecedent;
}

/**
 * Handle binary operators operators (the intermediate steps in recursive
 * descent)
 */
template <char S, class T, ParseFunc N>
static std::shared_ptr<AstNode> parseBinary(ParseState &state) throw(
    ParseError) {
  std::vector<std::shared_ptr<AstNode>> items;

  auto prev_where = state.where();
  std::shared_ptr<AstNode> node = N(state);
  state.parseSpace();
  while (!state.empty() && *state == S) {
    if (node->type() != bamql::BOOL) {
      throw ParseError(prev_where, "Expression must be Boolean.");
    }
    state.next();
    items.push_back(node);

    prev_where = state.where();
    node = N(state);
    if (node->type() != bamql::BOOL) {
      throw ParseError(prev_where, "Expression must be Boolean.");
    }
    state.parseSpace();
  }
  while (items.size() > 0) {
    node = std::make_shared<T>(items.back(), node);
    items.pop_back();
  }
  return node;
}

static std::shared_ptr<AstNode> parseIntermediate(ParseState &state) throw(
    ParseError) {
  return parseBinary<
      '|',
      OrNode,
      parseBinary<'^', XOrNode, parseBinary<'&', AndNode, parseImplication>>>(
      state);
}

/**
 * Handle conditional operators
 */
static std::shared_ptr<AstNode> parseConditional(ParseState &state) throw(
    ParseError) {
  auto cond_part = parseIntermediate(state);
  auto prev_where = state.where();
  state.parseSpace();
  if (!state.parseKeyword("then")) {
    return cond_part;
  }
  if (cond_part->type() != bamql::BOOL) {
    throw ParseError(prev_where, "Condition expression must be Boolean.");
  }
  auto then_part = parseIntermediate(state);
  if (!state.parseKeyword("else")) {
    throw ParseError(state.where(), "Ternary operator has no else.");
  }
  prev_where = state.where();
  auto else_part = parseIntermediate(state);
  if (then_part->type() != else_part->type()) {
    throw ParseError(
        prev_where, "The `then' and `else' expressions must be the same type.");
  }
  return std::make_shared<ConditionalNode>(cond_part, then_part, else_part);
}

/**
 * Handle let operators (first step in the recursive descent)
 */
std::shared_ptr<AstNode> AstNode::parse(ParseState &state) throw(ParseError) {
  state.parseSpace();
  std::shared_ptr<AstNode> node;
  if (state.parseKeyword("let")) {
    auto let = std::make_shared<BindingNode>(state);
    let->parse(state);
    node = let;
  } else {
    node = parseConditional(state);
  }
  return node;
}

/**
 * Parse a string into a syntax tree using the built-in logical operations and
 * the predicates provided.
 */
std::shared_ptr<AstNode> AstNode::parse(
    const std::string &input, PredicateMap &predicates) throw(ParseError) {
  ParseState state(input);
  state.push(predicates);
  std::shared_ptr<AstNode> node = AstNode::parse(state);
  state.pop(predicates);

  state.parseSpace();
  // check string is fully consumed
  if (!state.empty()) {
    throw ParseError(state.where(), "Junk at end of input.");
  }
  if (node->type() != bamql::BOOL) {
    throw ParseError(state.where(), "Whole expression must be Boolean.");
  }
  return node;
}

std::shared_ptr<AstNode> bamql::AstNode::parseWithLogging(
    const std::string &input, PredicateMap &predicates) {
  std::shared_ptr<AstNode> ast;
  try {
    ast = AstNode::parse(input, predicates);
  } catch (ParseError e) {
    std::cerr << "Error: " << e.what() << std::endl << input << std::endl;
    for (size_t i = 0; i < e.where(); i++) {
      std::cerr << " ";
    }
    std::cerr << "^" << std::endl;
  }
  return ast;
}
}
