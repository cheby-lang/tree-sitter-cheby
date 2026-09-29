; Later patterns take precedence over earlier ones, so general captures come
; first and specific ones after.

; Identifiers
; -----------

(identifier) @variable

(type_identifier) @type

((type_identifier) @type.builtin
  (#eq? @type.builtin "Self"))

(constructor) @constructor

((constructor) @boolean
  (#any-of? @boolean "True" "False"))

((constructor) @constant.builtin
  (#eq? @constant.builtin "Nil"))

(module) @module

(wildcard) @variable.special

(parameter
  pattern: (identifier) @variable.parameter)

(parameter
  pattern: (tuple_pattern
    (identifier) @variable.parameter))

(const_declaration
  name: (identifier) @constant)

; Fields
; ------

(field_declaration
  name: (identifier) @property)

(field_initializer
  name: (identifier) @property)

(field_pattern
  name: (identifier) @property)

(field_expression
  field: (identifier) @property)

(interpolation
  field: (identifier) @property)

; Functions
; ---------

(function_declaration
  name: (identifier) @function)

(interface_function
  name: (identifier) @function)

(call_expression
  function: (identifier) @function)

(call_expression
  function: (path_expression
    name: (identifier) @function))

(path_expression
  interface: (type_identifier) @type)

; Attributes
; ----------

(attribute
  "@" @attribute
  name: (identifier) @attribute)

(inner_attribute
  "@!" @attribute
  name: (identifier) @attribute)

; Keywords
; --------

[
  "as"
  "assert"
  "const"
  "dyn"
  "exposed"
  "interface"
  "let"
  "priv"
  "pub"
  "test"
  "type"
  "use"
] @keyword

"fn" @keyword.function

"import" @keyword.import

[
  "case"
  "when"
] @keyword.conditional

[
  "panic"
  "todo"
] @keyword.exception

; Literals
; --------

[
  (string)
  (raw_string)
] @string

(escape_sequence) @string.escape

(interpolation
  [
    "{"
    "}"
    ":?"
  ] @punctuation.special)

[
  (integer)
  (float)
] @number

; Comments
; --------

(comment) @comment

[
  (doc_comment)
  (module_doc)
] @comment.doc

; Operators and punctuation
; -------------------------

[
  "+"
  "-"
  "*"
  "/"
  "%"
  "!"
  "&"
  "|"
  "^"
  "<<"
  ">>"
  "=="
  "!="
  "<="
  ">="
  "&&"
  "||"
  "|>"
  "="
  "=>"
  "->"
  "<-"
  ".."
] @operator

(binary_expression
  operator: [
    "<"
    ">"
  ] @operator)

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

(type_arguments
  [
    "<"
    ">"
  ] @punctuation.bracket)

(type_parameters
  [
    "<"
    ">"
  ] @punctuation.bracket)

(turbofish
  [
    "::<"
    ">"
  ] @punctuation.bracket)

[
  ","
  "."
  ":"
  "::"
] @punctuation.delimiter
