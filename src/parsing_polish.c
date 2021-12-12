#include <stdio.h>
#include <string.h>
#include <math.h>
#include <editline/readline.h>
#include <errno.h>
#include "mpc.h"

enum { LVAL_NUM, LVAR_ERR, LVAL_FNUM };
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM, LERR_INVALID_OP_FMOD };

#define EXTRACT_NUM(x) (long)((x).type == LVAL_NUM ? (x).num : (x).fnum)
#define EXTRACT_FNUM(x) (double)((x).type == LVAL_NUM ? (x).num : (x).fnum)
#define EXTRACT_VALUE(x) ((x).type == LVAL_NUM ? (x).num : (x).fnum)

typedef struct {
  int type;
  union {
    long num;
    double fnum;
    int err;
  };
} lval;

lval
lval_num(long x)
{
  lval v;
  v.type = LVAL_NUM;
  v.num = x;
  return v;
}

lval
lval_fnum(double x)
{
  lval v;
  v.type = LVAL_FNUM;
  v.fnum = x;
  return v;
}

lval
lval_err(int err)
{
  lval v;
  v.type = LVAR_ERR;
  v.err = err;
  return v;
}

void
lval_print(lval v)
{
  switch (v.type) {
  case LVAL_NUM:
    printf("%li", v.num);
    break;
  case LVAL_FNUM:
    printf("%f", v.fnum);
    break;    
  case LVAR_ERR:
    switch (v.err) {
    case LERR_BAD_OP:
       printf("Error: Invalid operator.");
      break;
    case LERR_BAD_NUM:
      printf("Error: Invalid number.");
      break;
    case LERR_DIV_ZERO:
      printf("Error: Division by zero.");
      break;
    case LERR_INVALID_OP_FMOD:
      printf("Error: float modulo.");
      break;      
    default:
      printf("unknown error: %d", v.err);
        break;
    }
    break;
  default:
    printf("unknown type: %d", v.type);
  }
}

#define lval_println(x) { lval_print((x)); printf("\n"); }

lval
eval_add(lval x, lval y)
{
  if (x.type == LVAL_FNUM || y.type == LVAL_FNUM) {
    // float
    return lval_fnum(EXTRACT_FNUM(x) + EXTRACT_FNUM(y));
  }
  return lval_num(EXTRACT_NUM(x) + EXTRACT_NUM(y));
}

lval eval_minus(lval x, lval y)
{
  if (x.type == LVAL_FNUM || y.type == LVAL_FNUM) {
    // float
    return lval_fnum(EXTRACT_FNUM(x) - EXTRACT_FNUM(y));
  }
  return lval_num(EXTRACT_NUM(x) - EXTRACT_NUM(y));  
}

lval eval_mod(lval x, lval y) {
  if (x.type == LVAL_FNUM || y.type == LVAL_FNUM) {
    // float
    return lval_err(LERR_INVALID_OP_FMOD);
  }
  return lval_num(EXTRACT_NUM(x) % EXTRACT_NUM(y));  
}

lval eval_mul(lval x, lval y) {
  if (x.type == LVAL_FNUM || y.type == LVAL_FNUM) {
    // float
    return lval_fnum(EXTRACT_FNUM(x) * EXTRACT_FNUM(y));
  }
  return lval_num(EXTRACT_NUM(x) * EXTRACT_NUM(y));    
}

lval eval_div(lval x, lval y) {
  if (EXTRACT_VALUE(y) == 0) {
    return lval_err(LERR_DIV_ZERO);
  }

  if (x.type == LVAL_FNUM || y.type == LVAL_FNUM) {
    // float
    return lval_fnum(EXTRACT_FNUM(x) / EXTRACT_FNUM(y));
  }
  return lval_num(EXTRACT_NUM(x) / EXTRACT_NUM(y));    
}

lval eval_min(lval x, lval y) {  
  if (x.type == LVAL_FNUM || y.type == LVAL_FNUM) {
    // float
    return lval_fnum(EXTRACT_VALUE(x) < EXTRACT_VALUE(y) ? EXTRACT_VALUE(x) : EXTRACT_VALUE(y));
  }
  return lval_num(EXTRACT_VALUE(x) < EXTRACT_VALUE(y) ? EXTRACT_VALUE(x) : EXTRACT_VALUE(y));
}

lval eval_max(lval x, lval y) {  
  if (x.type == LVAL_FNUM || y.type == LVAL_FNUM) {
    // float
    return lval_fnum(EXTRACT_VALUE(x) > EXTRACT_VALUE(y) ? EXTRACT_VALUE(x) : EXTRACT_VALUE(y));
  }
  return lval_num(EXTRACT_VALUE(x) > EXTRACT_VALUE(y) ? EXTRACT_VALUE(x) : EXTRACT_VALUE(y));
}

lval eval_pow(lval x, lval y)
{
  if (x.type == LVAL_FNUM || y.type == LVAL_FNUM) {
    // float
    return lval_fnum(powl(EXTRACT_FNUM(x), EXTRACT_FNUM(y)));
  }
  return lval_num((long)powl(EXTRACT_FNUM(x), EXTRACT_FNUM(y)));
}

lval
eval_op(lval x, const char *op, lval y)
{
  if (x.type == LVAR_ERR) return x;
  if (y.type == LVAR_ERR) return y;

  if (strcmp(op, "+") == 0) { return eval_add(x, y); }
  if (strcmp(op, "-") == 0) { return eval_minus(x, y); }
  if (strcmp(op, "*") == 0) { return eval_mul(x, y); }
  if (strcmp(op, "%") == 0) { return eval_mod(x, y); }
  if (strcmp(op, "min") == 0) { return eval_min(x, y); }
  if (strcmp(op, "max") == 0) { return eval_max(x, y); }
  if (strcmp(op, "^") == 0) { return eval_pow(x, y); }
  if (strcmp(op, "/") == 0) { return eval_div(x, y); }
  
  return lval_err(LERR_BAD_OP);
}

lval
eval_unary(const char *op, lval x)
{
  if (strcmp(op, "+") == 0) { return x; }
  if (strcmp(op, "-") == 0) {
    if (x.type == LVAL_FNUM) {
      x.fnum = -x.fnum;
    }
    if (x.type == LVAL_NUM) {
      x.num = -x.num;
    }
    return x;
  };
  return lval_err(LERR_BAD_OP);
}

lval
eval(mpc_ast_t *ast)
{
  if (strstr(ast->tag, "fnumber")) {
    errno = 0;
    double x = strtod(ast->contents, NULL);
    return errno != ERANGE ? lval_fnum(x) : lval_err(LERR_BAD_NUM);
  }
  
  if (strstr(ast->tag, "number")) {
    errno = 0;
    long x = strtol(ast->contents, NULL, 10);
    return errno != ERANGE ? lval_num(x) : lval_err(LERR_BAD_NUM);
  }

  char *op = ast->children[1]->contents;

  lval x = eval(ast->children[2]);

  if (ast->children_num == 4) { // operator + number + two wrappers(see output of ast)
    return eval_unary(op, x);
  }

  int i = 3;
  while (strstr(ast->children[i]->tag, "expr")) {
    x = eval_op(x, op, eval(ast->children[i]));
    i++;
  }
  return x;
}

int
main(int argc, char **argv)
{
  puts("Lispy Version 0.0.0.0.0.1");
  puts("Press Ctrl+c to exit\n");
  
  mpc_parser_t* number   = mpc_new("number");
  mpc_parser_t* fnumber   = mpc_new("fnumber");  
  mpc_parser_t* operator = mpc_new("operator");
  mpc_parser_t* expr     = mpc_new("expr");
  mpc_parser_t* lispy    = mpc_new("lispy");
  
  mpca_lang(MPCA_LANG_DEFAULT,
  "                                                                      \
  number   : /-?[0-9]+/ ;                                                \
  fnumber   : /-?[0-9]+\\.[0-9]+/ ;                                         \
  operator : '%' | '+' | '-' | '*' | '/' | '^' | \"min\" | \"max\";      \
  expr     : <fnumber> | <number> | '(' <operator> <expr>+ ')' ; \
  lispy    : /^/ <operator> <expr>+ /$/ ;            \
  ",
  number, fnumber, operator, expr, lispy);

  while (1) {
    char *input = readline("lispy> ");
    add_history(input);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, lispy, &r)) {
      mpc_ast_print(r.output);
      lval_println(eval(r.output));
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    free(input);
  }
  mpc_cleanup(4, number, operator, expr, lispy);            
  return 0;
}
