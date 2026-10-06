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
  COMMENT,
  INTERPRETED_STRING_CONTENT,
  RAW_STRING_CONTENT,
  REJECTED_TOKEN,
  ERROR_SENTINEL,
};

enum { BYTE_ORDER_MARK = 0xFEFF };

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static bool is_white_space(int32_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// constraint: Go rejects NUL, a byte order mark after the first character of the file, and bytes that are not
// UTF-8, for which tree-sitter gives a negative character
static bool is_valid_in_source(int32_t c) { return c > 0 && c != BYTE_ORDER_MARK; }

// constraint: a line comment ends before the line end and before one carriage return there, which go/scanner drops
// The lexer stands on the second `/`. Returns false for a comment that Go rejects.
static bool scan_line_comment(TSLexer *lexer) {
  advance(lexer);
  bool content_is_valid = true;
  bool carriage_return_is_pending = false;
  while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
    int32_t c = lexer->lookahead;
    carriage_return_is_pending = c == '\r';
    if (carriage_return_is_pending) {
      lexer->mark_end(lexer);
    }
    if (!is_valid_in_source(c)) {
      content_is_valid = false;
    }
    advance(lexer);
  }
  if (!carriage_return_is_pending) {
    lexer->mark_end(lexer);
  }
  return content_is_valid;
}

// The lexer stands on the `*` after `/`. Returns false when the comment has no end.
static bool scan_general_comment(TSLexer *lexer, bool *crossed_newline, bool *is_valid) {
  advance(lexer);
  for (;;) {
    if (lexer->eof(lexer)) {
      return false;
    }
    int32_t c = lexer->lookahead;
    advance(lexer);
    if (c == '*' && lexer->lookahead == '/') {
      advance(lexer);
      return true;
    }
    if (c == '\n') {
      *crossed_newline = true;
    }
    if (!is_valid_in_source(c)) {
      *is_valid = false;
    }
  }
}

// Returns the first character of the next token on the line, after the general comments that stay on the line, and
// 0 when the line ends before a token.
static int32_t next_token_start_on_line(TSLexer *lexer) {
  for (;;) {
    while (!lexer->eof(lexer) && is_white_space(lexer->lookahead) && lexer->lookahead != '\n') {
      advance(lexer);
    }
    if (lexer->eof(lexer) || lexer->lookahead == '\n') {
      return 0;
    }
    if (lexer->lookahead != '/') {
      return lexer->lookahead;
    }
    advance(lexer);
    if (lexer->lookahead == '/') {
      return 0;
    }
    if (lexer->lookahead != '*') {
      return '/';
    }
    bool crossed_newline = false;
    bool ignored = true;
    if (!scan_general_comment(lexer, &crossed_newline, &ignored) || crossed_newline) {
      return 0;
    }
  }
}

static bool scan_string_content(TSLexer *lexer, bool is_raw) {
  bool has_content = false;
  while (!lexer->eof(lexer) && is_valid_in_source(lexer->lookahead)) {
    int32_t c = lexer->lookahead;
    if (is_raw ? c == '`' : (c == '"' || c == '\\' || c == '\n')) {
      break;
    }
    advance(lexer);
    has_content = true;
  }
  lexer->result_symbol = is_raw ? RAW_STRING_CONTENT : INTERPRETED_STRING_CONTENT;
  return has_content;
}

static bool is_comma_or_closing_bracket(int32_t c) { return c == ',' || c == ')' || c == ']' || c == '}'; }

static bool colon_is_a_token(TSLexer *lexer) {
  advance(lexer);
  return lexer->lookahead != '=';
}

static bool rune_starts_with_invalid_bytes(TSLexer *lexer) {
  advance(lexer);
  return !lexer->eof(lexer) && lexer->lookahead < 0;
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

static bool give(TSLexer *lexer, enum TokenType token) {
  lexer->result_symbol = token;
  return true;
}

// constraint: Go inserts a semicolon at a line end after the token before each marker; where no rule takes a
// semicolon, the scan returns `_rejected_token`, which no rule accepts
static bool take_line_end(TSLexer *lexer, const bool *valid_symbols) {
  if (valid_symbols[AUTOMATIC_SEMICOLON]) {
    return give(lexer, AUTOMATIC_SEMICOLON);
  }
  bool line_must_continue = valid_symbols[SAME_LINE] || valid_symbols[BRACE_ON_SAME_LINE] ||
                            valid_symbols[ELEMENT_END] || valid_symbols[COLON_ON_SAME_LINE] ||
                            valid_symbols[LINE_CONTINUES];
  return line_must_continue && give(lexer, REJECTED_TOKEN);
}

// constraint: a line comment and a general comment with a newline end the line like a newline (spec, Comments)
// The scan gives a line comment itself, for the end before a carriage return. It reads a general comment only to
// reject the text that Go rejects, and leaves the token to the lexer.
bool tree_sitter_golang_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  (void)payload;
  if (valid_symbols[ERROR_SENTINEL]) {
    return false;
  }
  if (valid_symbols[INTERPRETED_STRING_CONTENT] || valid_symbols[RAW_STRING_CONTENT]) {
    return scan_string_content(lexer, valid_symbols[RAW_STRING_CONTENT]);
  }
  lexer->mark_end(lexer);
  bool line_ended = false;
  while (!lexer->eof(lexer) && is_white_space(lexer->lookahead)) {
    line_ended = line_ended || lexer->lookahead == '\n';
    skip(lexer);
  }
  if (lexer->eof(lexer)) {
    return take_line_end(lexer, valid_symbols);
  }
  int32_t next = lexer->lookahead;
  if (next == '/') {
    advance(lexer);
    if (lexer->lookahead == '/') {
      if (take_line_end(lexer, valid_symbols)) {
        return true;
      }
      return give(lexer, scan_line_comment(lexer) ? COMMENT : REJECTED_TOKEN);
    }
    if (lexer->lookahead == '*') {
      bool crossed_newline = false;
      bool is_valid = true;
      if (!scan_general_comment(lexer, &crossed_newline, &is_valid)) {
        return give(lexer, REJECTED_TOKEN);
      }
      line_ended = line_ended || crossed_newline;
      next = line_ended ? 0 : next_token_start_on_line(lexer);
      line_ended = next == 0;
      if (!is_valid && !(line_ended && valid_symbols[AUTOMATIC_SEMICOLON])) {
        return give(lexer, REJECTED_TOKEN);
      }
    }
  }
  if (valid_symbols[STATEMENT_START] && next == '~') {
    return give(lexer, REJECTED_TOKEN);
  }
  if (line_ended) {
    return take_line_end(lexer, valid_symbols);
  }
  if (next == '\'' && rune_starts_with_invalid_bytes(lexer)) {
    return give(lexer, REJECTED_TOKEN);
  }
  if (valid_symbols[BRACE_ON_SAME_LINE] && next == '{') {
    return give(lexer, BRACE_ON_SAME_LINE);
  }
  if (valid_symbols[ELEMENT_END] && is_comma_or_closing_bracket(next)) {
    return give(lexer, ELEMENT_END);
  }
  if (valid_symbols[SAME_LINE]) {
    return give(lexer, SAME_LINE);
  }
  if (valid_symbols[COLON_ON_SAME_LINE] && next == ':' && colon_is_a_token(lexer)) {
    return give(lexer, COLON_ON_SAME_LINE);
  }
  return false;
}
