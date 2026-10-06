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
  TYPE_PARAMETERS_FOLLOW,
  NO_TYPE_PARAMETERS,
  COMMENT,
  INTERPRETED_STRING_CONTENT,
  RAW_STRING_CONTENT,
  REJECTED_TOKEN,
  ERROR_SENTINEL,
};

enum {
  BYTE_ORDER_MARK = 0xFEFF,
  DIRECTIVE_PREFIX_LENGTH = 5,
  NOT_A_DIRECTIVE = DIRECTIVE_PREFIX_LENGTH + 1,
  MAX_LINE_OR_COLUMN = 1 << 30,
  MAX_WORD_LENGTH = 9,
  MAX_NESTING = 16,
};

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static bool is_white_space(int32_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// constraint: Go rejects NUL, a byte order mark after the first character of the file, and bytes that are not
// UTF-8, for which tree-sitter gives a negative character
static bool is_valid_in_source(int32_t c) { return c > 0 && c != BYTE_ORDER_MARK; }

typedef struct {
  uint64_t value;
  unsigned digit_count;
  bool has_other_character;
  bool overflows;
} DirectiveNumber;

// constraint: a comment whose text starts with `line ` is a line directive; the text after its last `:` is the
// line, or the column when the text before it is a number too, and both parsers reject a line or a column that is
// not a number from 1 to 2^30
typedef struct {
  unsigned matched_prefix_length;
  unsigned colon_count;
  DirectiveNumber last;
  DirectiveNumber before_last;
} LineDirective;

static void read_directive_character(LineDirective *directive, int32_t c) {
  static const char prefix[DIRECTIVE_PREFIX_LENGTH + 1] = "line ";
  if (directive->matched_prefix_length < DIRECTIVE_PREFIX_LENGTH) {
    bool matches = c == prefix[directive->matched_prefix_length];
    directive->matched_prefix_length = matches ? directive->matched_prefix_length + 1 : NOT_A_DIRECTIVE;
    return;
  }
  if (directive->matched_prefix_length == NOT_A_DIRECTIVE) {
    return;
  }
  if (c == ':') {
    directive->colon_count++;
    directive->before_last = directive->last;
    directive->last = (DirectiveNumber){0};
    return;
  }
  DirectiveNumber *number = &directive->last;
  if (c < '0' || c > '9') {
    number->has_other_character = true;
    return;
  }
  uint64_t digit = (uint64_t)(c - '0');
  if (number->value > (UINT64_MAX - digit) / 10) {
    number->overflows = true;
    return;
  }
  number->value = number->value * 10 + digit;
  number->digit_count++;
}

static bool is_number(const DirectiveNumber *number) {
  return number->digit_count > 0 && !number->has_other_character && !number->overflows;
}

static bool is_line_or_column(uint64_t value) { return value >= 1 && value <= MAX_LINE_OR_COLUMN; }

static bool directive_is_valid(const LineDirective *directive) {
  if (directive->matched_prefix_length != DIRECTIVE_PREFIX_LENGTH || directive->colon_count == 0) {
    return true;
  }
  if (!is_number(&directive->last)) {
    return false;
  }
  if (directive->colon_count > 1 && is_number(&directive->before_last)) {
    return is_line_or_column(directive->last.value) && is_line_or_column(directive->before_last.value);
  }
  return is_line_or_column(directive->last.value);
}

// constraint: a line comment ends before the line end and before one carriage return there, which go/scanner drops;
// it is a line directive only when it starts the line
// The lexer stands on the second `/`. Returns false for a comment that Go rejects.
static bool scan_line_comment(TSLexer *lexer) {
  advance(lexer);
  LineDirective directive = {0};
  bool starts_line = false;
  bool content_is_valid = true;
  bool carriage_return_is_pending = false;
  while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
    int32_t c = lexer->lookahead;
    if (carriage_return_is_pending) {
      read_directive_character(&directive, '\r');
    }
    carriage_return_is_pending = c == '\r';
    if (carriage_return_is_pending) {
      lexer->mark_end(lexer);
      advance(lexer);
      continue;
    }
    if (!is_valid_in_source(c)) {
      content_is_valid = false;
    }
    advance(lexer);
    bool prefix_was_incomplete = directive.matched_prefix_length < DIRECTIVE_PREFIX_LENGTH;
    read_directive_character(&directive, c);
    if (prefix_was_incomplete && directive.matched_prefix_length == DIRECTIVE_PREFIX_LENGTH) {
      starts_line = lexer->get_column(lexer) == 2 + DIRECTIVE_PREFIX_LENGTH;
    }
  }
  if (!carriage_return_is_pending) {
    lexer->mark_end(lexer);
  }
  return content_is_valid && (!starts_line || directive_is_valid(&directive));
}

// The lexer stands on the `*` after `/`. Returns false when the comment has no end.
static bool scan_general_comment(TSLexer *lexer, bool *crossed_newline, bool *is_valid) {
  advance(lexer);
  LineDirective directive = {0};
  for (;;) {
    if (lexer->eof(lexer)) {
      return false;
    }
    int32_t c = lexer->lookahead;
    advance(lexer);
    if (c == '*' && lexer->lookahead == '/') {
      advance(lexer);
      break;
    }
    if (c == '\n') {
      *crossed_newline = true;
    }
    if (!is_valid_in_source(c)) {
      *is_valid = false;
    }
    read_directive_character(&directive, c);
  }
  if (!directive_is_valid(&directive)) {
    *is_valid = false;
  }
  return true;
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

static bool is_word_character(int32_t c) {
  return c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80;
}

static bool is_digit(int32_t c) { return c >= '0' && c <= '9'; }

// Returns false at a `/` that starts no comment, after that `/`, and for a general comment without an end.
static bool skip_white_space_and_comments(TSLexer *lexer, bool *passed_division_operator) {
  *passed_division_operator = false;
  for (;;) {
    while (!lexer->eof(lexer) && is_white_space(lexer->lookahead)) {
      advance(lexer);
    }
    if (lexer->lookahead != '/') {
      return true;
    }
    advance(lexer);
    bool ignored = false;
    if (lexer->lookahead == '/') {
      while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
        advance(lexer);
      }
    } else if (lexer->lookahead != '*') {
      *passed_division_operator = true;
      return true;
    } else if (!scan_general_comment(lexer, &ignored, &ignored)) {
      return false;
    }
  }
}

static bool starts_type_literal(const char *word) {
  static const char *const keywords[] = {"chan", "func", "interface", "map", "struct"};
  for (unsigned i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
    const char *keyword = keywords[i];
    unsigned at = 0;
    while (word[at] != 0 && word[at] == keyword[at]) {
      at++;
    }
    if (word[at] == keyword[at]) {
      return true;
    }
  }
  return false;
}

// Reads a word, and tells whether it is a keyword that starts a type literal.
static bool read_word_that_starts_type_literal(TSLexer *lexer) {
  char word[MAX_WORD_LENGTH + 1];
  unsigned length = 0;
  while (!lexer->eof(lexer) && is_word_character(lexer->lookahead)) {
    if (length < MAX_WORD_LENGTH) {
      word[length] = lexer->lookahead < 0x80 ? (char)lexer->lookahead : '?';
    }
    length++;
    advance(lexer);
  }
  if (length > MAX_WORD_LENGTH) {
    return false;
  }
  word[length] = 0;
  return starts_type_literal(word);
}

enum BracketKind { UNDECIDED_BRACKET, TYPE_PARAMETER_BRACKET, ARRAY_LENGTH_BRACKET };

typedef struct {
  bool shows_type_element;
  bool has_unary_operator;
} Nesting;

// Reads one binary operator. Returns false for a text that is no binary operator of an expression.
static bool read_binary_operator(TSLexer *lexer) {
  int32_t first = lexer->lookahead;
  advance(lexer);
  int32_t second = lexer->lookahead;
  switch (first) {
    case '+':
    case '-':
    case '*':
    case '%':
    case '^':
      return true;
    case '&':
      if (second == '^' || second == '&') {
        advance(lexer);
      }
      return true;
    case '|':
      if (second == '|') {
        advance(lexer);
      }
      return true;
    case '<':
    case '>':
      if (second == '-') {
        return false;
      }
      if (second == first || second == '=') {
        advance(lexer);
      }
      return true;
    case '=':
    case '!':
      if (second != '=') {
        return false;
      }
      advance(lexer);
      return true;
    default:
      return false;
  }
}

// constraint: both parsers read the bracket after the name of a type declaration as an expression when it starts
// with a name, and take a type parameter list where the expression splits into a name and a constraint: `P *C`
// and `P (C)` split only before a comma, or when a term of the expression is a type literal or a `~` term that
// stands under no operator but `|` and parentheses (extractName and isTypeElem of go/parser); a `[` after the name
// starts a constraint in every text
// The lexer stands on the `[`. The scan settles only the texts that hold no such term and no comma.
static enum BracketKind classify_type_declaration_bracket(TSLexer *lexer) {
  advance(lexer);
  bool passed_division_operator;
  if (!skip_white_space_and_comments(lexer, &passed_division_operator) || passed_division_operator ||
      !is_word_character(lexer->lookahead) || is_digit(lexer->lookahead) ||
      read_word_that_starts_type_literal(lexer)) {
    return UNDECIDED_BRACKET;
  }
  if (!skip_white_space_and_comments(lexer, &passed_division_operator) || passed_division_operator) {
    return UNDECIDED_BRACKET;
  }
  if (lexer->lookahead == '[') {
    return TYPE_PARAMETER_BRACKET;
  }
  if (lexer->lookahead != '*' && lexer->lookahead != '(') {
    return UNDECIDED_BRACKET;
  }
  Nesting levels[MAX_NESTING] = {{.shows_type_element = true, .has_unary_operator = false}};
  unsigned depth = 0;
  if (lexer->lookahead == '(') {
    depth = 1;
    levels[1] = levels[0];
  }
  advance(lexer);
  bool expects_operand = true;
  for (;;) {
    Nesting *level = &levels[depth];
    if (!skip_white_space_and_comments(lexer, &passed_division_operator) || lexer->eof(lexer)) {
      return UNDECIDED_BRACKET;
    }
    if (passed_division_operator) {
      if (expects_operand) {
        return UNDECIDED_BRACKET;
      }
      level->has_unary_operator = false;
      expects_operand = true;
      continue;
    }
    int32_t c = lexer->lookahead;
    bool shows_type_element = expects_operand && level->shows_type_element && !level->has_unary_operator;
    if (is_word_character(c)) {
      bool is_number = is_digit(c);
      if (read_word_that_starts_type_literal(lexer) && !is_number && shows_type_element) {
        return UNDECIDED_BRACKET;
      }
      expects_operand = false;
      continue;
    }
    switch (c) {
      case '(':
      case '[':
      case '{':
        if ((c == '[' && shows_type_element) || depth + 1 == MAX_NESTING) {
          return UNDECIDED_BRACKET;
        }
        depth++;
        levels[depth] = (Nesting){.shows_type_element = c == '(' && shows_type_element, .has_unary_operator = false};
        advance(lexer);
        expects_operand = true;
        continue;
      case ')':
      case ']':
      case '}':
        if (depth == 0) {
          return c == ']' ? ARRAY_LENGTH_BRACKET : UNDECIDED_BRACKET;
        }
        depth--;
        advance(lexer);
        expects_operand = false;
        continue;
      case ',':
        if (depth == 0) {
          return UNDECIDED_BRACKET;
        }
        advance(lexer);
        level->has_unary_operator = false;
        expects_operand = true;
        continue;
      case '.':
        advance(lexer);
        continue;
      default:
        break;
    }
    if (!expects_operand) {
      if (!read_binary_operator(lexer)) {
        return UNDECIDED_BRACKET;
      }
      level->has_unary_operator = false;
      expects_operand = true;
      continue;
    }
    if (shows_type_element && (c == '~' || c == '<')) {
      return UNDECIDED_BRACKET;
    }
    if (c != '+' && c != '-' && c != '!' && c != '^' && c != '*' && c != '&' && c != '~' && c != '<') {
      return UNDECIDED_BRACKET;
    }
    advance(lexer);
    if (c == '<') {
      if (lexer->lookahead != '-') {
        return UNDECIDED_BRACKET;
      }
      advance(lexer);
    }
    level->has_unary_operator = true;
  }
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
  if (valid_symbols[TYPE_PARAMETERS_FOLLOW] && next == '[') {
    enum BracketKind kind = classify_type_declaration_bracket(lexer);
    if (kind != UNDECIDED_BRACKET) {
      return give(lexer, kind == TYPE_PARAMETER_BRACKET ? TYPE_PARAMETERS_FOLLOW : NO_TYPE_PARAMETERS);
    }
  }
  if (valid_symbols[SAME_LINE]) {
    return give(lexer, SAME_LINE);
  }
  if (valid_symbols[COLON_ON_SAME_LINE] && next == ':' && colon_is_a_token(lexer)) {
    return give(lexer, COLON_ON_SAME_LINE);
  }
  return false;
}
