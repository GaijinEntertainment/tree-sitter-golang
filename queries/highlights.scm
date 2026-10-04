(identifier) @variable

(field_identifier) @variable.member

(package_identifier) @module

(label) @label

(type_identifier) @type

((type_identifier) @type.builtin
  (#any-of? @type.builtin
    "any" "bool" "byte" "comparable" "complex64" "complex128" "error" "float32" "float64" "int" "int8" "int16"
    "int32" "int64" "rune" "string" "uint" "uint8" "uint16" "uint32" "uint64" "uintptr"))

(type_definition
  name: (type_identifier) @type.definition)

(alias_declaration
  name: (type_identifier) @type.definition)

(const_spec
  name: (identifier) @constant)

((identifier) @constant.builtin
  (#any-of? @constant.builtin "nil" "iota"))

((type_identifier) @constant.builtin
  (#eq? @constant.builtin "nil"))

((identifier) @boolean
  (#any-of? @boolean "true" "false"))

(parameter_declaration
  name: (identifier) @variable.parameter)

(blank_identifier) @variable.builtin

((identifier) @variable.builtin
  (#eq? @variable.builtin "_"))

(dot) @punctuation.special

(function_declaration
  name: (identifier) @function)

(method_declaration
  name: (field_identifier) @function.method)

(method_elem
  name: (field_identifier) @function.method)

(call_expression
  function: (identifier) @function.call)

(call_expression
  function: (selector_expression
    field: (field_identifier) @function.method.call))

((call_expression
  function: (identifier) @function.builtin)
  (#any-of? @function.builtin
    "append" "cap" "clear" "close" "complex" "copy" "delete" "imag" "len" "make" "max" "min" "new" "panic" "print"
    "println" "real" "recover"))

[
  "package"
  "import"
] @keyword.import

"func" @keyword.function

"return" @keyword.return

"go" @keyword.coroutine

[
  "if"
  "else"
  "switch"
  "case"
  "default"
  "select"
] @keyword.conditional

[
  "for"
  "range"
] @keyword.repeat

[
  "type"
  "struct"
  "interface"
  "map"
  "chan"
] @keyword.type

[
  "const"
  "var"
  "defer"
  "break"
  "continue"
  "goto"
  (fallthrough_statement)
] @keyword

[
  "+"
  "-"
  "*"
  "/"
  "%"
  "&"
  "|"
  "^"
  "<<"
  ">>"
  "&^"
  "+="
  "-="
  "*="
  "/="
  "%="
  "&="
  "|="
  "^="
  "<<="
  ">>="
  "&^="
  "&&"
  "||"
  "<-"
  "++"
  "--"
  "=="
  "<"
  ">"
  "="
  "!"
  "~"
  "!="
  "<="
  ">="
  ":="
  "..."
] @operator

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

[
  ","
  ";"
  ":"
  "."
] @punctuation.delimiter

[
  (interpreted_string_literal)
  (raw_string_literal)
] @string

(escape_sequence) @string.escape

(rune_literal) @character

[
  (int_literal)
  (imaginary_literal)
] @number

(float_literal) @number.float

(comment) @comment

((comment) @keyword.directive
  (#match? @keyword.directive "^//go:"))
