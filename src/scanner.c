/*
 * External scanner for tree-sitter-cheby.
 *
 * Handles the tokens that a regular-expression lexer cannot:
 *
 * - NEWLINE: a significant line break (spec §2.8). The parser only marks
 *   NEWLINE as valid where a statement, item, case arm or interface
 *   function may end, so the "open brackets" and "trailing continuation
 *   token" rules fall out of the grammar. This scanner adds the "leading
 *   continuation token" rule: if the next non-blank, non-comment line
 *   starts with `|>`, `&&` or `||`, no NEWLINE is produced.
 * - STRING_CONTENT: literal text inside a "..." string, up to the next
 *   `"`, `\` or `{` (spec §2.5.3). Strings may span lines.
 * - RAW_STRING: r"...", r#"..."#, r##"..."## and so on (spec §2.5.3, D-130).
 */

#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdint.h>

enum TokenType {
  NEWLINE,
  STRING_CONTENT,
  RAW_STRING,
  ERROR_SENTINEL,
};

void *tree_sitter_cheby_external_scanner_create(void) { return NULL; }

void tree_sitter_cheby_external_scanner_destroy(void *payload) {
  (void)payload;
}

unsigned tree_sitter_cheby_external_scanner_serialize(void *payload,
                                                      char *buffer) {
  (void)payload;
  (void)buffer;
  return 0;
}

void tree_sitter_cheby_external_scanner_deserialize(void *payload,
                                                    const char *buffer,
                                                    unsigned length) {
  (void)payload;
  (void)buffer;
  (void)length;
}

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static inline bool is_space(int32_t c) {
  return c == ' ' || c == '\t' || c == '\r';
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

static bool scan_raw_string(TSLexer *lexer) {
  if (lexer->lookahead != 'r') {
    return false;
  }
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
      // Unterminated: highlight the rest of the file as the string, which
      // is what the user sees while typing.
      break;
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
 * Called with the lexer on a '\n'. Consumes the line break and any blank
 * lines after it, then looks past comment lines at the first token of the
 * next line without including it in the NEWLINE token.
 */
static bool scan_newline(TSLexer *lexer) {
  while (lexer->lookahead == '\n' || is_space(lexer->lookahead)) {
    advance(lexer);
  }
  lexer->mark_end(lexer);

  for (;;) {
    while (lexer->lookahead == '\n' || is_space(lexer->lookahead)) {
      advance(lexer);
    }
    if (lexer->lookahead != '/') {
      break;
    }
    advance(lexer);
    if (lexer->lookahead != '/') {
      // A leading `/` does not continue the line.
      lexer->result_symbol = NEWLINE;
      return true;
    }
    // A comment line: skip it and look at the line after.
    while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
      advance(lexer);
    }
  }

  switch (lexer->lookahead) {
  case '|':
    advance(lexer);
    if (lexer->lookahead == '>' || lexer->lookahead == '|') {
      return false; // `|>` or `||` continues the previous line
    }
    break;
  case '&':
    advance(lexer);
    if (lexer->lookahead == '&') {
      return false; // `&&` continues the previous line
    }
    break;
  default:
    break;
  }

  lexer->result_symbol = NEWLINE;
  return true;
}

bool tree_sitter_cheby_external_scanner_scan(void *payload, TSLexer *lexer,
                                             const bool *valid_symbols) {
  (void)payload;

  // During error recovery every symbol is valid. Let the internal lexer
  // handle that case.
  if (valid_symbols[ERROR_SENTINEL]) {
    return false;
  }

  if (valid_symbols[STRING_CONTENT]) {
    return scan_string_content(lexer);
  }

  if (valid_symbols[NEWLINE]) {
    while (is_space(lexer->lookahead)) {
      skip(lexer);
    }
    if (lexer->lookahead == '\n') {
      return scan_newline(lexer);
    }
  } else {
    while (lexer->lookahead == '\n' || is_space(lexer->lookahead)) {
      skip(lexer);
    }
  }

  if (valid_symbols[RAW_STRING]) {
    return scan_raw_string(lexer);
  }

  return false;
}
