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
  RECOVERY_LINE_END,
  FRAGMENT_START,
  ERROR_SENTINEL,
};

enum {
  BYTE_ORDER_MARK = 0xFEFF,
  DIRECTIVE_PREFIX_LENGTH = 5,
  NOT_A_DIRECTIVE = DIRECTIVE_PREFIX_LENGTH + 1,
  MAX_LINE_OR_COLUMN = 1 << 30,
  MAX_WORD_LENGTH = 11,
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
// it is a line directive only when it starts the line, and a byte order mark before it moves it off the line start
// The lexer stands on the second `/`. Returns false for a comment that Go rejects.
static bool scan_line_comment(TSLexer *lexer, bool follows_byte_order_mark) {
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
      starts_line = !follows_byte_order_mark && lexer->get_column(lexer) == 2 + DIRECTIVE_PREFIX_LENGTH;
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

enum WordKind {
  NO_WORD,
  NAME_WORD,
  NUMBER_WORD,
  KEYWORD_WORD,
  CHAN_WORD,
  FUNC_WORD,
  INTERFACE_WORD,
  MAP_WORD,
  STRUCT_WORD,
};

static bool starts_type_literal(enum WordKind word) { return word >= CHAN_WORD; }

static bool is_same_word(const char *word, const char *other) {
  unsigned at = 0;
  while (word[at] != 0 && word[at] == other[at]) {
    at++;
  }
  return word[at] == other[at];
}

// constraint: the keywords match `KEYWORDS` of grammar.js
static enum WordKind kind_of_word(const char *word) {
  static const char *const type_keywords[] = {"chan", "func", "interface", "map", "struct"};
  static const enum WordKind type_kinds[] = {CHAN_WORD, FUNC_WORD, INTERFACE_WORD, MAP_WORD, STRUCT_WORD};
  static const char *const other_keywords[] = {
    "break", "case",   "const", "continue", "default", "defer",  "else",   "fallthrough", "for",  "go",
    "goto",  "if",     "import", "package", "range",   "return", "select", "switch",      "type", "var",
  };
  for (unsigned i = 0; i < sizeof(type_keywords) / sizeof(type_keywords[0]); i++) {
    if (is_same_word(word, type_keywords[i])) {
      return type_kinds[i];
    }
  }
  for (unsigned i = 0; i < sizeof(other_keywords) / sizeof(other_keywords[0]); i++) {
    if (is_same_word(word, other_keywords[i])) {
      return KEYWORD_WORD;
    }
  }
  return NAME_WORD;
}

// Reads an identifier, a keyword, or a number literal with its radix point and the sign of its exponent.
static enum WordKind read_word(TSLexer *lexer) {
  char word[MAX_WORD_LENGTH + 1];
  unsigned length = 0;
  bool is_number = is_digit(lexer->lookahead);
  bool is_hexadecimal = false;
  bool has_radix_point = false;
  int32_t previous = 0;
  while (!lexer->eof(lexer)) {
    int32_t c = lexer->lookahead;
    bool is_exponent_mark = is_hexadecimal ? previous == 'p' || previous == 'P' : previous == 'e' || previous == 'E';
    bool is_exponent_sign = is_number && is_exponent_mark && (c == '+' || c == '-');
    bool is_radix_point = is_number && !has_radix_point && c == '.';
    if (!is_word_character(c) && !is_exponent_sign && !is_radix_point) {
      break;
    }
    is_hexadecimal = is_hexadecimal || (is_number && length == 1 && (c == 'x' || c == 'X'));
    has_radix_point = has_radix_point || is_radix_point;
    if (length < MAX_WORD_LENGTH) {
      word[length] = c < 0x80 ? (char)c : '?';
    }
    length++;
    previous = c;
    advance(lexer);
  }
  if (is_number) {
    return NUMBER_WORD;
  }
  if (length > MAX_WORD_LENGTH) {
    return NAME_WORD;
  }
  word[length] = 0;
  return kind_of_word(word);
}

// constraint: the lexer shows one character, so the scan knows that a `/` or a `<` starts a binary operator only
// after it has passed that character; `passed_operator_start` holds it until `read_binary_operator` takes it
typedef struct {
  TSLexer *lexer;
  int32_t passed_operator_start;
} TokenReader;

// Skips white space and comments. Returns the first character of the next token, and 0 where the text ends or a
// general comment has no end.
static int32_t next_token_start(TokenReader *reader) {
  if (reader->passed_operator_start != 0) {
    return reader->passed_operator_start;
  }
  bool passed_division_operator;
  if (!skip_white_space_and_comments(reader->lexer, &passed_division_operator)) {
    return 0;
  }
  if (passed_division_operator) {
    reader->passed_operator_start = '/';
    return '/';
  }
  return reader->lexer->eof(reader->lexer) ? 0 : reader->lexer->lookahead;
}

// The lexer stands on the quote. Returns false for a literal without an end.
static bool skip_string_or_rune(TSLexer *lexer) {
  int32_t quote = lexer->lookahead;
  advance(lexer);
  while (!lexer->eof(lexer)) {
    int32_t c = lexer->lookahead;
    advance(lexer);
    if (c == quote) {
      return true;
    }
    if (quote != '`' && c == '\n') {
      return false;
    }
    if (quote != '`' && c == '\\' && !lexer->eof(lexer)) {
      advance(lexer);
    }
  }
  return false;
}

// The lexer stands on an opening bracket. Skips the text to the end of its closing bracket.
static bool skip_brackets(TokenReader *reader) {
  unsigned depth = 0;
  for (;;) {
    int32_t c = next_token_start(reader);
    if (reader->passed_operator_start != 0) {
      reader->passed_operator_start = 0;
      continue;
    }
    switch (c) {
      case 0:
        return false;
      case '"':
      case '`':
      case '\'':
        if (!skip_string_or_rune(reader->lexer)) {
          return false;
        }
        continue;
      case '(':
      case '[':
      case '{':
        depth++;
        break;
      case ')':
      case ']':
      case '}':
        depth--;
        break;
      default:
        break;
    }
    advance(reader->lexer);
    if (depth == 0) {
      return true;
    }
  }
}

// The lexer stands on `<`. Returns false for a text other than `<-`.
static bool skip_arrow(TSLexer *lexer) {
  advance(lexer);
  if (lexer->lookahead != '-') {
    return false;
  }
  advance(lexer);
  return true;
}

// Skips a type, which starts with `first` when the caller has read its first word. Returns false for a text that
// is no type.
static bool skip_type(TokenReader *reader, enum WordKind first) {
  TSLexer *lexer = reader->lexer;
  enum WordKind word = first;
  for (;;) {
    if (word == NO_WORD) {
      int32_t c = next_token_start(reader);
      if (c == '(') {
        return skip_brackets(reader);
      }
      if (c == '*') {
        advance(lexer);
        continue;
      }
      if (c == '[') {
        if (!skip_brackets(reader)) {
          return false;
        }
        continue;
      }
      if (c == '<') {
        if (!skip_arrow(lexer) || !is_word_character(next_token_start(reader)) || read_word(lexer) != CHAN_WORD) {
          return false;
        }
        word = CHAN_WORD;
      } else if (is_word_character(c)) {
        word = read_word(lexer);
      } else {
        return false;
      }
    }
    switch (word) {
      case MAP_WORD:
        if (next_token_start(reader) != '[' || !skip_brackets(reader)) {
          return false;
        }
        word = NO_WORD;
        continue;
      case CHAN_WORD:
        if (next_token_start(reader) == '<' && !skip_arrow(lexer)) {
          return false;
        }
        word = NO_WORD;
        continue;
      case FUNC_WORD: {
        if (next_token_start(reader) != '(' || !skip_brackets(reader)) {
          return false;
        }
        int32_t result = next_token_start(reader);
        if (result == '(') {
          return skip_brackets(reader);
        }
        if (result == '*' || result == '[' || is_word_character(result)) {
          word = NO_WORD;
          continue;
        }
        if (result != '<') {
          return true;
        }
        advance(lexer);
        if (lexer->lookahead != '-') {
          reader->passed_operator_start = '<';
          return true;
        }
        advance(lexer);
        if (!is_word_character(next_token_start(reader)) || read_word(lexer) != CHAN_WORD) {
          return false;
        }
        word = CHAN_WORD;
        continue;
      }
      case STRUCT_WORD:
      case INTERFACE_WORD:
        return next_token_start(reader) == '{' && skip_brackets(reader);
      case NAME_WORD:
        if (next_token_start(reader) == '.') {
          advance(lexer);
          if (!is_word_character(next_token_start(reader)) || read_word(lexer) != NAME_WORD) {
            return false;
          }
        }
        return next_token_start(reader) != '[' || skip_brackets(reader);
      default:
        return false;
    }
  }
}

enum BracketKind { UNDECIDED_BRACKET, TYPE_PARAMETER_BRACKET, ARRAY_LENGTH_BRACKET };

enum OperatorKind { NO_BINARY_OPERATOR, UNION_OPERATOR, MULTIPLICATIVE_OPERATOR, OTHER_BINARY_OPERATOR };

static enum OperatorKind read_binary_operator(TokenReader *reader) {
  TSLexer *lexer = reader->lexer;
  int32_t first = reader->passed_operator_start;
  reader->passed_operator_start = 0;
  if (first == 0) {
    first = lexer->lookahead;
    advance(lexer);
  }
  int32_t second = lexer->lookahead;
  switch (first) {
    case '*':
    case '/':
    case '%':
      return MULTIPLICATIVE_OPERATOR;
    case '+':
    case '-':
    case '^':
      return OTHER_BINARY_OPERATOR;
    case '&':
      if (second == '&') {
        advance(lexer);
        return OTHER_BINARY_OPERATOR;
      }
      if (second == '^') {
        advance(lexer);
      }
      return MULTIPLICATIVE_OPERATOR;
    case '|':
      if (second == '|') {
        advance(lexer);
        return OTHER_BINARY_OPERATOR;
      }
      return UNION_OPERATOR;
    case '<':
    case '>':
      if (first == '<' && second == '-') {
        return NO_BINARY_OPERATOR;
      }
      if (second == first) {
        advance(lexer);
        return MULTIPLICATIVE_OPERATOR;
      }
      if (second == '=') {
        advance(lexer);
      }
      return OTHER_BINARY_OPERATOR;
    case '=':
    case '!':
      if (second != '=') {
        return NO_BINARY_OPERATOR;
      }
      advance(lexer);
      return OTHER_BINARY_OPERATOR;
    default:
      return NO_BINARY_OPERATOR;
  }
}

// constraint: both parsers read the bracket after the name of a type declaration as an expression when it starts
// with a name, and take a type parameter list where the expression splits into a name and a constraint
// (`extractName` of go/parser): `P *x | y` and `P (x) | y`, with terms that bind tighter than `|`, split before a
// comma, or when a term holds a type element (`isTypeElem`), which is a type literal that is a whole operand, or a
// `~` term, under no operator but binary ones and parentheses; a `[` after the name starts a constraint in every text
// The lexer stands on the `[`. The scan settles the texts that end at the `]` with no comma outside parentheses.
// `depth` counts the open parentheses where a type element can stand; the scan skips every other bracket pair
// whole. `type_element_level` is `depth + 1` of the outermost such parentheses that hold a type element, and 0
// when none holds one.
static enum BracketKind classify_type_declaration_bracket(TSLexer *lexer) {
  TokenReader reader = {.lexer = lexer, .passed_operator_start = 0};
  advance(lexer);
  if (!is_word_character(next_token_start(&reader)) || read_word(lexer) != NAME_WORD) {
    return UNDECIDED_BRACKET;
  }
  int32_t c = next_token_start(&reader);
  if (c == '[') {
    return TYPE_PARAMETER_BRACKET;
  }
  if (c != '*' && c != '(') {
    return UNDECIDED_BRACKET;
  }
  advance(lexer);
  unsigned depth = c == '(' ? 1 : 0;
  unsigned type_element_level = 0;
  bool has_unary_operator = false;
  bool call_is_open = c == '(';
  bool expects_argument = call_is_open;
  unsigned argument_count = 0;
  bool call_has_just_closed = false;
  bool splits_into_name_and_constraint = true;
  bool passed_union = false;
  bool expects_operand = true;
  bool operand_can_be_type_element = false;
  for (;;) {
    c = next_token_start(&reader);
    if (c == 0) {
      return UNDECIDED_BRACKET;
    }
    bool continues_operand = !expects_operand && (c == '(' || c == '[' || c == '{' || c == '.');
    bool is_type_element = (operand_can_be_type_element && !continues_operand) ||
                           (expects_operand && c == '~' && !has_unary_operator);
    if (is_type_element && (type_element_level == 0 || type_element_level > depth + 1)) {
      type_element_level = depth + 1;
    }
    operand_can_be_type_element = false;
    if (call_has_just_closed && continues_operand) {
      splits_into_name_and_constraint = false;
    }
    call_has_just_closed = false;
    if (expects_operand && expects_argument && c != ')') {
      argument_count++;
      expects_argument = false;
    }
    if (is_word_character(c)) {
      enum WordKind word = read_word(lexer);
      if (expects_operand && starts_type_literal(word)) {
        if (!skip_type(&reader, word)) {
          return UNDECIDED_BRACKET;
        }
        operand_can_be_type_element = !has_unary_operator;
      }
      expects_operand = false;
      continue;
    }
    if (c == '.') {
      advance(lexer);
      if (lexer->lookahead == '.') {
        splits_into_name_and_constraint = false;
      }
      continue;
    }
    if (continues_operand) {
      if (!skip_brackets(&reader)) {
        return UNDECIDED_BRACKET;
      }
      continue;
    }
    if (c == ')') {
      bool closes_call = depth == 1 && call_is_open;
      if (depth == 0 || (expects_operand && !(closes_call && expects_argument))) {
        return UNDECIDED_BRACKET;
      }
      operand_can_be_type_element = type_element_level == depth + 1;
      if (operand_can_be_type_element) {
        type_element_level = 0;
      }
      depth--;
      advance(lexer);
      if (closes_call) {
        call_is_open = false;
        expects_argument = false;
        call_has_just_closed = true;
        splits_into_name_and_constraint = splits_into_name_and_constraint && argument_count == 1;
      }
      expects_operand = false;
      continue;
    }
    if (c == ',') {
      if (expects_operand || depth != 1 || !call_is_open) {
        return UNDECIDED_BRACKET;
      }
      advance(lexer);
      has_unary_operator = false;
      expects_operand = true;
      expects_argument = true;
      continue;
    }
    if (!expects_operand) {
      if (c == ']') {
        if (depth != 0) {
          return UNDECIDED_BRACKET;
        }
        bool has_constraint = splits_into_name_and_constraint && type_element_level == 1;
        return has_constraint ? TYPE_PARAMETER_BRACKET : ARRAY_LENGTH_BRACKET;
      }
      enum OperatorKind kind = read_binary_operator(&reader);
      if (kind == NO_BINARY_OPERATOR) {
        return UNDECIDED_BRACKET;
      }
      if (depth == 0 && kind == UNION_OPERATOR) {
        passed_union = true;
      } else if (depth == 0 && (kind == OTHER_BINARY_OPERATOR || !passed_union)) {
        splits_into_name_and_constraint = false;
      }
      has_unary_operator = false;
      expects_operand = true;
      continue;
    }
    switch (c) {
      case '"':
      case '`':
      case '\'':
        if (!skip_string_or_rune(lexer)) {
          return UNDECIDED_BRACKET;
        }
        expects_operand = false;
        continue;
      case '(':
        if (has_unary_operator) {
          if (!skip_brackets(&reader)) {
            return UNDECIDED_BRACKET;
          }
          expects_operand = false;
          continue;
        }
        depth++;
        advance(lexer);
        continue;
      case '[':
        if (!skip_type(&reader, NO_WORD)) {
          return UNDECIDED_BRACKET;
        }
        operand_can_be_type_element = !has_unary_operator;
        expects_operand = false;
        continue;
      case '<':
        if (!skip_arrow(lexer)) {
          return UNDECIDED_BRACKET;
        }
        if (is_word_character(next_token_start(&reader))) {
          enum WordKind word = read_word(lexer);
          if (starts_type_literal(word) && !skip_type(&reader, word)) {
            return UNDECIDED_BRACKET;
          }
          operand_can_be_type_element = word == CHAN_WORD && !has_unary_operator;
          expects_operand = false;
          continue;
        }
        has_unary_operator = true;
        continue;
      case '+':
      case '-':
      case '!':
      case '^':
      case '*':
      case '&':
      case '~':
        advance(lexer);
        has_unary_operator = true;
        continue;
      default:
        return UNDECIDED_BRACKET;
    }
  }
}

static bool is_closing_bracket(int32_t c) { return c == ')' || c == ']' || c == '}'; }

static bool is_comma_or_closing_bracket(int32_t c) { return c == ',' || is_closing_bracket(c); }

static bool colon_is_a_token(TSLexer *lexer) {
  advance(lexer);
  return lexer->lookahead != '=';
}

// constraint: a text is a Go file when its first token is `package`, also when a token that is no name follows it:
// the file form drops that token and finds the name. Any other text is a fragment, and so is a text in which a
// keyword or the end of the text follows `package`: the file form would take the name of the next declaration for
// the name of the package
// The lexer stands on the first character of the first token of the text.
static bool starts_package_clause(TSLexer *lexer) {
  static const char keyword[] = "package";
  for (unsigned at = 0; keyword[at] != 0; at++) {
    if (lexer->lookahead != keyword[at]) {
      return false;
    }
    advance(lexer);
  }
  if (is_word_character(lexer->lookahead)) {
    return false;
  }
  bool passed_division_operator;
  if (!skip_white_space_and_comments(lexer, &passed_division_operator) || passed_division_operator) {
    return true;
  }
  if (lexer->eof(lexer)) {
    return false;
  }
  if (!is_word_character(lexer->lookahead)) {
    return true;
  }
  enum WordKind word = read_word(lexer);
  return word != KEYWORD_WORD && !starts_type_literal(word);
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

// constraint: Go inserts a semicolon at a line end after the token before each marker; the scan returns the
// semicolon also where no rule takes it, which is an error, and the parser then recovers with a token that later
// states take: it can close a bracket with a MISSING node, or drop the open list and end the statement
static bool take_line_end(TSLexer *lexer, const bool *valid_symbols) {
  bool is_after_semicolon_token = valid_symbols[AUTOMATIC_SEMICOLON] || valid_symbols[SAME_LINE] ||
                                  valid_symbols[BRACE_ON_SAME_LINE] || valid_symbols[ELEMENT_END] ||
                                  valid_symbols[COLON_ON_SAME_LINE] || valid_symbols[LINE_CONTINUES];
  return is_after_semicolon_token && give(lexer, AUTOMATIC_SEMICOLON);
}

// constraint: a line end after an element of a list is an error, and the semicolon that the scan returns for it has
// no text; the parser does not go back to a state at the position where it stands, so the semicolon before a line
// that starts with a closing bracket takes the line break as its padding, and the bracket then closes the list
static bool ends_line_in_list(const bool *valid_symbols) {
  return valid_symbols[ELEMENT_END] && !valid_symbols[AUTOMATIC_SEMICOLON];
}

// constraint: the semicolon of a line end stands before a line comment, and a semicolon there keeps the line break
// after the comment out of its padding; so the scan leaves a comment before a line with a closing bracket to the
// lexer, and gives the semicolon after it. The lexer does no check of a comment: the scan leaves it only a comment
// that needs none, without a character that Go rejects, a line directive, or a carriage return at its end
// The lexer stands on the second `/` of the comment.
static bool is_plain_comment_before_closing_bracket(TSLexer *lexer) {
  advance(lexer);
  LineDirective directive = {0};
  bool ends_with_carriage_return = false;
  while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
    int32_t c = lexer->lookahead;
    if (!is_valid_in_source(c)) {
      return false;
    }
    read_directive_character(&directive, c);
    ends_with_carriage_return = c == '\r';
    advance(lexer);
  }
  bool passed_division_operator;
  return !ends_with_carriage_return && directive.matched_prefix_length != DIRECTIVE_PREFIX_LENGTH &&
         skip_white_space_and_comments(lexer, &passed_division_operator) && !passed_division_operator &&
         is_closing_bracket(lexer->lookahead);
}

// constraint: while the parser recovers from an error, it takes no token without text, and every external token is
// valid, so the scan cannot tell where a semicolon belongs; `_recovery_line_end` holds the newline, and the grammar
// takes it as a terminator and as an empty item of each list of statements, declarations or specs, so the parser
// continues at the nearest of those places
static bool scan_line_end_in_recovery(TSLexer *lexer) {
  while (!lexer->eof(lexer) && is_white_space(lexer->lookahead) && lexer->lookahead != '\n') {
    skip(lexer);
  }
  if (lexer->eof(lexer) || lexer->lookahead != '\n') {
    return false;
  }
  advance(lexer);
  lexer->mark_end(lexer);
  while (!lexer->eof(lexer) && is_white_space(lexer->lookahead)) {
    advance(lexer);
  }
  int32_t next = lexer->lookahead;
  bool next_line_continues = next == ')' || next == ']' || next == '}' || next == ',' || next == '.';
  return !next_line_continues && give(lexer, RECOVERY_LINE_END);
}

// constraint: a line comment and a general comment with a newline end the line like a newline (spec, Comments)
// The scan gives a line comment itself, for the end before a carriage return. It reads a general comment only to
// reject the text that Go rejects, and leaves the token to the lexer.
bool tree_sitter_golang_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  (void)payload;
  if (valid_symbols[ERROR_SENTINEL]) {
    return scan_line_end_in_recovery(lexer);
  }
  if (valid_symbols[INTERPRETED_STRING_CONTENT] || valid_symbols[RAW_STRING_CONTENT]) {
    return scan_string_content(lexer, valid_symbols[RAW_STRING_CONTENT]);
  }
  lexer->mark_end(lexer);
  // constraint: the lexer skips a byte order mark at the start of the text and counts no column for it, so a token at
  // column 0 that the scan reaches without white space, and not at the start of the text, follows the mark
  bool follows_token_or_mark = !lexer->is_at_included_range_start(lexer) && !is_white_space(lexer->lookahead);
  bool line_ended = false;
  while (!lexer->eof(lexer) && is_white_space(lexer->lookahead)) {
    line_ended = line_ended || lexer->lookahead == '\n';
    skip(lexer);
  }
  if (lexer->eof(lexer)) {
    return valid_symbols[FRAGMENT_START] ? give(lexer, FRAGMENT_START) : take_line_end(lexer, valid_symbols);
  }
  int32_t next = lexer->lookahead;
  if (next == '/') {
    advance(lexer);
    if (lexer->lookahead == '/') {
      if (ends_line_in_list(valid_symbols) && is_plain_comment_before_closing_bracket(lexer)) {
        return false;
      }
      if (take_line_end(lexer, valid_symbols)) {
        return true;
      }
      return give(lexer, scan_line_comment(lexer, follows_token_or_mark) ? COMMENT : REJECTED_TOKEN);
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
  // constraint: the lexer hides a byte order mark at the start of the text and shows one that an edit has moved from
  // there, and the parser can reuse a token before text that no edit has changed; so the scan gives no token before
  // a byte order mark, which Go rejects in that place
  bool stands_on_token = next != 0 && next != BYTE_ORDER_MARK;
  if (valid_symbols[FRAGMENT_START] && stands_on_token && !starts_package_clause(lexer)) {
    return give(lexer, FRAGMENT_START);
  }
  if (valid_symbols[STATEMENT_START] && next == '~') {
    return give(lexer, REJECTED_TOKEN);
  }
  if (line_ended) {
    if (ends_line_in_list(valid_symbols) && is_closing_bracket(next)) {
      lexer->mark_end(lexer);
    }
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
  if (valid_symbols[SAME_LINE] && !is_comma_or_closing_bracket(next) && next != ';') {
    return give(lexer, SAME_LINE);
  }
  if (valid_symbols[COLON_ON_SAME_LINE] && next == ':' && colon_is_a_token(lexer)) {
    return give(lexer, COLON_ON_SAME_LINE);
  }
  return false;
}
