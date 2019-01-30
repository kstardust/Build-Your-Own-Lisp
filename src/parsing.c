#include <stdio.h>
#include <string.h>
#include <math.h>
#include <editline/readline.h>
#include "mpc.h"

long eval(mpc_ast_t*);
long eval_op(long, char*, long);
long mpowl(long, long);
long eval_unary(long, char*);

long
mpowl(long x, long y)
{
    if (y == 0) {
        return 1;
    }

    if (y % 2) {
        return x * mpowl(x, y-1);
    } else {
        long z = mpowl(x, y / 2);
        return z * z;
    }
}

long
eval(mpc_ast_t *t) {
    if (strstr(t->tag, "number")) {
        return atoi(t->contents);
    }

    char *op = t->children[1]->contents;
    
    long x = eval(t->children[2]);
    
    int i = 3;
    while (strstr(t->children[i]->tag, "expr")) {
        x = eval_op(x, op, eval(t->children[i]));
        i++;
    }

    if (i == 3) {
        x = eval_unary(x, op);
    }
        
    return x;
}

long
eval_unary(long x, char *op)
{
    if (strcmp(op, "-") == 0) {
        return -x;
    }
    return 0;
}

long
eval_op(long x, char *op, long y)
{
    if (strcmp(op, "+") == 0) {
        return x + y;
    }
    if (strcmp(op, "-") == 0) {
        return x - y;
    }
    if (strcmp(op, "*") == 0) {
        return x * y;
    }
    if (strcmp(op, "/") == 0) {
        return x / y;
    }
    if (strcmp(op, "^") == 0) {
        return mpowl(x, y);
    }
    if (strcmp(op, "%") == 0) {
        return x % y;
    }
    if (strcmp(op, "min") == 0) {
        return x > y ? y : x;
    }    
    if (strcmp(op, "max") == 0) {
        return x < y ? y : x;
    }    
    return 0;
}

void
print_ast_tags(mpc_ast_t* t)
{
    for (int i = 0; i < t->children_num; i++) {
        printf("%s\n", t->children[i]->tag);
    }
}

int
main(int argc, char *const *const argv)
{
    mpc_parser_t *Number   = mpc_new("number");
    mpc_parser_t *Operator = mpc_new("operator");
    mpc_parser_t *Expr     = mpc_new("expr");
    mpc_parser_t *Lispy    = mpc_new("lispy");

    mpca_lang(MPCA_LANG_DEFAULT,
              // "number   :   /-?[0-9]+\\.?[0-9]?/ ;"
              "number   :   /-?[0-9]+/ ;"
              // "operator : '+' | '-' | '*' | '/' | '%' | \"mod\" | \"sub\" | \"add\" | \"mul\" | \"div\" ;"
              "operator : '+' | '-' | '*' | '/' | '^' | '%' | \"min\" | \"max\"; "
              "expr     : <number> | '(' <operator> <expr>+ ')' ;"
              "lispy    : /^/ <operator> <expr>+ /$/ ;",
              Number, Operator, Expr, Lispy);

    while (1) {
        char *input = readline("lispy> ");
        
        if (!input) {
            break;
        }
        
        add_history(input);

        mpc_result_t r;
        if (mpc_parse("<stdin>", input, Lispy, &r)) {
            printf("%ld\n", eval(r.output));
            mpc_ast_delete(r.output);
        } else {
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }
            
        free(input);
    }
    
    mpc_cleanup(4, Number, Operator, Expr, Lispy);
    return 0;
}
