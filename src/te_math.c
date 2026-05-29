/* ============================================================================
 * te_math.c — Math.* dispatch (Nivel B paso 2.a).
 *
 * Receiver: identifier "Math" (pseudo-static class). Methods take 1 or 2
 * numeric args evaluated via evaluate_expression(). Result is wrapped as
 * INT when integral, FLOAT otherwise, and stored in __ret__.
 *
 * Constants `PI` and `E` are exposed as zero-arg "methods" (call sites
 * already use `Math.PI()` / `Math.E()` syntax in the language).
 * ============================================================================ */

#include "te_math.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

int te_math_method_dispatch(ASTNode *node, ASTNode *objNode) {
    if (!objNode || !objNode->id || strcmp(objNode->id, "Math") != 0) return 0;

    ASTNode *arg = node->right;
    double a = arg ? evaluate_expression(arg) : 0;
    double b = (arg && arg->next) ? evaluate_expression(arg->next) : 0; /* gotcha #1: 2nd arg via ->next */
    double res = 0;
    const char *m = node->id;
    if      (strcmp(m, "sqrt") == 0)  res = sqrt(a);
    else if (strcmp(m, "abs") == 0)   res = fabs(a);
    else if (strcmp(m, "floor") == 0) res = floor(a);
    else if (strcmp(m, "ceil") == 0)  res = ceil(a);
    else if (strcmp(m, "round") == 0) res = floor(a + 0.5);
    else if (strcmp(m, "pow") == 0)   res = pow(a, b);
    else if (strcmp(m, "min") == 0)   res = (a < b) ? a : b;
    else if (strcmp(m, "max") == 0)   res = (a > b) ? a : b;
    else if (strcmp(m, "log") == 0)   res = log(a);
    else if (strcmp(m, "log2") == 0)  res = log2(a);
    else if (strcmp(m, "log10") == 0) res = log10(a);
    else if (strcmp(m, "exp") == 0)   res = exp(a);
    else if (strcmp(m, "sin") == 0)   res = sin(a);
    else if (strcmp(m, "cos") == 0)   res = cos(a);
    else if (strcmp(m, "tan") == 0)   res = tan(a);
    else if (strcmp(m, "asin") == 0)  res = asin(a);
    else if (strcmp(m, "acos") == 0)  res = acos(a);
    else if (strcmp(m, "atan") == 0)  res = atan(a);
    else if (strcmp(m, "atan2") == 0) res = atan2(a, b);
    else if (strcmp(m, "sinh") == 0)  res = sinh(a);
    else if (strcmp(m, "cosh") == 0)  res = cosh(a);
    else if (strcmp(m, "tanh") == 0)  res = tanh(a);
    else if (strcmp(m, "sign") == 0)  res = (a > 0) - (a < 0);
    else if (strcmp(m, "trunc") == 0) res = (double)(long long)a;
    else if (strcmp(m, "mod") == 0)   res = fmod(a, b);
    else if (strcmp(m, "PI") == 0)    res = 3.14159265358979323846;
    else if (strcmp(m, "E") == 0)     res = 2.71828182845904523536;
    else { printf("Error: Math.%s not supported.\n", m); return 1; }

    ASTNode *r;
    if (res == (int)res) {
        r = create_ast_leaf_number("INT", (int)res, NULL, NULL);
    } else {
        char buf[64];
        snprintf(buf, 64, "%f", res);
        r = create_ast_leaf("FLOAT", 0, buf, NULL);
    }
    add_or_update_variable("__ret__", r);
    return 1;
}
