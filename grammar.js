/**
 * @file Tree-sitter grammar for the Cheby programming language
 * @author xikxp1 <xikxp1@gmail.com>
 *
 * Follows docs/spec/appendix-a-grammar.md of the Cheby repository. The
 * grammar is deliberately more permissive than the spec (for example, it
 * accepts camelCase identifiers and trailing commas everywhere), because it
 * is meant for editors, not for rejecting invalid programs.
 *
 * Newlines (spec §2.8) are handled by the external scanner in src/scanner.c.
 * A newline becomes a `_newline` token only where the grammar allows one
 * (between items, statements, case arms and interface functions). In every
 * other position, such as inside `( )`, `[ ]`, `< >` or after a binary
 * operator, it is ordinary whitespace. That matches the spec's "open
 * brackets" and "trailing continuation token" rules. The scanner implements
 * the "leading continuation token" rule (`|>`, `&&`, `||`) itself.
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const PREC = {
  as_pattern: 0,
  or_pattern: 1,
  or: 1,
  and: 2,
  compare: 3,
  pipe: 4,
  bit_or: 5,
  bit_xor: 6,
  bit_and: 7,
  shift: 8,
  additive: 9,
  multiplicative: 10,
  unary: 11,
  postfix: 12,
};

/**
 * One or more `rule`s separated by commas, with an optional trailing comma.
 * @param {RuleOrLiteral} rule
 */
const commaSep1 = (rule) => seq(rule, repeat(seq(",", rule)), optional(","));

/**
 * Zero or more `rule`s separated by commas, with an optional trailing comma.
 * @param {RuleOrLiteral} rule
 */
const commaSep = (rule) => optional(commaSep1(rule));

/**
 * One or more `rule`s separated by significant newlines.
 * @param {GrammarSymbols<string>} $
 * @param {RuleOrLiteral} rule
 */
const lines1 = ($, rule) =>
  seq(rule, repeat(seq($._newline, rule)), optional($._newline));

/**
 * A `{ … }` body whose entries are separated by significant newlines.
 * @param {GrammarSymbols<string>} $
 * @param {RuleOrLiteral} rule
 */
const newlineBody = ($, rule) =>
  seq("{", optional($._newline), optional(lines1($, rule)), "}");

module.exports = grammar({
  name: "cheby",

  word: ($) => $.identifier,

  externals: ($) => [
    $._newline,
    $.string_content,
    $.raw_string,
    // Never used in the grammar. Tree-sitter marks every symbol as valid
    // during error recovery, so the scanner checks this one to detect that.
    $._error_sentinel,
  ],

  extras: ($) => [/\s/, $.comment, $.doc_comment, $.module_doc],

  supertypes: ($) => [$._expression, $._pattern, $._type],

  conflicts: ($) => [
    // `case Foo {`: a subject that is a constructor name followed by the
    // case body, not a record expression (spec §5.9.6).
    [$._expression, $.record_expression],
    [$.path_expression, $.record_expression],
  ],

  rules: {
    source_file: ($) => seq(optional($._newline), optional(lines1($, $._item))),

    // ---------------------------------------------------------------------
    // Items (A.1, A.2)
    // ---------------------------------------------------------------------

    _item: ($) =>
      choice(
        $.import_declaration,
        $.inner_attribute,
        $.attribute,
        $.function_declaration,
        $.type_declaration,
        $.type_alias,
        $.const_declaration,
        $.interface_declaration,
        $.test_declaration,
      ),

    import_declaration: ($) =>
      seq(
        "import",
        field("path", $._module),
        repeat(seq("::", field("path", $._module))),
        optional(seq("::", field("items", $.import_list))),
        optional(seq("as", field("alias", $._module))),
      ),

    import_list: ($) => seq("{", commaSep1($.import_item), "}"),

    import_item: ($) =>
      seq(
        field("name", choice($.identifier, $.type_identifier)),
        optional(
          seq("as", field("alias", choice($.identifier, $.type_identifier))),
        ),
      ),

    attribute: ($) =>
      seq(
        "@",
        field("name", $.identifier),
        optional(field("arguments", $.attribute_arguments)),
      ),

    inner_attribute: ($) =>
      seq(
        "@!",
        field("name", $.identifier),
        optional(field("arguments", $.attribute_arguments)),
      ),

    attribute_arguments: ($) =>
      seq("(", commaSep(choice($.identifier, $.string)), ")"),

    visibility_modifier: (_) => choice("pub", "priv"),

    function_declaration: ($) =>
      seq(
        optional($.visibility_modifier),
        "fn",
        field("name", $.identifier),
        optional(field("type_parameters", $.type_parameters)),
        field("parameters", $.parameters),
        optional(seq("->", field("return_type", $._type))),
        optional(field("body", $.block)),
      ),

    parameters: ($) => seq("(", commaSep($.parameter), ")"),

    parameter: ($) =>
      seq(
        field("name", $.identifier),
        optional(seq(":", field("type", $._type))),
      ),

    type_parameters: ($) => seq("<", commaSep1($.type_parameter), ">"),

    type_parameter: ($) =>
      seq(
        field("name", $.type_identifier),
        optional(seq(":", field("bounds", $.bounds))),
      ),

    bounds: ($) => seq($.named_type, repeat(seq("+", $.named_type))),

    type_declaration: ($) =>
      seq(
        optional($.visibility_modifier),
        optional("exposed"),
        "type",
        field("name", $.type_identifier),
        optional(field("type_parameters", $.type_parameters)),
        optional(field("body", choice($.variant_list, $.field_list))),
      ),

    variant_list: ($) => seq("{", commaSep1($.variant), "}"),

    variant: ($) =>
      seq(
        field("name", alias($.type_identifier, $.constructor)),
        optional(
          field("fields", choice($.positional_field_list, $.field_list)),
        ),
      ),

    positional_field_list: ($) => seq("(", commaSep($._type), ")"),

    field_list: ($) => seq("{", commaSep($.field_declaration), "}"),

    field_declaration: ($) =>
      seq(field("name", $.identifier), ":", field("type", $._type)),

    type_alias: ($) =>
      seq(
        optional($.visibility_modifier),
        "type",
        field("name", $.type_identifier),
        optional(field("type_parameters", $.type_parameters)),
        "=",
        field("type", $._type),
      ),

    const_declaration: ($) =>
      seq(
        optional($.visibility_modifier),
        "const",
        field("name", $.identifier),
        optional(seq(":", field("type", $._type))),
        "=",
        field("value", $._expression),
      ),

    interface_declaration: ($) =>
      seq(
        optional($.visibility_modifier),
        "interface",
        field("name", $.type_identifier),
        optional(seq(":", field("bounds", $.bounds))),
        optional(field("body", $.interface_body)),
      ),

    interface_body: ($) => newlineBody($, $.interface_function),

    interface_function: ($) =>
      seq(
        "fn",
        field("name", $.identifier),
        field("parameters", $.parameter_types),
        optional(seq("->", field("return_type", $._type))),
      ),

    parameter_types: ($) => seq("(", commaSep($._type), ")"),

    test_declaration: ($) =>
      seq("test", field("name", $.string), field("body", $.block)),

    // ---------------------------------------------------------------------
    // Types (A.3)
    // ---------------------------------------------------------------------

    _type: ($) =>
      choice($.named_type, $.tuple_type, $.function_type, $.dyn_type),

    named_type: ($) =>
      seq(
        optional(seq(field("module", $._module), "::")),
        field("name", $.type_identifier),
        optional(field("arguments", $.type_arguments)),
      ),

    type_arguments: ($) => seq("<", commaSep1($._type), ">"),

    tuple_type: ($) => seq("(", commaSep($._type), ")"),

    function_type: ($) =>
      prec.right(
        seq(
          "fn",
          field("parameters", $.parameter_types),
          optional(seq("->", field("return_type", $._type))),
        ),
      ),

    dyn_type: ($) => seq("dyn", field("interface", $.named_type)),

    // ---------------------------------------------------------------------
    // Blocks and statements (A.4)
    // ---------------------------------------------------------------------

    block: ($) => newlineBody($, $._statement),

    _statement: ($) =>
      choice(
        $.let_statement,
        $.use_statement,
        $.assert_statement,
        $.function_declaration,
        $._expression,
      ),

    let_statement: ($) =>
      seq(
        "let",
        optional("assert"),
        field("pattern", $._pattern),
        optional(seq(":", field("type", $._type))),
        "=",
        field("value", $._expression),
        optional(seq("as", field("message", $._expression))),
      ),

    use_statement: ($) =>
      seq("use", commaSep($.use_binding), "<-", field("value", $._expression)),

    use_binding: ($) =>
      seq(
        field("pattern", $._pattern),
        optional(seq(":", field("type", $._type))),
      ),

    assert_statement: ($) =>
      seq(
        "assert",
        field("condition", $._expression),
        optional(seq("as", field("message", $._expression))),
      ),

    // ---------------------------------------------------------------------
    // Expressions (A.5)
    // ---------------------------------------------------------------------

    _expression: ($) =>
      choice(
        $.binary_expression,
        $.unary_expression,
        $.call_expression,
        $.field_expression,
        $.identifier,
        alias($.type_identifier, $.constructor),
        $.path_expression,
        $.record_expression,
        $.integer,
        $.float,
        $.string,
        $.raw_string,
        $.tuple_expression,
        $.parenthesized_expression,
        $.list_expression,
        $.block,
        $.closure,
        $.case_expression,
        $.panic_expression,
        $.todo_expression,
      ),

    binary_expression: ($) => {
      /** @type {[number, RuleOrLiteral][]} */
      const table = [
        [PREC.or, "||"],
        [PREC.and, "&&"],
        [PREC.compare, choice("==", "!=", "<", "<=", ">", ">=")],
        [PREC.pipe, "|>"],
        [PREC.bit_or, "|"],
        [PREC.bit_xor, "^"],
        [PREC.bit_and, "&"],
        [PREC.shift, choice("<<", ">>")],
        [PREC.additive, choice("+", "-")],
        [PREC.multiplicative, choice("*", "/", "%")],
      ];
      return choice(
        ...table.map(([precedence, operator]) =>
          prec.left(
            precedence,
            seq(
              field("left", $._expression),
              field("operator", operator),
              field("right", $._expression),
            ),
          ),
        ),
      );
    },

    unary_expression: ($) =>
      prec(
        PREC.unary,
        seq(
          field("operator", choice("-", "!")),
          field("operand", $._expression),
        ),
      ),

    call_expression: ($) =>
      prec(
        PREC.postfix,
        seq(field("function", $._expression), field("arguments", $.arguments)),
      ),

    arguments: ($) =>
      seq("(", commaSep(choice($._expression, $.wildcard)), ")"),

    field_expression: ($) =>
      prec(
        PREC.postfix,
        seq(
          field("value", $._expression),
          ".",
          field("field", choice($.identifier, $.integer)),
        ),
      ),

    // `io::println`, `board::Won`, `Show::show`, `shape::Shape::area`,
    // `channel::new::<Int>`.
    path_expression: ($) =>
      choice(
        seq(
          field("module", $._module),
          "::",
          field(
            "name",
            choice($.identifier, alias($.type_identifier, $.constructor)),
          ),
          optional($._turbofish),
        ),
        seq(
          optional(seq(field("module", $._module), "::")),
          field("interface", $.type_identifier),
          "::",
          field("name", $.identifier),
          optional($._turbofish),
        ),
        seq(
          field(
            "name",
            choice($.identifier, alias($.type_identifier, $.constructor)),
          ),
          $._turbofish,
        ),
      ),

    // `::<` is one token so that `name::<T>` and `module::name` can be told
    // apart with one token of lookahead.
    _turbofish: ($) => field("type_arguments", $.turbofish),

    turbofish: ($) => seq("::<", commaSep1($._type), ">"),

    record_expression: ($) =>
      seq(
        optional(seq(field("module", $._module), "::")),
        field("name", alias($.type_identifier, $.constructor)),
        "{",
        commaSep(choice($.field_initializer, $.spread_element)),
        "}",
      ),

    field_initializer: ($) =>
      seq(
        field("name", $.identifier),
        optional(seq(":", field("value", $._expression))),
      ),

    spread_element: ($) => seq("..", $._expression),

    tuple_expression: ($) =>
      seq("(", $._expression, ",", commaSep($._expression), ")"),

    parenthesized_expression: ($) => seq("(", $._expression, ")"),

    list_expression: ($) =>
      seq("[", commaSep(choice($._expression, $.spread_element)), "]"),

    closure: ($) =>
      seq(
        "fn",
        field("parameters", $.parameters),
        optional(seq("->", field("return_type", $._type))),
        field("body", $.block),
      ),

    case_expression: ($) =>
      choice(
        seq(
          "case",
          field("subject", $._expression),
          repeat(seq(",", field("subject", $._expression))),
          field("body", $.case_body),
        ),
        seq("case", field("body", $.condition_body)),
      ),

    case_body: ($) => newlineBody($, $.case_arm),

    case_arm: ($) =>
      seq(
        field("pattern", $._pattern),
        repeat(seq(",", field("pattern", $._pattern))),
        optional(seq("when", field("guard", $._expression))),
        "=>",
        field("value", $._expression),
      ),

    // A case subject cannot start with `{` (spec §5.9.6), so `case {` always
    // starts a subjectless case.
    condition_body: ($) => prec(1, newlineBody($, $.condition_arm)),

    condition_arm: ($) =>
      seq(
        field("condition", choice($._expression, $.wildcard)),
        "=>",
        field("value", $._expression),
      ),

    panic_expression: ($) =>
      prec.right(
        seq("panic", optional(seq("as", field("message", $._expression)))),
      ),

    todo_expression: ($) =>
      prec.right(
        seq("todo", optional(seq("as", field("message", $._expression)))),
      ),

    // ---------------------------------------------------------------------
    // Patterns (A.6)
    // ---------------------------------------------------------------------

    _pattern: ($) =>
      choice(
        $.as_pattern,
        $.or_pattern,
        $.wildcard,
        $.identifier,
        $.integer,
        $.float,
        $.negative_literal,
        $.string,
        $.raw_string,
        $.constructor_pattern,
        $.tuple_pattern,
        $.list_pattern,
      ),

    as_pattern: ($) =>
      prec.left(
        PREC.as_pattern,
        seq(field("pattern", $._pattern), "as", field("name", $.identifier)),
      ),

    or_pattern: ($) =>
      prec.left(PREC.or_pattern, seq($._pattern, "|", $._pattern)),

    negative_literal: ($) => seq("-", choice($.integer, $.float)),

    constructor_pattern: ($) =>
      seq(
        optional(seq(field("module", $._module), "::")),
        field("name", alias($.type_identifier, $.constructor)),
        optional(
          choice(
            seq("(", commaSep($._pattern), ")"),
            seq("{", commaSep(choice($.field_pattern, "..")), "}"),
          ),
        ),
      ),

    field_pattern: ($) =>
      seq(
        field("name", $.identifier),
        optional(seq(":", field("pattern", $._pattern))),
      ),

    tuple_pattern: ($) => seq("(", commaSep($._pattern), ")"),

    list_pattern: ($) =>
      seq("[", commaSep(choice($._pattern, $.rest_pattern)), "]"),

    rest_pattern: ($) => seq("..", optional(field("name", $.identifier))),

    wildcard: (_) => "_",

    // ---------------------------------------------------------------------
    // Literals (A.7, A.8)
    // ---------------------------------------------------------------------

    string: ($) =>
      seq(
        '"',
        repeat(choice($.string_content, $.escape_sequence, $.interpolation)),
        '"',
      ),

    escape_sequence: (_) =>
      token(seq("\\", choice(/u\{[0-9a-fA-F]*\}/, /[\s\S]/))),

    interpolation: ($) =>
      seq(
        "{",
        field("name", $.identifier),
        repeat(seq(".", field("field", choice($.identifier, $.integer)))),
        optional(":?"),
        "}",
      ),

    integer: (_) =>
      token(choice(/0x[0-9a-fA-F_]+/, /0b[01_]+/, /0o[0-7_]+/, /[0-9][0-9_]*/)),

    float: (_) =>
      token(
        choice(
          /[0-9][0-9_]*\.[0-9][0-9_]*([eE][+-]?[0-9][0-9_]*)?/,
          /[0-9][0-9_]*[eE][+-]?[0-9][0-9_]*/,
        ),
      ),

    // ---------------------------------------------------------------------
    // Identifiers and comments (A.9, §2.2)
    // ---------------------------------------------------------------------

    // The spec forbids upper-case letters in LOWER and `_` in UPPER. The
    // grammar accepts both so that misspelled names still highlight.
    identifier: (_) => /[a-z_][a-zA-Z0-9_]*/,

    type_identifier: (_) => /[A-Z][a-zA-Z0-9_]*/,

    _module: ($) => alias($.identifier, $.module),

    comment: (_) => token(prec(1, seq("//", /[^\n]*/))),

    doc_comment: (_) => token(prec(2, seq("///", /[^\n]*/))),

    module_doc: (_) => token(prec(2, seq("//!", /[^\n]*/))),
  },
});
