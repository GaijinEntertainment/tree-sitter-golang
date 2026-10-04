(package_clause
  name: (package_identifier) @name) @definition.module

(
  (comment)* @doc
  .
  (function_declaration
    name: (identifier) @name) @definition.function
  (#strip! @doc "^//\\s?|^/\\*\\s*|\\s*\\*/$")
  (#select-adjacent! @doc @definition.function)
)

(
  (comment)* @doc
  .
  (method_declaration
    name: (field_identifier) @name) @definition.method
  (#strip! @doc "^//\\s?|^/\\*\\s*|\\s*\\*/$")
  (#select-adjacent! @doc @definition.method)
)

(
  (comment)* @doc
  .
  (type_declaration
    .
    [
      (type_definition
        name: (type_identifier) @name
        type: (interface_type))
      (alias_declaration
        name: (type_identifier) @name
        type: (interface_type))
    ] @definition.interface
    .)
  (#strip! @doc "^//\\s?|^/\\*\\s*|\\s*\\*/$")
  (#select-adjacent! @doc @definition.interface)
)

(
  (comment)* @doc
  .
  (type_declaration
    .
    [
      (type_definition
        name: (type_identifier) @name)
      (alias_declaration
        name: (type_identifier) @name)
    ] @definition.class
    .)
  (#strip! @doc "^//\\s?|^/\\*\\s*|\\s*\\*/$")
  (#select-adjacent! @doc @definition.class)
)

(type_declaration
  (comment)* @doc
  .
  [
    (type_definition
      name: (type_identifier) @name
      type: (interface_type))
    (alias_declaration
      name: (type_identifier) @name
      type: (interface_type))
  ] @definition.interface
  (#strip! @doc "^//\\s?|^/\\*\\s*|\\s*\\*/$")
  (#select-adjacent! @doc @definition.interface))

(type_declaration
  (comment)* @doc
  .
  [
    (type_definition
      name: (type_identifier) @name)
    (alias_declaration
      name: (type_identifier) @name)
  ] @definition.class
  (#strip! @doc "^//\\s?|^/\\*\\s*|\\s*\\*/$")
  (#select-adjacent! @doc @definition.class))

(method_elem
  name: (field_identifier) @name) @definition.method

((const_spec
  name: (identifier) @name) @definition.constant
  (#not-eq? @name "_"))

((call_expression
  function: [
    (identifier) @name
    (index_expression
      operand: (identifier) @name)
  ]) @reference.call
  (#not-any-of? @name
    "append" "cap" "clear" "close" "complex" "copy" "delete" "imag" "len" "make" "max" "min" "new" "panic" "print"
    "println" "real" "recover"))

(call_expression
  function: [
    (selector_expression
      field: (field_identifier) @name)
    (index_expression
      operand: (selector_expression
        field: (field_identifier) @name))
  ]) @reference.call

(composite_literal
  type: [
    (type_identifier) @name
    (qualified_identifier
      name: (type_identifier) @name)
    (generic_type
      type: [
        (type_identifier) @name
        (qualified_identifier
          name: (type_identifier) @name)
      ])
  ]) @reference.class
