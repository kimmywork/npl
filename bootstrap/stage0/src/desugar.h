#ifndef NPL_DESUGAR_H
#define NPL_DESUGAR_H

#include "ast.h"

/*
 * desugar.c — AST-to-AST expansion of core forms that are not
 * special forms in the code generator.
 *
 * Runs after macro expansion, before QBE generation.
 *
 * Currently handles:
 *   (cond (test expr...) ... (else expr...))
 *     -> nested (if test (begin expr...) ...)
 *
 * Errors (stderr + exit 1):
 *   - else clause not last / more than one else
 *   - malformed clause (not a pair)
 */

/* Expand all cond forms in a program (top-level list of expressions).
 * Returns the expanded program. Terminates on error.
 */
AstNode *desugar_program(AstNode *program);

#endif /* NPL_DESUGAR_H */
