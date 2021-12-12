#include <stdio.h>
#include <string.h>
#include <math.h>
#include <editline/readline.h>
#include <errno.h>
#include "mpc.h"

enum { LVAL_NUM, LVAL_ERR, LVAL_FNUM, LVAL_SYM, LVAL_SEXPR};
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM, LERR_INVALID_OP_FMOD };

#define EXTRACT_NUM(x) (long)((x)->type == LVAL_NUM ? (x)->num : (x)->fnum)
#define EXTRACT_FNUM(x) (double)((x)->type == LVAL_NUM ? (x)->num : (x)->fnum)
#define EXTRACT_VALUE(x) ((x)->type == LVAL_NUM ? (x)->num : (x)->fnum)

struct lval_s {
  int type;
  union {
    long num;
    double fnum;
  };
  char *err;
  char *sym;
  int count;
  struct lval_s **cell;
};

typedef struct lval_s lval;


lval*
lval_num(long x)
{
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

lval*
lval_fnum(double x)
{
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FNUM;
  v->fnum = x;
  return v;
}

lval*
lval_err(char *err)
{
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  v->err = malloc(strlen(err) + 1);
  strcpy(v->err, err);
  return v;
}

lval*
lval_sym(char *s)
{
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(s)+1);
  strcpy(v->sym, s);
  return v;
}

lval*
lval_sexpr(void)
{
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

void
lval_del(lval *v)
{
  switch (v->type) {
  case LVAL_NUM:
  case LVAL_FNUM:
    break;
  case LVAL_SYM:
    free(v->sym);
    break;
  case LVAL_ERR:
    free(v->err);
    break;
  case LVAL_SEXPR:
    for (int i = 0; i < v->count; i++) {
      lval_del(v->cell[i]);
    }
    free(v->cell);
    break;
  };
  free(v);
  return;
}

lval*
lval_read_num(mpc_ast_t *t)
{
  errno = 0;
  long x = strtol(t->contents, NULL, 10);
  return errno != ERANGE ? lval_num(x) : lval_err("invalid number");
}

lval*
lval_read_fnum(mpc_ast_t *t)
{
  errno = 0;
  long x = strtod(t->contents, NULL);
  return errno != ERANGE ? lval_fnum(x) : lval_err("invalid number");  
}

lval*
lval_add(lval *v, lval *x)
{
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  v->cell[v->count-1] = x;
  return v;
}

lval*
lval_read(mpc_ast_t *t) {
  if (strstr(t->tag, "fnumber")) {
    return lval_read_fnum(t);
  }
  
  if (strstr(t->tag, "number")) {
    return lval_read_num(t);
  }

  if (strstr(t->tag, "symbol")) {
    return lval_sym(t->contents);
  }

  lval *x = NULL;
  // root
  if (strcmp(t->tag, ">") == 0 || strstr(t->tag, "sexpr")) {
    x = lval_sexpr();
  }
  
  for (int i = 0; i < t->children_num; i++) {
    if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
    if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
    if (strcmp(t->children[i]->tag, "regex") == 0) { continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }
  return x;
}

void
lval_expr_print(lval *v, char open, char close);

void
lval_print(lval *v)
{
  switch (v->type) {
  case LVAL_NUM:
    printf("%li", v->num);
    break;
  case LVAL_FNUM:
    printf("%f", v->fnum);
    break;
  case LVAL_SYM:
    printf("%s", v->sym);
    break;
  case LVAL_ERR:
    printf("Error: %s", v->err);
    break;
  case LVAL_SEXPR:
    lval_expr_print(v, '(', ')');
    break;
  default:
     break;    
  }
}

void
lval_expr_print(lval *v, char open, char close)
{
  putchar(open);
  for (int i = 0; i < v->count; i++) {
    lval_print(v->cell[i]);

    if (i != v->count - 1) {
      putchar(' ');
    }
  }
  putchar(close);
}

void
lval_println(lval *v)
{
  lval_print(v);
  putchar('\n');
}

lval*
lval_pop(lval *v, int i)
{
  lval *x = v->cell[i];
  memmove(v->cell+i, v->cell+i+1, sizeof(lval*) * (v->count-i-1));

  v->count--;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  return x;
}

lval*
lval_take(lval *v, int i)
{
  lval *x = lval_pop(v, i);
  lval_del(v);
  return x;
}

lval*
eval_add(lval *x, lval *y)
{
  if (x->type == LVAL_FNUM || y->type == LVAL_FNUM) {
    // float
    return lval_fnum(EXTRACT_FNUM(x) + EXTRACT_FNUM(y));
  }
  return lval_num(EXTRACT_NUM(x) + EXTRACT_NUM(y));
}

lval*
eval_minus(lval *x, lval *y)
{
  if (x->type == LVAL_FNUM || y->type == LVAL_FNUM) {
    // float
    return lval_fnum(EXTRACT_FNUM(x) - EXTRACT_FNUM(y));
  }
  return lval_num(EXTRACT_NUM(x) - EXTRACT_NUM(y));
}

lval*
eval_mod(lval *x, lval *y) {
  if (x->type == LVAL_FNUM || y->type == LVAL_FNUM) {
    // float
    return lval_err("float modulo.");
  }
  return lval_num(EXTRACT_NUM(x) % EXTRACT_NUM(y));
}

lval*
eval_mul(lval *x, lval *y) {
  if (x->type == LVAL_FNUM || y->type == LVAL_FNUM) {
    // float
    return lval_fnum(EXTRACT_FNUM(x) * EXTRACT_FNUM(y));
  }
  return lval_num(EXTRACT_NUM(x) * EXTRACT_NUM(y));
}

lval*
eval_div(lval *x, lval *y) {
  if (EXTRACT_VALUE(y) == 0) {
    return lval_err("Division by zero.");
  }

  if (x->type == LVAL_FNUM || y->type == LVAL_FNUM) {
    // float
    return lval_fnum(EXTRACT_FNUM(x) / EXTRACT_FNUM(y));
  }
  return lval_num(EXTRACT_NUM(x) / EXTRACT_NUM(y));
}

lval*
eval_min(lval *x, lval *y) {
  if (x->type == LVAL_FNUM || y->type == LVAL_FNUM) {
    // float
    return lval_fnum(EXTRACT_VALUE(x) < EXTRACT_VALUE(y) ? EXTRACT_VALUE(x) : EXTRACT_VALUE(y));
  }
  return lval_num(EXTRACT_VALUE(x) < EXTRACT_VALUE(y) ? EXTRACT_VALUE(x) : EXTRACT_VALUE(y));
}

lval*
eval_max(lval *x, lval *y) {
  if (x->type == LVAL_FNUM || y->type == LVAL_FNUM) {
    // float
    return lval_fnum(EXTRACT_VALUE(x) > EXTRACT_VALUE(y) ? EXTRACT_VALUE(x) : EXTRACT_VALUE(y));
  }
  return lval_num(EXTRACT_VALUE(x) > EXTRACT_VALUE(y) ? EXTRACT_VALUE(x) : EXTRACT_VALUE(y));
}

lval*
eval_pow(lval *x, lval *y)
{
  if (x->type == LVAL_FNUM || y->type == LVAL_FNUM) {
    // float
    return lval_fnum(powl(EXTRACT_FNUM(x), EXTRACT_FNUM(y)));
  }
  return lval_num((long)powl(EXTRACT_FNUM(x), EXTRACT_FNUM(y)));
}

lval*
eval_op(lval *x, const char *op, lval *y)
{
  lval* r = NULL;
  if (strcmp(op, "+") == 0) { r = eval_add(x, y); }
  else if (strcmp(op, "-") == 0) { r = eval_minus(x, y); }
  else if (strcmp(op, "*") == 0) { r = eval_mul(x, y); }
  else if (strcmp(op, "%") == 0) { r = eval_mod(x, y); }
  else if (strcmp(op, "min") == 0) { r =  eval_min(x, y); }
  else if (strcmp(op, "max") == 0) { r = eval_max(x, y); }
  else if (strcmp(op, "^") == 0) { r = eval_pow(x, y); }
  else if (strcmp(op, "/") == 0) { r = eval_div(x, y); }
  else { r = lval_err("invalid op"); }
  
  return r;
}

lval*
eval_unary(const char *op, lval *a)
{
  lval *x = NULL;
  if (a->type == LVAL_FNUM) {
    x = lval_fnum(a->fnum);
  } else if (a->type == LVAL_NUM) {
    x = lval_num(a->num);
  } else {
    return lval_err("require a number"); 
  }

  if (strcmp(op, "+") == 0) { return x; }
  if (strcmp(op, "-") == 0) {
    if (x->type == LVAL_FNUM) {
      x->fnum = -x->fnum;
    }
    if (x->type == LVAL_NUM) {
      x->num = -x->num;
    }
    return x;
  };
  lval_del(x);
  return lval_err("invalid expression");
}

lval*
builtin_op(lval *v, const char *op)
{
  for (int i = 0; i < v->count; i++) {
    int type = v->cell[i]->type;
    if (type != LVAL_NUM && type != LVAL_FNUM) {
      lval_del(v);
      return lval_err("cannot operate on non-number!");
    }
  }

  lval* x = lval_pop(v, 0);
  // unary operator
  if (v->count == 0) {
    lval *r = eval_unary(op, x);
    lval_del(x);
    return r;
  }

  while (v->count) {
    lval *y = lval_pop(v, 0);
    // eval_op always returns a new obj, it has to be free
    lval *r = eval_op(x, op, y);
    lval_del(x);
    lval_del(y);
    
    x = r;
    if (x->type == LVAL_ERR) {
      break;
    }
  };
  lval_del(v);
  return x;
}

lval* lval_eval(lval *v);

lval*
lval_eval_sexpr(lval *v)
{
  for (int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(v->cell[i]);
  }

  for (int i = 0; i < v->count; i++) {
    if (v->cell[i]->type == LVAL_ERR) {
      return lval_take(v, i);
    }
  }

  if (v->count == 0) {
    return v;
  }

  if (v->count == 1) {
    return lval_take(v, 0);
  }

  lval *f = lval_pop(v, 0);
  if (f->type != LVAL_SYM) {
    lval_del(f);
    lval_del(v);
    return lval_err("s-expression must start with symbol");
  }

  lval* result = builtin_op(v, f->sym);
  lval_del(f);
  return result;
}

lval*
lval_eval(lval *v)
{
  if (v->type == LVAL_SEXPR) {
    return lval_eval_sexpr(v);
  }
  return v;
}

int
main(int argc, char **argv)
{
  puts("Lispy Version 0.0.0.0.0.1");
  puts("Press Ctrl+c to exit\n");
  
  mpc_parser_t* number   = mpc_new("number");
  mpc_parser_t* fnumber   = mpc_new("fnumber");  
  mpc_parser_t* symbol = mpc_new("symbol");
  mpc_parser_t* sexpr     = mpc_new("sexpr");  
  mpc_parser_t* expr     = mpc_new("expr");
  mpc_parser_t* lispy    = mpc_new("lispy");
  
  mpca_lang(MPCA_LANG_DEFAULT,
  "                                                                      \
  number   : /-?[0-9]+/ ;                                                \
  fnumber   : /-?[0-9]+\\.[0-9]+/ ;                                         \
  symbol : '%' | '+' | '-' | '*' | '/' | '^' | \"min\" | \"max\";      \
  sexpr    : '(' <expr>* ')' ; \
  expr     : <fnumber> | <number> | <symbol> | <sexpr> ; \
  lispy    : /^/ <expr>+ /$/ ;            \
  ",
  number, fnumber, symbol, sexpr, expr, lispy);

  while (1) {
    char *input = readline("lispy> ");
    add_history(input);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, lispy, &r)) {
      mpc_ast_print(r.output);
      lval *x = lval_read(r.output);
      lval_println(x);      
      lval *v = lval_eval(x);
      lval_println(v);
      lval_del(v);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    free(input);
  }
  mpc_cleanup(6, number, fnumber, symbol, expr, sexpr, lispy);
  return 0;
}
