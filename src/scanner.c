/*
 * External scanner for tree-sitter-cheby.
 *
 * Line breaks (spec §2.8, D-231, D-426) follow the newline policy of the
 * compiler parser (crates/cheby_syntax/src/parser/input.rs in the cheby
 * repository), so that both parsers reject the same files:
 *
 * - Inside `(`, `[` or a type `<`, a line break is whitespace. The scanner
 *   keeps a stack of the open delimiters for that, serialized with the
 *   tree, which is why it lexes every delimiter itself.
 * - Before `|>`, `&&`, `||`, `}` or the end of the file (looking past blank
 *   lines and comment lines), a line break is whitespace.
 * - After a token that continues the line (`,`, `=`, `=>`, `->`, `<-`, a
 *   binary operator or a `{`), a line break is whitespace too. The grammar
 *   says where that is: it allows LINE_CONTINUATION after exactly those
 *   tokens, and NEWLINE after a `{` that opens a block.
 * - Every other line break is a NEWLINE token. Where the grammar allows no
 *   NEWLINE, such as between `let x` and `= 1`, it is a syntax error, as in
 *   the compiler.
 *
 * The scanner also lexes:
 *
 * - STRING_CONTENT: literal text inside a "..." string, up to the next `"`,
 *   `\` or `{` (spec §2.5.3). Strings may span lines.
 * - RAW_STRING: r"...", r#"..."#, r##"..."## and so on (spec §2.5.3,
 *   D-130). An unterminated raw string is no token (E0010).
 * - `<-`, by longest match, wherever it appears (§2.6): `a<-1` is not
 *   `a < -1`.
 * - TYPE_LT and TYPE_GT, the `<` and `>` of type arguments and type
 *   parameters, which open and close a delimiter. A `>` that closes type
 *   arguments is one character even in `>>` or `>=` (D-430).
 * - INVALID, a token the grammar never accepts, for a line break or a
 *   comment inside the braces of an interpolation (§2.5.4).
 *
 * HOOK is an extra. Being valid everywhere, it makes tree-sitter call the
 * scanner before every token, so that no line break or delimiter escapes
 * it. The scanner returns it for a line break that is whitespace where line
 * breaks are significant, so that it still lexes a `}` after it.
 */

#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum TokenType {
  NEWLINE,
  STRING_CONTENT,
  RAW_STRING,
  ERROR_SENTINEL,
  LINE_CONTINUATION,
  HOOK,
  INVALID,
  TYPE_LT,
  TYPE_GT,
  L_PAREN,
  R_PAREN,
  L_BRACK,
  R_BRACK,
  L_BRACE,
  R_BRACE,
  LEFT_ARROW,
};

/* What a delimiter on the stack is. */
enum Delimiter {
  DELIM_PAREN = '(',
  DELIM_BRACK = '[',
  DELIM_TYPE = '<',
  DELIM_BRACE = '{',
  DELIM_INTERPOLATION = '"',
};

/* Deeper nesting than this is counted but not recorded, and treated like a
 * `{`. The limit keeps the serialized state within tree-sitter's buffer. */
#define MAX_DEPTH (TREE_SITTER_SERIALIZATION_BUFFER_SIZE - 1)

typedef struct {
  uint16_t depth;
  /* Delimiters opened beyond MAX_DEPTH and not closed yet. */
  uint8_t overflow;
  char stack[MAX_DEPTH];
} Scanner;

void *tree_sitter_cheby_external_scanner_create(void) {
  return ts_calloc(1, sizeof(Scanner));
}

void tree_sitter_cheby_external_scanner_destroy(void *payload) {
  ts_free(payload);
}

unsigned tree_sitter_cheby_external_scanner_serialize(void *payload,
                                                      char *buffer) {
  Scanner *scanner = payload;
  if (scanner->depth == 0 && scanner->overflow == 0) {
    return 0;
  }
  buffer[0] = (char)scanner->overflow;
  memcpy(&buffer[1], scanner->stack, scanner->depth);
  return 1 + (unsigned)scanner->depth;
}

void tree_sitter_cheby_external_scanner_deserialize(void *payload,
                                                    const char *buffer,
                                                    unsigned length) {
  Scanner *scanner = payload;
  scanner->depth = 0;
  scanner->overflow = 0;
  if (length == 0) {
    return;
  }
  scanner->overflow = (uint8_t)buffer[0];
  unsigned depth = length - 1;
  if (depth > MAX_DEPTH) {
    depth = MAX_DEPTH;
  }
  memcpy(scanner->stack, &buffer[1], depth);
  scanner->depth = (uint16_t)depth;
}

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

/* Whitespace within a line (§2.1): space, tab, and a carriage return, which
 * ends a CRLF line break or is a lone CR (E0002, reported by validation). */
static inline bool is_space(int32_t c) {
  return c == ' ' || c == '\t' || c == '\r';
}

static char top(const Scanner *scanner) {
  if (scanner->overflow > 0 || scanner->depth == 0) {
    return DELIM_BRACE;
  }
  return scanner->stack[scanner->depth - 1];
}

static void push(Scanner *scanner, char delimiter) {
  if (scanner->overflow > 0 || scanner->depth >= MAX_DEPTH) {
    if (scanner->overflow < UINT8_MAX) {
      scanner->overflow++;
    }
    return;
  }
  scanner->stack[scanner->depth++] = delimiter;
}

/* Closes the innermost open delimiter of kind `delimiter`, and with it any
 * delimiters opened inside it that were never closed. A closer that matches
 * nothing open changes nothing. A `}` also closes an interpolation. */
static void pop(Scanner *scanner, char delimiter) {
  if (scanner->overflow > 0) {
    scanner->overflow--;
    return;
  }
  for (unsigned i = scanner->depth; i > 0; i--) {
    char open = scanner->stack[i - 1];
    if (open == delimiter ||
        (delimiter == DELIM_BRACE && open == DELIM_INTERPOLATION)) {
      scanner->depth = (uint16_t)(i - 1);
      return;
    }
  }
}

/* Takes the current character as a token of kind `symbol`. */
static bool accept(TSLexer *lexer, enum TokenType symbol) {
  advance(lexer);
  lexer->mark_end(lexer);
  lexer->result_symbol = (TSSymbol)symbol;
  return true;
}

static bool scan_string_content(TSLexer *lexer) {
  bool has_content = false;
  while (!lexer->eof(lexer)) {
    int32_t c = lexer->lookahead;
    if (c == '"' || c == '\\' || c == '{') {
      break;
    }
    advance(lexer);
    has_content = true;
  }
  if (!has_content) {
    return false;
  }
  lexer->mark_end(lexer);
  lexer->result_symbol = STRING_CONTENT;
  return true;
}

/* Called with the lexer on an `r`. */
static bool scan_raw_string(TSLexer *lexer) {
  advance(lexer);

  unsigned hashes = 0;
  while (lexer->lookahead == '#') {
    advance(lexer);
    hashes++;
  }
  if (lexer->lookahead != '"') {
    // An identifier such as `rest`. The internal lexer takes over.
    return false;
  }
  advance(lexer);

  for (;;) {
    if (lexer->eof(lexer)) {
      // Unterminated (E0010): no token, so the file has a syntax error.
      return false;
    }
    if (lexer->lookahead == '"') {
      advance(lexer);
      unsigned closing = 0;
      while (closing < hashes && lexer->lookahead == '#') {
        advance(lexer);
        closing++;
      }
      if (closing == hashes) {
        break;
      }
    } else {
      advance(lexer);
    }
  }

  lexer->mark_end(lexer);
  lexer->result_symbol = RAW_STRING;
  return true;
}

/*
 * Called with the lexer on a '\n' where line breaks are significant.
 * Consumes the line break and any blank lines after it, then looks past
 * comment lines at the first token of the next line without including it in
 * the token.
 *
 * If a comment line comes next, the line break is returned as whitespace
 * (HOOK), and the line break after the last comment line decides. Comments
 * are extras, so the parse state is the same there, and the grammar never
 * needs two NEWLINE or LINE_CONTINUATION tokens in a row.
 */
static bool scan_line_break(TSLexer *lexer, const bool *valid_symbols) {
  while (lexer->lookahead == '\n' || is_space(lexer->lookahead)) {
    advance(lexer);
  }
  lexer->mark_end(lexer);

  bool comment_next = false;
  for (;;) {
    while (lexer->lookahead == '\n' || is_space(lexer->lookahead)) {
      advance(lexer);
    }
    if (lexer->lookahead != '/') {
      break;
    }
    advance(lexer);
    if (lexer->lookahead != '/') {
      break; // a leading `/` is an operator, not a comment
    }
    // A comment line: skip it and look at the line after.
    comment_next = true;
    while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
      advance(lexer);
    }
  }

  // Whether the line break is whitespace (§2.8 rule 3, D-426). It is then
  // returned as HOOK, an extra, rather than left to the internal lexer, so
  // that the scanner still lexes the `}` that may follow.
  bool whitespace = false;
  if (lexer->eof(lexer)) {
    whitespace = true; // line breaks before the end of the file
  } else {
    switch (lexer->lookahead) {
    case '}':
      whitespace = true;
      break;
    case '|':
      advance(lexer);
      // a leading `|>` or `||` continues the line
      whitespace = lexer->lookahead == '>' || lexer->lookahead == '|';
      break;
    case '&':
      advance(lexer);
      whitespace = lexer->lookahead == '&'; // a leading `&&`
      break;
    default:
      break;
    }
  }

  if (whitespace || comment_next) {
    lexer->result_symbol = HOOK;
  } else if (valid_symbols[LINE_CONTINUATION]) {
    lexer->result_symbol = LINE_CONTINUATION;
  } else {
    lexer->result_symbol = NEWLINE;
  }
  return true;
}

bool tree_sitter_cheby_external_scanner_scan(void *payload, TSLexer *lexer,
                                             const bool *valid_symbols) {
  Scanner *scanner = payload;

  // During error recovery every symbol is valid, so the valid symbols say
  // nothing about the context. Delimiters are still tracked.
  bool recovering = valid_symbols[ERROR_SENTINEL];

  if (!recovering && valid_symbols[STRING_CONTENT]) {
    if (lexer->lookahead == '{') {
      push(scanner, DELIM_INTERPOLATION);
      return accept(lexer, L_BRACE);
    }
    return scan_string_content(lexer);
  }

  char delimiter = top(scanner);

  for (;;) {
    while (is_space(lexer->lookahead)) {
      skip(lexer);
    }
    if (lexer->lookahead != '\n') {
      break;
    }
    if (delimiter == DELIM_INTERPOLATION && !recovering) {
      return accept(lexer, INVALID);
    }
    if (delimiter == DELIM_BRACE && !recovering) {
      return scan_line_break(lexer, valid_symbols);
    }
    // Inside `(`, `[` or a type `<`, a line break is whitespace.
    skip(lexer);
  }

  switch (lexer->lookahead) {
  case '(':
    push(scanner, DELIM_PAREN);
    return accept(lexer, L_PAREN);
  case ')':
    pop(scanner, DELIM_PAREN);
    return accept(lexer, R_PAREN);
  case '[':
    push(scanner, DELIM_BRACK);
    return accept(lexer, L_BRACK);
  case ']':
    pop(scanner, DELIM_BRACK);
    return accept(lexer, R_BRACK);
  case '{':
    push(scanner, DELIM_BRACE);
    return accept(lexer, L_BRACE);
  case '}':
    pop(scanner, DELIM_BRACE);
    return accept(lexer, R_BRACE);
  case '<':
    advance(lexer);
    if (lexer->lookahead == '-') {
      return accept(lexer, LEFT_ARROW);
    }
    if (!recovering && valid_symbols[TYPE_LT] && lexer->lookahead != '<' &&
        lexer->lookahead != '=') {
      push(scanner, DELIM_TYPE);
      lexer->mark_end(lexer);
      lexer->result_symbol = TYPE_LT;
      return true;
    }
    return false;
  case '>':
    if (!recovering && valid_symbols[TYPE_GT]) {
      pop(scanner, DELIM_TYPE);
      return accept(lexer, TYPE_GT);
    }
    return false;
  case '/':
    if (delimiter == DELIM_INTERPOLATION && !recovering) {
      advance(lexer);
      if (lexer->lookahead == '/') {
        return accept(lexer, INVALID);
      }
    }
    return false;
  case 'r':
    if (recovering || valid_symbols[RAW_STRING]) {
      return scan_raw_string(lexer);
    }
    return false;
  default:
    return false;
  }
}
