#include <stdio.h>
#include <string.h>
#include <math.h>
#include <editline/readline.h>
#include <errno.h>
#include "mpc.h"

enum { LVAL_NUM, LVAR_ERR };
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

typedef struct {
  int type;
  union {
    long num;
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
eval_op(lval x, const char *op, lval y)
{
  if (x.type == LVAR_ERR) return x;
  if (y.type == LVAR_ERR) return y;

  if (strcmp(op, "+") == 0) { return lval_num(x.num+y.num); }
  if (strcmp(op, "-") == 0) { return lval_num(x.num-y.num); }
  if (strcmp(op, "*") == 0) { return lval_num(x.num*y.num); }
  if (strcmp(op, "%") == 0) { return lval_num(x.num%y.num); }
  if (strcmp(op, "min") == 0) { return x.num > y.num ? y : x; }
  if (strcmp(op, "max") == 0) { return x.num < y.num ? y : x; }
  if (strcmp(op, "^") == 0) { return lval_num(powl(x.num, y.num)); }

  if (strcmp(op, "/") == 0) {
    if (y.num == 0)
      return lval_err(LERR_DIV_ZERO);
    return lval_num(x.num/y.num);
  }
  
  return lval_err(LERR_BAD_OP);
}

lval
eval_unary(const char *op, lval x)
{
  if (strcmp(op, "+") == 0) { return x; }
  if (strcmp(op, "-") == 0) { return lval_num(-x.num); }
  return lval_err(LERR_BAD_OP);
}

lval
eval(mpc_ast_t *ast)
{
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
  mpc_parser_t* operator = mpc_new("operator");
  mpc_parser_t* expr     = mpc_new("expr");
  mpc_parser_t* lispy    = mpc_new("lispy");
  
  mpca_lang(MPCA_LANG_DEFAULT,
  "                                                                      \
  number   : /-?[0-9]+/ ;                                                \
  operator : '%' | '+' | '-' | '*' | '/' | '^' | \"min\" | \"max\";      \
  expr     : <number> | '(' <operator> <expr>+ ')' ; \
  lispy    : /^/ <operator> <expr>+ /$/ ;            \
  ",
  number, operator, expr, lispy);

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
