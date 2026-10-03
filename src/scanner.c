#include "tree_sitter/parser.h"

// constraint: the order matches the externals array of grammar.js
enum TokenType {
  AUTOMATIC_SEMICOLON,
  SAME_LINE,
  BRACE_ON_SAME_LINE,
  ELEMENT_END,
  COLON_ON_SAME_LINE,
  LINE_BREAK_AFTER_ELEMENT,
  ERROR_SENTINEL,
};

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static bool is_horizontal_space(int32_t c) { return c == ' ' || c == '\t' || c == '\r'; }

// constraint: a line comment and a general comment with a newline end the line like a newline (spec, Comments)
static bool line_ends_before_next_token(TSLexer *lexer) {
  for (;;) {
    if (lexer->eof(lexer) || lexer->lookahead == '\n') {
      return true;
    }
    if (is_horizontal_space(lexer->lookahead)) {
      advance(lexer);
      continue;
    }
    if (lexer->lookahead != '/') {
      return false;
    }
    advance(lexer);
    if (lexer->lookahead == '/') {
      return true;
    }
    if (lexer->lookahead != '*') {
      return false;
    }
    advance(lexer);
    bool crossed_newline = false;
    for (;;) {
      if (lexer->eof(lexer)) {
        return crossed_newline;
      }
      if (lexer->lookahead == '\n') {
        crossed_newline = true;
      }
      if (lexer->lookahead == '*') {
        advance(lexer);
        if (lexer->lookahead == '/') {
          advance(lexer);
          break;
        }
        continue;
      }
      advance(lexer);
    }
    if (crossed_newline) {
      return true;
    }
  }
}

static bool is_comma_or_closing_bracket(int32_t c) { return c == ',' || c == ')' || c == ']' || c == '}'; }

static bool next_token_is_colon(TSLexer *lexer) {
  if (lexer->lookahead != ':') {
    return false;
  }
  advance(lexer);
  return lexer->lookahead != '=';
}

void *tree_sitter_golang_external_scanner_create(void) { return NULL; }

void tree_sitter_golang_external_scanner_destroy(void *payload) { (void)payload; }

unsigned tree_sitter_golang_external_scanner_serialize(void *payload, char *buffer) {
  (void)payload;
  (void)buffer;
  return 0;
}

void tree_sitter_golang_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  (void)payload;
  (void)buffer;
  (void)length;
}

// constraint: Go inserts a semicolon at a line end after the last token of a list element; no rule accepts it there
bool tree_sitter_golang_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  (void)payload;
  if (valid_symbols[ERROR_SENTINEL]) {
    return false;
  }
  bool after_element = valid_symbols[ELEMENT_END] || valid_symbols[COLON_ON_SAME_LINE];
  if (!valid_symbols[AUTOMATIC_SEMICOLON] && !valid_symbols[SAME_LINE] && !valid_symbols[BRACE_ON_SAME_LINE] &&
      !after_element) {
    return false;
  }
  lexer->mark_end(lexer);
  if (line_ends_before_next_token(lexer)) {
    if (valid_symbols[AUTOMATIC_SEMICOLON]) {
      lexer->result_symbol = AUTOMATIC_SEMICOLON;
      return true;
    }
    if (after_element) {
      lexer->result_symbol = LINE_BREAK_AFTER_ELEMENT;
      return true;
    }
    return false;
  }
  if (valid_symbols[BRACE_ON_SAME_LINE] && lexer->lookahead == '{') {
    lexer->result_symbol = BRACE_ON_SAME_LINE;
    return true;
  }
  if (valid_symbols[ELEMENT_END] && is_comma_or_closing_bracket(lexer->lookahead)) {
    lexer->result_symbol = ELEMENT_END;
    return true;
  }
  if (valid_symbols[SAME_LINE]) {
    lexer->result_symbol = SAME_LINE;
    return true;
  }
  if (valid_symbols[COLON_ON_SAME_LINE] && next_token_is_colon(lexer)) {
    lexer->result_symbol = COLON_ON_SAME_LINE;
    return true;
  }
  return false;
}
