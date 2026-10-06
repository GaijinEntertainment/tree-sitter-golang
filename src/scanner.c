#include "tree_sitter/parser.h"

// constraint: the order matches the externals array of grammar.js
enum TokenType {
  AUTOMATIC_SEMICOLON,
  SAME_LINE,
  BRACE_ON_SAME_LINE,
  ELEMENT_END,
  COLON_ON_SAME_LINE,
  LINE_CONTINUES,
  STATEMENT_START,
  NEVER_RETURNED,
  REJECTED_TOKEN,
  ERROR_SENTINEL,
};

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static bool is_horizontal_space(int32_t c) { return c == ' ' || c == '\t' || c == '\r'; }

static void skip_general_comment_body(TSLexer *lexer, bool *crossed_newline) {
  while (!lexer->eof(lexer)) {
    if (lexer->lookahead == '\n') {
      *crossed_newline = true;
    }
    if (lexer->lookahead != '*') {
      advance(lexer);
      continue;
    }
    advance(lexer);
    if (lexer->lookahead == '/') {
      advance(lexer);
      return;
    }
  }
}

// constraint: a line comment and a general comment with a newline end the line like a newline (spec, Comments)
// Returns the first character of the next token, and 0 at the end of the input. `*line_ended` tells that the line
// ends before that token; with `stop_at_line_end`, the scan stops there and returns 0.
static int32_t next_token_start(TSLexer *lexer, bool stop_at_line_end, bool *line_ended) {
  *line_ended = false;
  for (;;) {
    if (lexer->eof(lexer)) {
      *line_ended = true;
      return 0;
    }
    if (lexer->lookahead == '\n') {
      *line_ended = true;
      if (stop_at_line_end) {
        return 0;
      }
      advance(lexer);
      continue;
    }
    if (is_horizontal_space(lexer->lookahead)) {
      advance(lexer);
      continue;
    }
    if (lexer->lookahead != '/') {
      return lexer->lookahead;
    }
    advance(lexer);
    if (lexer->lookahead == '/') {
      *line_ended = true;
      if (stop_at_line_end) {
        return 0;
      }
      while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
        advance(lexer);
      }
      continue;
    }
    if (lexer->lookahead != '*') {
      return '/';
    }
    advance(lexer);
    skip_general_comment_body(lexer, line_ended);
    if (*line_ended && stop_at_line_end) {
      return 0;
    }
  }
}

static bool is_comma_or_closing_bracket(int32_t c) { return c == ',' || c == ')' || c == ']' || c == '}'; }

static bool colon_is_a_token(TSLexer *lexer) {
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

// constraint: Go inserts a semicolon at a line end after the token before each marker; where no rule takes a
// semicolon, the scan returns `_rejected_token`, which no rule accepts
bool tree_sitter_golang_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  (void)payload;
  if (valid_symbols[ERROR_SENTINEL]) {
    return false;
  }
  bool line_must_continue = valid_symbols[SAME_LINE] || valid_symbols[BRACE_ON_SAME_LINE] ||
                            valid_symbols[ELEMENT_END] || valid_symbols[COLON_ON_SAME_LINE] ||
                            valid_symbols[LINE_CONTINUES];
  if (!valid_symbols[AUTOMATIC_SEMICOLON] && !line_must_continue && !valid_symbols[STATEMENT_START]) {
    return false;
  }
  lexer->mark_end(lexer);
  bool line_ended;
  int32_t next = next_token_start(lexer, !valid_symbols[STATEMENT_START], &line_ended);
  if (valid_symbols[STATEMENT_START] && next == '~') {
    lexer->result_symbol = REJECTED_TOKEN;
    return true;
  }
  if (line_ended) {
    if (!valid_symbols[AUTOMATIC_SEMICOLON] && !line_must_continue) {
      return false;
    }
    lexer->result_symbol = valid_symbols[AUTOMATIC_SEMICOLON] ? AUTOMATIC_SEMICOLON : REJECTED_TOKEN;
    return true;
  }
  if (valid_symbols[BRACE_ON_SAME_LINE] && next == '{') {
    lexer->result_symbol = BRACE_ON_SAME_LINE;
    return true;
  }
  if (valid_symbols[ELEMENT_END] && is_comma_or_closing_bracket(next)) {
    lexer->result_symbol = ELEMENT_END;
    return true;
  }
  if (valid_symbols[SAME_LINE]) {
    lexer->result_symbol = SAME_LINE;
    return true;
  }
  if (valid_symbols[COLON_ON_SAME_LINE] && next == ':' && colon_is_a_token(lexer)) {
    lexer->result_symbol = COLON_ON_SAME_LINE;
    return true;
  }
  return false;
}
