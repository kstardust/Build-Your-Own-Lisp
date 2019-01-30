#include <stdio.h>
#include <string.h>
#include <math.h>
#include <editline/readline.h>
#include <errno.h>
#include "mpc.h"
 
typedef enum {
    LVAL_INT, LVAL_ERR, LVAL_DOUBLE
} lval_type_e;

typedef enum {
    LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM
} lerr_type_e;

typedef struct {
    lval_type_e type;
    union {
        long num_l;
        double num_d;
        lerr_type_e err;
    };
} lval;

#define lval_get_num(x) (x.type == LVAL_INT ? x.num_l : x.num_d)

#define lval_binary_opration(t, x, y, op)                       \
    t == LVAL_INT                                               \
        ? lval_int((lval_get_num(x) op lval_get_num(y)))        \
        : lval_double((lval_get_num(x) op lval_get_num(y)))

#define lval_assign(v, y)                       \
    (v).type == LVAL_INT                        \
        ? ((v).num_l = (y))                     \
        : ((v).type == LVAL_DOUBLE              \
           ? ((v).num_d = (y))                  \
           : ((v).err = (y)))

lval eval(mpc_ast_t*);
lval eval_op(lval, char*, lval);
long mpowl(long, long);
lval eval_unary(lval, char*);
lval lval_int(long);
lval lval_double(double);
lval lval_err(lerr_type_e);
void lval_print(lval);
void lval_println(lval);
lval lval_toint(lval);

void
lval_println(lval v)
{
    lval_print(v);
    printf("\n");
}

void
lval_print(lval v)
{
    switch (v.type) {
    case LVAL_INT:
        printf("%ld", v.num_l);
        break;
    case LVAL_DOUBLE:
        printf("%lf", v.num_d);
        break;
    case LVAL_ERR:
        if (v.err == LERR_DIV_ZERO) {
            printf("Error: Division By Zero!");
        }
        if (v.err == LERR_BAD_OP)   {
            printf("Error: Invalid Operator!");
        }
        if (v.err == LERR_BAD_NUM)  {
            printf("Error: Invalid Number!");
        }
        break;
    default:
        break;
    }
}

lval
lval_toint(lval v)
{
    lval v1;
    switch (v.type) {
    case LVAL_INT:
    case LVAL_ERR:        
        return v;
    case LVAL_DOUBLE:
        v1.type = LVAL_INT;
        v1.num_l = (long)v.num_d;
        return v1;
    }
    v1.type = LVAL_ERR;
    v1.err = LERR_BAD_OP;
    return v1;
}

lval
lval_int(long x)
{
    lval v;
    v.num_l = x;
    v.type = LVAL_INT;
    return v;
}

lval
lval_double(double x)
{
    lval v;
    v.num_d = x;
    v.type = LVAL_DOUBLE;
    return v;
}

lval
lval_err(lerr_type_e e)
{
    lval v;
    v.err = e;
    v.type = LVAL_ERR;
    return v;
}

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

lval
eval(mpc_ast_t *t) {
    
    if (strstr(t->tag, "integer")) {
        errno = 0;
        long x = strtol(t->contents, NULL, 10);
        return errno ? lval_err(LERR_BAD_NUM): lval_int(x);
    }
    
    if (strstr(t->tag, "double")) {
        errno = 0;
        double x = strtod(t->contents, NULL);
        return errno ? lval_err(LERR_BAD_NUM): lval_double(x);
    }

    char *op = t->children[1]->contents;
    
    lval x = eval(t->children[2]);

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

lval
eval_unary(lval x, char *op)
{
    if (strcmp(op, "-") == 0) {
        if (x.type == LVAL_INT) {
            return lval_int(-x.num_l);
        }
        if (x.type == LVAL_DOUBLE) {
            return lval_double(-x.num_d);
        }
    }
    return lval_err(LERR_BAD_OP);
}

lval
eval_op(lval x, char *op, lval y)
{
    if (x.type == LVAL_ERR) {
        return x;
    }
    if (y.type == LVAL_ERR) {
        return y;
    }

    lval_type_e t = x.type > y.type ? x.type : y.type;
    if (strcmp(op, "+") == 0) {
        return lval_binary_opration(t, x, y, +);
    }
    if (strcmp(op, "-") == 0) {
        return lval_binary_opration(t, x, y, -);
    }
    if (strcmp(op, "*") == 0) {
        return lval_binary_opration(t, x, y, *);
    }
    if (strcmp(op, "/") == 0) {
        return lval_get_num(y) == 0
            ? lval_err(LERR_DIV_ZERO)
            : lval_binary_opration(t, x, y, /);
    }
    if (strcmp(op, "^") == 0) {
        return lval_double(pow(lval_get_num(x), lval_get_num(y)));
    }
    if (strcmp(op, "%") == 0) {
        long xv = (long)lval_get_num(x);
        long yv = (long)lval_get_num(y);
        lval v;
        v.type = LVAL_INT;
        lval_assign(v, xv % yv);
        return v;
    }
    if (strcmp(op, "min") == 0) {
        return lval_get_num(x) > lval_get_num(y) ? y : x;
    }
    if (strcmp(op, "max") == 0) {
        return lval_get_num(x) < lval_get_num(y) ? y : x;
    }
    return lval_err(LERR_BAD_OP);
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
    mpc_parser_t *Double   = mpc_new("double");
    mpc_parser_t *Integer  = mpc_new("integer");
    mpc_parser_t *Number   = mpc_new("number");
    mpc_parser_t *Operator = mpc_new("operator");
    mpc_parser_t *Expr     = mpc_new("expr");
    mpc_parser_t *Lispy    = mpc_new("lispy");

    mpca_lang(MPCA_LANG_DEFAULT,
              "double   : /-?[0-9]+\\.[0-9]?/;"
              "integer  : /-?[0-9]+/;"
              "number   : <double> | <integer> ;"
              "operator : '+' | '-' | '*' | '/' | '^' | '%' | \"min\" | \"max\"; "
              "expr     : <number> | '(' <operator> <expr>+ ')' ;"
              "lispy    : /^/ <operator> <expr>+ /$/ ;",
              Double, Integer, Number, Operator, Expr, Lispy);

    while (1) {
        char *input = readline("lispy> ");
        
        if (!input) {
            break;
        }
        
        add_history(input);

        mpc_result_t r;
        if (mpc_parse("<stdin>", input, Lispy, &r)) {
             lval_println(eval(r.output));
            mpc_ast_delete(r.output);
        } else {
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }
            
        free(input);
    }
    
    mpc_cleanup(6, Double, Integer, Number, Operator, Expr, Lispy);
    return 0;
}
