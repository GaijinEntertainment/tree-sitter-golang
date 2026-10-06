/**
 * @file Go grammar for tree-sitter
 * @author Gaijin Entertainment
 * @author Anton Zinovyev <xog3@yandex.ru>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const PREC = {
  RECEIVE_OPERATION: -3,
  TYPE_ELEMENT_IN_EXPRESSION: -2,
  TYPE_PARAMETERS_OVER_ARRAY_LENGTH: -1,
  RECEIVE_CHANNEL_TYPE: -1,
  TYPE_GUARD_OUTSIDE_SWITCH: -1,
  OR: 1,
  AND: 2,
  COMPARE: 3,
  ADD: 4,
  MULTIPLY: 5,
  UNARY: 6,
  PRIMARY: 7,
};

const BINARY_OPERATORS = [
  [PREC.MULTIPLY, choice('*', '/', '%', '<<', '>>', '&', '&^')],
  [PREC.ADD, choice('+', '-', '|', '^')],
  [PREC.COMPARE, choice('==', '!=', '<', '<=', '>', '>=')],
  [PREC.AND, '&&'],
  [PREC.OR, '||'],
];

const KEYWORDS = [
  'break', 'case', 'chan', 'const', 'continue', 'default', 'defer', 'else', 'fallthrough', 'for', 'func', 'go', 'goto',
  'if', 'import', 'interface', 'map', 'package', 'range', 'return', 'select', 'struct', 'switch', 'type', 'var',
];

const ASSIGNMENT_OPERATIONS = ['+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '<<=', '>>=', '&^='];

const WHOLE_TOKENS_BEFORE_OPERAND = ['++', '--', '&&', '&^'];
const WHOLE_TOKENS_AFTER_OPERAND = ['++', '--', '<-'];

const DECIMAL_DIGITS = /[0-9](_?[0-9])*/.source;
const HEX_DIGITS = /[0-9a-fA-F](_?[0-9a-fA-F])*/.source;
const DECIMAL_EXPONENT = `[eE][+-]?${DECIMAL_DIGITS}`;
const HEX_MANTISSA = `(_?${HEX_DIGITS}\\.(${HEX_DIGITS})?|_?${HEX_DIGITS}|\\.${HEX_DIGITS})`;

const INT_LITERAL = [
  /0|[1-9](_?[0-9])*/.source,
  /0[bB](_?[01])+/.source,
  /0[oO]?(_?[0-7])+/.source,
  /0[xX](_?[0-9a-fA-F])+/.source,
].join('|');

const FLOAT_LITERAL = [
  `${DECIMAL_DIGITS}\\.(${DECIMAL_DIGITS})?(${DECIMAL_EXPONENT})?`,
  `${DECIMAL_DIGITS}${DECIMAL_EXPONENT}`,
  `\\.${DECIMAL_DIGITS}(${DECIMAL_EXPONENT})?`,
  `0[xX]${HEX_MANTISSA}[pP][+-]?${DECIMAL_DIGITS}`,
].join('|');

const SCALAR_HEX4 = /[0-9a-cA-CeEfF][0-9a-fA-F]{3}|[dD][0-7][0-9a-fA-F]{2}/.source;
const ESCAPE_TAIL = [
  /[0-3][0-7]{2}/.source,
  /x[0-9a-fA-F]{2}/.source,
  `u(${SCALAR_HEX4})`,
  `U(0000(${SCALAR_HEX4})|000[1-9a-fA-F][0-9a-fA-F]{4}|0010[0-9a-fA-F]{4})`,
].join('|');

/**
 * @param {string} prefix
 * @param {string} base
 * @returns {string}
 */
function spineName(prefix, base) {
  return base.startsWith('_') ? `_${prefix}${base.slice(1)}` : `${prefix}${base}`;
}

// constraint: a rule copy with fields gets a visible name; the generator copies an aliased hidden rule's fields up
/**
 * @param {GrammarSymbols<string>} $
 * @param {string} prefix
 * @param {string} base
 * @returns {RuleOrLiteral}
 */
function spine($, prefix, base) {
  const rule = $[spineName(prefix, base)];
  if (prefix === '' || base.startsWith('_')) {
    return rule;
  }
  return alias(rule, $[base]);
}

// constraint: between `if`, `for` or `switch` and the block, Go reads the `{` after a named type as the block
/**
 * @param {string} prefix
 * @returns {Record<string, ($: GrammarSymbols<string>) => RuleOrLiteral>}
 */
function expressionSpine(prefix) {
  const literalTypes = ($) => {
    const types = [
      $.array_type,
      $.implicit_length_array_type,
      $.slice_type,
      $.map_type,
      $.struct_type,
    ];
    return prefix === '' ? [...types, $._type_name, $.generic_type] : types;
  };

  return {
    [spineName(prefix, '_expression')]: ($) => choice(
      spine($, prefix, 'unary_expression'),
      spine($, prefix, 'binary_expression'),
      spine($, prefix, '_primary_expression'),
    ),

    [spineName(prefix, 'unary_expression')]: ($) => choice(
      prec(PREC.UNARY, seq(
        field('operator', choice('+', '-', '!', '^', '*', '&')),
        field('operand', spine($, prefix, '_expression')),
      )),
      prec.dynamic(PREC.RECEIVE_OPERATION, prec(PREC.UNARY, seq(
        field('operator', '<-'),
        field('operand', spine($, prefix, '_expression')),
      ))),
      typeElementInExpression(prec(PREC.UNARY, seq(
        field('operator', '~'),
        field('operand', spine($, prefix, '_expression')),
      ))),
      prec(PREC.UNARY, seq(
        field('operator', wholeToken(WHOLE_TOKENS_BEFORE_OPERAND)),
        $._never_returned,
        field('operand', spine($, prefix, '_expression')),
      )),
    ),

    [spineName(prefix, 'binary_expression')]: ($) => choice(
      ...BINARY_OPERATORS.map(([precedence, operator]) =>
        prec.left(precedence, seq(
          field('left', spine($, prefix, '_expression')),
          field('operator', operator),
          field('right', spine($, prefix, '_expression')),
        )),
      ),
      prec.left(seq(
        field('left', spine($, prefix, '_expression')),
        field('operator', wholeToken(WHOLE_TOKENS_AFTER_OPERAND)),
        $._never_returned,
        field('right', spine($, prefix, '_expression')),
      )),
    ),

    [spineName(prefix, '_primary_expression')]: ($) => choice(
      spine($, prefix, '_operand_expression'),
      typeElementInExpression($.array_type),
      typeElementInExpression($.implicit_length_array_type),
      typeElementInExpression($.slice_type),
      typeElementInExpression($.map_type),
      typeElementInExpression($.channel_type),
      typeElementInExpression($.function_type),
    ),

    [spineName(prefix, '_operand_expression')]: ($) => choice(
      typeElementInExpression($.struct_type),
      typeElementInExpression($.interface_type),
      $.identifier,
      $.int_literal,
      $.float_literal,
      $.imaginary_literal,
      $.rune_literal,
      $.raw_string_literal,
      $.interpreted_string_literal,
      spine($, prefix, 'composite_literal'),
      $.function_literal,
      $.parenthesized_expression,
      spine($, prefix, 'selector_expression'),
      spine($, prefix, 'index_expression'),
      spine($, prefix, 'slice_expression'),
      spine($, prefix, 'type_assertion_expression'),
      spine($, prefix, 'call_expression'),
    ),

    [spineName(prefix, 'composite_literal')]: ($) => prec(PREC.PRIMARY, seq(
      field('type', choice(...literalTypes($))),
      $._brace_on_same_line,
      field('value', $.literal_value),
    )),

    // constraint: Go reads the radix point into a literal with a base prefix: `0x1.f` is a mantissa without its
    // exponent, and not a selector on `0x1`; the second form never completes, so that text gives ERROR
    [spineName(prefix, 'selector_expression')]: ($) => prec(PREC.PRIMARY, seq(
      choice(
        seq(field('operand', spine($, prefix, '_operand_expression')), '.'),
        seq($._prefixed_int_with_radix_point, $._never_returned, field('operand', $.int_literal)),
      ),
      field('field', alias($.identifier, $.field_identifier)),
    )),

    [spineName(prefix, 'index_expression')]: ($) => prec(PREC.PRIMARY, seq(
      field('operand', spine($, prefix, '_operand_expression')),
      '[',
      closedElementList($, field('index', $._expression)),
      ']',
    )),

    [spineName(prefix, 'slice_expression')]: ($) => prec(PREC.PRIMARY, seq(
      field('operand', spine($, prefix, '_operand_expression')),
      '[',
      choice(
        seq(
          optional(seq(field('low', $._expression), $._colon_on_same_line)),
          ':',
          optional(seq(field('high', $._expression), $._element_end)),
        ),
        seq(
          optional(seq(field('low', $._expression), $._colon_on_same_line)),
          ':',
          field('high', $._expression),
          $._colon_on_same_line,
          ':',
          field('max', $._expression),
          $._element_end,
        ),
      ),
      ']',
    )),

    [spineName(prefix, 'type_assertion_expression')]: ($) => choice(
      prec(PREC.PRIMARY, seq(
        field('operand', spine($, prefix, '_operand_expression')),
        '.',
        '(',
        field('type', $._type),
        $._element_end,
        ')',
      )),
      prec(PREC.TYPE_GUARD_OUTSIDE_SWITCH, seq(
        field('operand', spine($, prefix, '_operand_expression')),
        '.',
        '(',
        'type',
        ')',
      )),
    ),

    [spineName(prefix, 'call_expression')]: ($) => prec(PREC.PRIMARY, seq(
      field('function', spine($, prefix, '_primary_expression')),
      optional($._line_continues),
      field('arguments', $.arguments),
    )),

    [spineName(prefix, 'expression_list')]: ($) => commaSep1(spine($, prefix, '_expression')),

    [spineName(prefix, '_simple_statement')]: ($) => choice(
      spine($, prefix, 'expression_statement'),
      spine($, prefix, 'send_statement'),
      spine($, prefix, 'inc_dec_statement'),
      spine($, prefix, 'assignment'),
      spine($, prefix, 'short_var_declaration'),
    ),

    // constraint: both parsers take `~x` as an operand, and reject `~` as the first token of a statement in a block;
    // the scanner never returns `_statement_start`, and gives `_rejected_token` for a `~` where that marker is valid
    [spineName(prefix, 'expression_statement')]: ($) => prefix === '' ?
      seq(optional($._statement_start), $._expression) :
      spine($, prefix, '_expression'),

    [spineName(prefix, 'send_statement')]: ($) => seq(
      field('channel', spine($, prefix, '_expression')),
      '<-',
      field('value', spine($, prefix, '_expression')),
    ),

    [spineName(prefix, 'inc_dec_statement')]: ($) => seq(
      field('operand', spine($, prefix, '_expression')),
      field('operator', choice('++', '--')),
    ),

    // constraint: the compiler takes one operand on each side of an assignment operation such as `+=`
    [spineName(prefix, 'assignment')]: ($) => choice(
      seq(
        field('left', spine($, prefix, 'expression_list')),
        field('operator', '='),
        field('right', spine($, prefix, 'expression_list')),
      ),
      seq(
        field('left', alias($[spineName(prefix, '_single_expression')], $.expression_list)),
        field('operator', choice(...ASSIGNMENT_OPERATIONS)),
        field('right', alias($[spineName(prefix, '_single_expression')], $.expression_list)),
      ),
    ),

    [spineName(prefix, '_single_expression')]: ($) => spine($, prefix, '_expression'),

    [spineName(prefix, 'short_var_declaration')]: ($) => seq(
      field('left', spine($, prefix, 'expression_list')),
      ':=',
      field('right', spine($, prefix, 'expression_list')),
    ),
  };
}

export default grammar({
  name: 'golang',

  externals: ($) => [
    $._automatic_semicolon,
    $._same_line,
    $._brace_on_same_line,
    $._element_end,
    $._colon_on_same_line,
    $._line_continues,
    $._statement_start,
    $._never_returned,
    $._rejected_token,
    $._error_sentinel,
  ],

  extras: ($) => [/[ \t\r\n]/, $.comment],

  word: ($) => $.identifier,

  reserved: {
    global: (_) => KEYWORDS,
  },

  conflicts: ($) => [
    [$.parameter_declaration, $._type_name],
    [$._type_name, $.field_declaration],
    [$.qualified_identifier, $._operand_expression],
    [$._type_name, $._operand_expression],
    [$.type_parameter_declaration, $._operand_expression],
    [$.type_parameter_declaration, $._type_name, $._operand_expression],
    [$.type_switch_statement, $._header_operand_expression],
    [$._simple_type, $._primary_expression],
    [$._simple_type, $._operand_expression],
    [$.channel_type],
    [$.receiver_parameter_declaration, $._type_name],
    [$._range_left, $.header_expression_list],
  ],

  supertypes: ($) => [
    $._expression,
    $._type,
    $._statement,
    $._simple_statement,
  ],

  rules: {
    source_file: ($) => seq(
      $.package_clause,
      $._terminator,
      repeat(seq($.import_declaration, $._terminator)),
      repeat(seq($._top_level_declaration, $._terminator)),
    ),

    package_clause: ($) => seq('package', field('name', packageIdentifier($))),

    import_declaration: ($) => seq('import', choice($.import_spec, seq('(', terminated($, $.import_spec), ')'))),

    import_spec: ($) => seq(
      optional(choice(
        field('name', alias('.', $.dot)),
        seq(field('name', choice(alias('_', $.blank_identifier), packageIdentifier($))), $._same_line),
      )),
      field('path', $._string_literal),
    ),

    _top_level_declaration: ($) => choice($._declaration, $.function_declaration, $.method_declaration),

    _declaration: ($) => choice($.const_declaration, $.type_declaration, $.var_declaration),

    const_declaration: ($) => seq('const', choice($.const_spec, seq('(', terminated($, $.const_spec), ')'))),

    const_spec: ($) => seq(
      commaSep1(field('name', $.identifier)),
      optional(field('type', $._type)),
      optional(seq('=', field('value', $.expression_list))),
    ),

    var_declaration: ($) => seq('var', choice($.var_spec, seq('(', terminated($, $.var_spec), ')'))),

    var_spec: ($) => seq(
      elementList($, field('name', $.identifier)),
      $._same_line,
      choice(
        seq(field('type', $._type), optional(seq('=', field('value', $.expression_list)))),
        seq('=', field('value', $.expression_list)),
      ),
    ),

    type_declaration: ($) => seq('type', choice(
      $.type_definition,
      $.alias_declaration,
      seq('(', terminated($, choice($.type_definition, $.alias_declaration)), ')'),
    )),

    type_definition: ($) => seq(
      field('name', typeIdentifier($)),
      $._same_line,
      optional(typeParametersOnTheLine($)),
      field('type', $._type),
    ),

    alias_declaration: ($) => seq(
      field('name', typeIdentifier($)),
      $._same_line,
      optional(typeParametersOnTheLine($)),
      '=',
      field('type', $._type),
    ),

    function_declaration: ($) => seq(
      'func',
      field('name', $.identifier),
      $._same_line,
      optional(typeParametersOnTheLine($)),
      field('parameters', $.parameters),
      optional(field('result', $._result)),
      optional(seq($._brace_on_same_line, field('body', $.block))),
    ),

    method_declaration: ($) => seq(
      'func',
      field('receiver', alias($.receiver, $.parameters)),
      $._same_line,
      field('name', fieldIdentifier($)),
      $._same_line,
      optional(typeParametersOnTheLine($)),
      field('parameters', $.parameters),
      optional(field('result', $._result)),
      optional(seq($._brace_on_same_line, field('body', $.block))),
    ),

    receiver: ($) => seq(
      '(',
      choice(
        alias($.receiver_parameter_declaration, $.parameter_declaration),
        alias($.unnamed_parameter_declaration, $.parameter_declaration),
      ),
      $._element_end,
      optional(','),
      ')',
    ),

    receiver_parameter_declaration: ($) => seq(field('name', $.identifier), field('type', $._type)),

    type_parameters: ($) => prec.dynamic(PREC.TYPE_PARAMETERS_OVER_ARRAY_LENGTH, seq(
      '[',
      closedElementList($, $.type_parameter_declaration),
      ']',
    )),

    type_parameter_declaration: ($) => seq(
      elementList($, field('name', typeIdentifier($))),
      field('constraint', $.type_elem),
    ),

    parameters: ($) => seq('(', optional(choice($._named_parameters, $._unnamed_parameters)), ')'),

    _named_parameters: ($) => seq(
      choice(
        seq(
          elementList($, $.parameter_declaration),
          optional(seq($._element_end, ',', alias($.variadic_parameter_declaration, $.parameter_declaration))),
        ),
        alias($.variadic_parameter_declaration, $.parameter_declaration),
      ),
      $._element_end,
      optional(','),
    ),

    _unnamed_parameters: ($) => seq(
      choice(
        seq(
          elementList($, alias($.unnamed_parameter_declaration, $.parameter_declaration)),
          optional(seq(
            $._element_end,
            ',',
            alias($.unnamed_variadic_parameter_declaration, $.parameter_declaration),
          )),
        ),
        alias($.unnamed_variadic_parameter_declaration, $.parameter_declaration),
      ),
      $._element_end,
      optional(','),
    ),

    parameter_declaration: ($) => seq(elementList($, field('name', $.identifier)), field('type', $._type)),

    unnamed_parameter_declaration: ($) => field('type', $._type),

    variadic_parameter_declaration: ($) => seq(field('name', $.identifier), '...', field('type', $._type)),

    unnamed_variadic_parameter_declaration: ($) => seq('...', field('type', $._type)),

    _result: ($) => choice($.parameters, $._simple_type),

    _type: ($) => choice($._simple_type, $.parenthesized_type),

    _simple_type: ($) => choice(
      $._type_name,
      $.generic_type,
      $.pointer_type,
      $.array_type,
      $.implicit_length_array_type,
      $.slice_type,
      $.map_type,
      $.channel_type,
      $.function_type,
      $.struct_type,
      $.interface_type,
    ),

    _type_name: ($) => choice(typeIdentifier($), $.qualified_identifier),

    parenthesized_type: ($) => seq('(', $._type, $._element_end, ')'),

    qualified_identifier: ($) => seq(
      field('package', packageIdentifier($)),
      '.',
      field('name', typeIdentifier($)),
    ),

    generic_type: ($) => seq(field('type', $._type_name), field('type_arguments', $.type_arguments)),

    type_arguments: ($) => seq('[', closedElementList($, $._type), ']'),

    pointer_type: ($) => prec(PREC.UNARY, seq('*', $._type)),

    array_type: ($) => prec.right(seq(
      '[',
      field('length', $._expression),
      $._element_end,
      ']',
      $._same_line,
      field('element', $._type),
    )),

    implicit_length_array_type: ($) => seq('[', '...', ']', $._same_line, field('element', $._type)),

    slice_type: ($) => prec.right(seq('[', ']', $._same_line, field('element', $._type))),

    map_type: ($) => prec.right(seq(
      'map',
      '[',
      field('key', $._type),
      $._element_end,
      ']',
      $._same_line,
      field('element', $._type),
    )),

    channel_type: ($) => choice(
      seq('chan', field('element', $._type)),
      seq('chan', '<-', field('element', $._type)),
      prec.dynamic(PREC.RECEIVE_CHANNEL_TYPE, seq('<-', 'chan', field('element', $._type))),
    ),

    function_type: ($) => prec.right(seq(
      'func',
      field('parameters', $.parameters),
      optional(field('result', $._result)),
    )),

    struct_type: ($) => seq('struct', '{', terminated($, $.field_declaration), '}'),

    field_declaration: ($) => seq(
      choice(
        seq(commaSep1(field('name', fieldIdentifier($))), optional($._line_continues), field('type', $._type)),
        field('type', $._embedded_type),
      ),
      optional(field('tag', $._string_literal)),
    ),

    _embedded_type: ($) => choice(
      $._type_name,
      $.generic_type,
      alias($._embedded_pointer_type, $.pointer_type),
    ),

    _embedded_pointer_type: ($) => seq('*', choice($._type_name, $.generic_type)),

    interface_type: ($) => seq('interface', '{', terminated($, choice($.method_elem, $.type_elem)), '}'),

    method_elem: ($) => seq(
      field('name', fieldIdentifier($)),
      field('parameters', $.parameters),
      optional(field('result', $._result)),
    ),

    type_elem: ($) => seq($._type_term, repeat(seq('|', $._type_term))),

    _type_term: ($) => choice($._type, $.underlying_type),

    underlying_type: ($) => seq('~', $._type),

    block: ($) => seq('{', optional($._block_statement_list), '}'),

    _block_statement_list: ($) => statementList($, [$._statement, alias($.empty_labeled_statement, $.labeled_statement)]),

    _unterminated_statement_list: ($) => seq(
      repeat(statementItem($)),
      choice($._statement, alias($.empty_labeled_statement, $.labeled_statement)),
    ),

    _statement: ($) => choice(
      $._declaration,
      $._simple_statement,
      $.labeled_statement,
      $.go_statement,
      $.defer_statement,
      $.return_statement,
      $.break_statement,
      $.continue_statement,
      $.goto_statement,
      $.fallthrough_statement,
      $.block,
      $.if_statement,
      $.expression_switch_statement,
      $.type_switch_statement,
      $.select_statement,
      $.for_statement,
    ),

    empty_statement: (_) => ';',

    labeled_statement: ($) => seq(field('label', labelName($)), ':', field('statement', $._statement)),

    empty_labeled_statement: ($) => seq(
      field('label', labelName($)),
      ':',
      optional(field('statement', alias($.empty_labeled_statement, $.labeled_statement))),
    ),

    semicolon_labeled_statement: ($) => seq(
      field('label', labelName($)),
      ':',
      field('statement', choice($.empty_statement, alias($.semicolon_labeled_statement, $.labeled_statement))),
    ),

    go_statement: ($) => seq('go', field('call', $.call_expression)),

    defer_statement: ($) => seq('defer', field('call', $.call_expression)),

    return_statement: ($) => seq('return', optional(field('result', $.expression_list))),

    break_statement: ($) => seq('break', optional(field('label', labelName($)))),

    continue_statement: ($) => seq('continue', optional(field('label', labelName($)))),

    goto_statement: ($) => seq('goto', field('label', labelName($))),

    fallthrough_statement: (_) => 'fallthrough',

    if_statement: ($) => seq(
      'if',
      optional($._header_initializer),
      field('condition', $._header_expression),
      $._brace_on_same_line,
      field('body', $.block),
      optional(seq('else', field('else', choice($.block, $.if_statement)))),
    ),

    expression_switch_statement: ($) => seq(
      'switch',
      optional($._header_initializer),
      optional(seq(field('value', $._header_expression), $._brace_on_same_line)),
      caseBody($, $.expression_case_clause, $.final_expression_case_clause),
    ),

    expression_case_clause: ($) => seq(
      'case',
      $._case_values,
      $._colon_on_same_line,
      ':',
      repeat(statementItem($)),
    ),

    final_expression_case_clause: ($) => seq(
      'case',
      $._case_values,
      $._colon_on_same_line,
      ':',
      $._unterminated_statement_list,
    ),

    _case_values: ($) => field('value', alias($._case_value_list, $.expression_list)),

    _case_value_list: ($) => elementList($, $._expression),

    default_clause: ($) => seq('default', ':', repeat(statementItem($))),

    final_default_clause: ($) => seq('default', ':', $._unterminated_statement_list),

    type_switch_statement: ($) => seq(
      'switch',
      optional($._header_initializer),
      optional(seq(field('name', $.identifier), ':=')),
      field('operand', $._header_operand_expression),
      '.',
      '(',
      'type',
      ')',
      $._brace_on_same_line,
      caseBody($, $.type_case_clause, $.final_type_case_clause),
    ),

    type_case_clause: ($) => seq(
      'case',
      $._type_case_list,
      $._colon_on_same_line,
      ':',
      repeat(statementItem($)),
    ),

    final_type_case_clause: ($) => seq(
      'case',
      $._type_case_list,
      $._colon_on_same_line,
      ':',
      $._unterminated_statement_list,
    ),

    _type_case_list: ($) => elementList($, field('type', choice(prec.dynamic(1, $._type), $._expression))),

    select_statement: ($) => seq('select', caseBody($, $.communication_clause, $.final_communication_clause)),

    communication_clause: ($) => seq(
      'case',
      $._communication,
      $._colon_on_same_line,
      ':',
      repeat(statementItem($)),
    ),

    final_communication_clause: ($) => seq(
      'case',
      $._communication,
      $._colon_on_same_line,
      ':',
      $._unterminated_statement_list,
    ),

    _communication: ($) => field('communication', choice($.send_statement, $.receive_statement)),

    receive_statement: ($) => seq(
      optional(seq(field('left', alias($._one_or_two_expressions, $.expression_list)), choice('=', ':='))),
      field('right', $._expression),
    ),

    _one_or_two_expressions: ($) => seq($._expression, optional(seq(',', $._expression))),

    for_statement: ($) => seq(
      'for',
      optional(choice(
        seq(field('condition', $._header_expression), $._brace_on_same_line),
        $.for_clause,
        seq($.range_clause, $._brace_on_same_line),
      )),
      field('body', $.block),
    ),

    _header_initializer: ($) => choice(seq(field('init_statement', $._header_simple_statement), $._terminator), ';'),

    for_clause: ($) => seq(
      $._header_initializer,
      choice(seq(field('condition', $._header_expression), $._terminator), ';'),
      optional(seq(field('post_statement', $._header_post_statement), $._brace_on_same_line)),
    ),

    _header_post_statement: ($) => choice(
      alias($.header_expression_statement, $.expression_statement),
      alias($.header_send_statement, $.send_statement),
      alias($.header_inc_dec_statement, $.inc_dec_statement),
      alias($.header_assignment, $.assignment),
    ),

    range_clause: ($) => seq(
      optional(seq(field('left', alias($._range_left, $.expression_list)), choice('=', ':='))),
      'range',
      field('right', $._header_expression),
    ),

    _range_left: ($) => seq($._header_expression, optional(seq(',', $._header_expression))),

    ...expressionSpine(''),
    ...expressionSpine('header_'),

    parenthesized_expression: ($) => seq('(', $._expression, $._element_end, ')'),

    function_literal: ($) => seq(
      'func',
      field('parameters', $.parameters),
      optional(field('result', $._result)),
      $._brace_on_same_line,
      field('body', $.block),
    ),

    arguments: ($) => seq(
      '(',
      optional(seq(
        elementList($, $._expression),
        choice(seq($._element_end, optional(',')), seq('...', optional(','))),
      )),
      ')',
    ),

    literal_value: ($) => seq('{', optional(closedElementList($, choice($._element, $.keyed_element))), '}'),

    _element: ($) => choice($._expression, $.literal_value),

    keyed_element: ($) => seq(field('key', $._element), $._colon_on_same_line, ':', field('element', $._element)),

    _string_literal: ($) => choice($.raw_string_literal, $.interpreted_string_literal),

    raw_string_literal: ($) => seq(
      '`',
      optional(alias($._raw_string_content, $.string_content)),
      token.immediate('`'),
    ),

    _raw_string_content: (_) => token.immediate(prec(1, /[^`]+/)),

    interpreted_string_literal: ($) => seq(
      '"',
      repeat(choice(alias($._interpreted_string_content, $.string_content), $.escape_sequence)),
      token.immediate('"'),
    ),

    _interpreted_string_content: (_) => token.immediate(prec(1, /[^"\\\n]+/)),

    escape_sequence: (_) => token.immediate(new RegExp(`\\\\([abfnrtv\\\\"]|${ESCAPE_TAIL})`)),

    rune_literal: (_) => token(new RegExp(`'([^'\\\\\\n]|\\\\([abfnrtv\\\\']|${ESCAPE_TAIL}))'`)),

    int_literal: (_) => token(new RegExp(INT_LITERAL)),

    _prefixed_int_with_radix_point: (_) => token(/0[bBoOxX][0-9a-fA-F_]*\./),

    float_literal: (_) => token(new RegExp(FLOAT_LITERAL)),

    // constraint: both parsers check the digits against the base for an integer literal only, so `0b9i` is valid
    imaginary_literal: (_) => token(new RegExp(
      `(${DECIMAL_DIGITS}|${INT_LITERAL}|0[bBoO](_?[0-9])+|${FLOAT_LITERAL})i`,
    )),

    identifier: (_) => /[_\p{L}][_\p{L}\p{Nd}]*/,

    _terminator: ($) => choice(';', $._automatic_semicolon),

    comment: (_) => token(choice(
      seq('//', /[^\n]*/),
      seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/'),
    )),
  },
});

/**
 * @param {GrammarSymbols<string>} $
 * @returns {AliasRule}
 */
function typeIdentifier($) {
  return alias($.identifier, $.type_identifier);
}

/**
 * @param {GrammarSymbols<string>} $
 * @returns {AliasRule}
 */
function fieldIdentifier($) {
  return alias($.identifier, $.field_identifier);
}

/**
 * @param {GrammarSymbols<string>} $
 * @returns {AliasRule}
 */
function packageIdentifier($) {
  return alias($.identifier, $.package_identifier);
}

/**
 * @param {GrammarSymbols<string>} $
 * @returns {AliasRule}
 */
function labelName($) {
  return alias($.identifier, $.label);
}

// constraint: Go reads the longest operator token, and tree-sitter reads only the tokens of the current state; a form
// that ends at `_never_returned` makes the long token valid where no rule takes it, so `a--b` is not `a - -b`
/**
 * @param {string[]} tokens
 * @returns {ChoiceRule}
 */
function wholeToken(tokens) {
  return choice(...tokens.map((token) => alias(token, '+')));
}

// constraint: the scanner never returns `_line_continues`; where it is valid, a line end needs an automatic semicolon
/**
 * @param {GrammarSymbols<string>} $
 * @returns {SeqRule}
 */
function typeParametersOnTheLine($) {
  return seq(field('type_parameters', $.type_parameters), optional($._line_continues));
}

// constraint: go/parser reads `type T[P X]` as type parameters when X holds a type literal or `~` term (isTypeElem)
/**
 * @param {RuleOrLiteral} rule
 * @returns {PrecRule}
 */
function typeElementInExpression(rule) {
  return prec.dynamic(PREC.TYPE_ELEMENT_IN_EXPRESSION, rule);
}

// constraint: Go omits the semicolon only before a closing brace, so a statement without a terminator and a label
// without a statement may end only the last clause
/**
 * @param {GrammarSymbols<string>} $
 * @param {RuleOrLiteral} clause
 * @param {RuleOrLiteral} finalClause
 * @returns {SeqRule}
 */
function caseBody($, clause, finalClause) {
  return seq(
    '{',
    repeat(choice(clause, alias($.default_clause, clause))),
    optional(choice(alias(finalClause, clause), alias($.final_default_clause, clause))),
    '}',
  );
}

/**
 * @param {GrammarSymbols<string>} $
 * @param {RuleOrLiteral[]} finalForms
 * @returns {ChoiceRule}
 */
function statementList($, finalForms) {
  return choice(
    seq(repeat1(statementItem($)), optional(choice(...finalForms))),
    ...finalForms,
  );
}

// constraint: Go inserts no semicolon after `:`, so only an explicit `;` ends a label without a statement
/**
 * @param {GrammarSymbols<string>} $
 * @returns {ChoiceRule}
 */
function statementItem($) {
  return choice(
    seq($._statement, $._terminator),
    alias($.semicolon_labeled_statement, $.labeled_statement),
    $.empty_statement,
  );
}

// constraint: Go inserts a semicolon at a line end after the last token of an element, so `,` stays on its line
/**
 * @param {GrammarSymbols<string>} $
 * @param {RuleOrLiteral} rule
 * @returns {SeqRule}
 */
function elementList($, rule) {
  return seq(rule, repeat(seq($._element_end, ',', rule)));
}

/**
 * @param {GrammarSymbols<string>} $
 * @param {RuleOrLiteral} rule
 * @returns {SeqRule}
 */
function closedElementList($, rule) {
  return seq(elementList($, rule), $._element_end, optional(','));
}

/**
 * @param {RuleOrLiteral} rule
 * @returns {SeqRule}
 */
function commaSep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}

// constraint: Go lets a list omit the semicolon before a closing `)` or `}`
/**
 * @param {GrammarSymbols<string>} $
 * @param {RuleOrLiteral} rule
 * @returns {SeqRule}
 */
function terminated($, rule) {
  return seq(repeat(seq(rule, $._terminator)), optional(rule));
}
