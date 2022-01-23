#include <stdio.h>
#include <string.h>
#include <math.h>
#include <editline/readline.h>
#include <errno.h>
#include "mpc.h"

struct lval_s;
struct lenv_s;
typedef struct lval_s lval;
typedef struct lenv_s lenv;

typedef lval*(*lbuiltin) (lenv*, lval*);

enum { LVAL_NUM, LVAL_ERR, LVAL_FNUM, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR, LVAL_FUNC};
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM, LERR_INVALID_OP_FMOD };

#define EXTRACT_NUM(x) (long)((x)->type == LVAL_NUM ? (x)->num : (x)->fnum)
#define EXTRACT_FNUM(x) (double)((x)->type == LVAL_NUM ? (x)->num : (x)->fnum)
#define EXTRACT_VALUE(x) ((x)->type == LVAL_NUM ? (x)->num : (x)->fnum)

#define LASSERT(args, cond, fmt, ...)                               \
  if (!(cond)) { lval* err = lval_err((fmt), ##__VA_ARGS__); lval_del(args); return err; }


struct lval_s {
  int type;
  union {
    long num;
    double fnum;
    lbuiltin func;
  };
  char *err;
  char *sym;
  int count;
  lval **cell;
};

struct lenv_s {
  int count;
  char** syms;
  lval** vals;
};

lval *lval_eval(lenv *e, lval *);
lval *lval_copy(lval *);

lval*
lval_func(lbuiltin func)
{
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUNC;
  v->func = func;
  return v;
}

lval*
lval_qexpr(void)
{
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

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
lval_err(char *fmt, ...)
{
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  va_list va;
  va_start(va, fmt);

  v->err = malloc(512);
  vsnprintf(v->err, 511, fmt, va);
  v->err = realloc(v->err, strlen(v->err) + 1);
  
  va_end(va);
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
  case  LVAL_FUNC:
    break;
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
  case LVAL_QEXPR:
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
lval_add_front(lval *v, lval *x)
{
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  memmove(v->cell+1, v->cell, sizeof(lval*) * (v->count-1));
  v->cell[0] = x;
  return v;
}

lenv*
lenv_new(void)
{
  lenv *e = malloc(sizeof(lenv));
  e->count = 0;
  e->syms = NULL;
  e->vals = NULL;
  return e;
}

void
lenv_del(lenv *e)
{
  for (int i = 0; i < e->count; i++) {
    free(e->syms[i]);
    lval_del(e->vals[i]);
  }
  free(e->syms);
  free(e->vals);
  free(e);
}

lval*
lenv_get(lenv *e, lval *k)
{
  for (int i = 0; i < e->count; i++) {
    if (strcmp(k->sym, e->syms[i]) == 0) {
      return lval_copy(e->vals[i]);
    }
  }
  return lval_err("unbound symbol: %s", k->sym);
}

void
lenv_put(lenv *e, lval *k, lval *v)
{
  for (int i = 0; i < e->count; i++) {
    if (strcmp(k->sym, e->syms[i]) == 0) {
      lval_del(e->vals[i]);
      e->vals[i] = lval_copy(v);
      return;
    }
  }
  e->count++;
  e->vals = realloc(e->vals, sizeof(lval*) * e->count);
  e->syms = realloc(e->syms, sizeof(char*) * e->count);

  e->vals[e->count-1] = lval_copy(v);
  e->syms[e->count-1] = malloc(strlen(k->sym) + 1);
  strcpy(e->syms[e->count-1], k->sym);
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
  } else if (strstr(t->tag, "qexpr")) {
    x = lval_qexpr();
  }

  for (int i = 0; i < t->children_num; i++) {
    if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
    if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
    if (strcmp(t->children[i]->tag, "regex") == 0) { continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }
  return x;
}

void
lval_expr_print(lval *v, char open, char close);

char*
ltype_name(int t) {
  switch(t) {
  case LVAL_FUNC: return "Function";
  case LVAL_NUM:
  case LVAL_FNUM:
    return "Number";
  case LVAL_ERR: return "Error";
  case LVAL_SYM: return "Symbol";
  case LVAL_SEXPR: return "S-Expression";
  case LVAL_QEXPR: return "Q-Expression";
  default: return "Unknown";
  }
}

void
lval_print(lval *v)
{
  switch (v->type) {
  case LVAL_FUNC:
    printf("<function>");
    break;
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
  case LVAL_QEXPR:
    lval_expr_print(v, '{', '}');
    break;    
  default:
     break;    
  }
}

lval*
lval_copy(lval *v)
{
  lval *x = malloc(sizeof(lval));
  x->type = v->type;
  switch (x->type) {
  case LVAL_FUNC: x->func = v->func; break;
  case LVAL_NUM: x->num = v->num; break;
  case LVAL_FNUM: x->fnum = v->fnum; break;
  case LVAL_ERR:
    x->err = malloc(strlen(v->err) + 1);
    strcpy(x->err, v->err);
    break;
  case LVAL_SYM:
    x->sym = malloc(strlen(v->sym) + 1);
    strcpy(x->sym, v->sym);
    break;
  case LVAL_QEXPR:
  case LVAL_SEXPR:
    x->count = v->count;
    x->cell = malloc(sizeof(lval*)*v->count);
    for (int i = 0; i < x->count; i++) {
      x->cell[i] = lval_copy(v->cell[i]);
    }
    break;
  }
  return x;
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
builtin_head(lenv* e, lval *a)
{
  LASSERT(a, a->count == 1, "Function 'head' expects %i arguments, got %i", 1, a->count);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'head' passed incorrect type for argument 0. "
          "Got %s, Expected %s.",  ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
  LASSERT(a, a->cell[0]->count != 0, "Function 'head' passed {}!");

  lval* v = lval_take(a, 0);
  while (v->count > 1) {
    lval_del(lval_pop(v, 1));
  }
  return v;
}

lval*
builtin_tail(lenv* e, lval *a)
{
  LASSERT(a, a->count == 1, "Function 'tail' expected %i arguments, got %i", 1, a->count);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'tail' passed incorrect type for argument 0. "
          "Got %s, Expected %s.",  ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
  LASSERT(a, a->cell[0]->count != 0, "Function 'tail' passed {}!");
  
  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v, 0));
  return v;
}

lval*
builtin_list(lenv* e, lval *a)
{
  a->type = LVAL_QEXPR;
  return a;
}

lval*
builtin_len(lenv* e, lval *a)
{
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'len' passed incorrect type for argument 0."
          "Got %s, Expected %s.",  ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
    lval* v = lval_num(a->cell[0]->count);
  lval_del(a);
  return v;
}

lval*
builtin_init(lenv* e, lval *a)
{
  LASSERT(a, a->count == 1, "Function 'init' expected %i arguments, got %i", 1, a->count);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'init' passed incorrect type for argument 0."
          "Got %s, Expected %s.",  ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));  
  LASSERT(a, a->cell[0]->count != 0, "Function 'init' passed {}!");

  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v, v->count-1));
  return v;
}

lval*
builtin_cons(lenv* e, lval *a)
{
  LASSERT(a, a->count == 2, "Function 'len' expected %i arguments, got %i", 2, a->count);

  LASSERT(a, a->cell[0]->type != LVAL_QEXPR,
          "Function 'cons' passed incorrect type for argument 0."
          "Got %s, Expected a value.", ltype_name(LVAL_QEXPR));
  LASSERT(a, a->cell[1]->type == LVAL_QEXPR,
          "Function 'cons' passed incorrect type for argument 1."
          "Got %s, Expected %s.",  ltype_name(a->cell[1]->type), ltype_name(LVAL_QEXPR));
    
  lval *v = lval_pop(a, 0);
  lval *qlist = lval_take(a, 0);
  lval_add_front(qlist, v);
  return qlist;
}

lval*
builtin_eval(lenv* e, lval *a)
{
  LASSERT(a, a->count == 1, "Function 'eval' expected %i arguments, got %i", 1, a->count);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'eval' passed incorrect type for argument 0."
          "Got %s, Expected %s.", ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));

  lval *x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(e, x);
}

lval*
lval_join(lval *a, lval *b)
{
  while (b->count) {
    a = lval_add(a, lval_pop(b, 0));
  }
  lval_del(b);
  return a;
}

lval*
builtin_join(lenv* e, lval *a)
{
  for (int i = 0; i < a->count; i++) {
    LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
            "Function 'join' passed incorrect type for argument %i."
            "Got %s, Expected %s.", i, ltype_name(a->cell[i]->type), ltype_name(LVAL_QEXPR));
  }
  lval *x = lval_pop(a, 0);
  while (a->count) {
    x = lval_join(x, lval_pop(a, 0));
  }
  lval_del(a);
  return x;
}

lval*
builtin_def(lenv *e, lval *a)
{
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'join' passed incorrect type for argument 0."
          "Got %s, Expected %s.", ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
    
  lval *syms = a->cell[0];
  for (int i = 0; i < syms->count; i++) {
    LASSERT(a, syms->cell[i]->type == LVAL_SYM, "Function 'def' cannot define non-symbol");
  }
  LASSERT(a, syms->count == a->count-1, "Function 'def': number of symbol and value not match");
  for (int i = 0; i < syms->count; i++) {
    lenv_put(e, syms->cell[i], a->cell[i+1]);
  }
  lval_del(a);
  return lval_sexpr();
}

lval*
builtin_exit(lenv *e, lval *a)
{
  LASSERT(a, a->count == 1, "Function 'eval' expected %i arguments, got %i", 1, a->count);
  LASSERT(a, a->cell[0]->type == LVAL_NUM,
          "Function 'eval' passed incorrect type for argument 0."
          "Got %s, Expected %s.", ltype_name(a->cell[0]->type), ltype_name(LVAL_NUM));
  
  exit(a->cell[0]->num);
  return NULL;
}

lval*
builtin_op(lenv* e, lval *v, const char *op)
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

lval*
builtin_add(lenv* e, lval* a)
{
  return builtin_op(e, a, "+");
}

lval*
builtin_sub(lenv* e, lval* a)
{
  return builtin_op(e, a, "-");
}

lval*
builtin_mul(lenv* e, lval* a)
{
  return builtin_op(e, a, "*");
}

lval*
builtin_div(lenv* e, lval* a)
{
  return builtin_op(e, a, "/");
}

void
lenv_add_builtin(lenv *e, char *name, lbuiltin func)
{
  lval* k = lval_sym(name);
  lval* v = lval_func(func);
  lenv_put(e, k, v);
  lval_del(k);
  lval_del(v);
}

void
lenv_init_builtins(lenv *e)
{
  lenv_add_builtin(e, "list", builtin_list);
  lenv_add_builtin(e, "head", builtin_head);
  lenv_add_builtin(e, "tail", builtin_tail);
  lenv_add_builtin(e, "eval", builtin_eval);
  lenv_add_builtin(e, "join", builtin_join);
  lenv_add_builtin(e, "init", builtin_init);
  lenv_add_builtin(e, "len", builtin_len);
  lenv_add_builtin(e, "cons", builtin_cons);
  lenv_add_builtin(e, "exit", builtin_exit);  
  lenv_add_builtin(e, "def", builtin_def);

  lenv_add_builtin(e, "+", builtin_add);
  lenv_add_builtin(e, "-", builtin_sub);
  lenv_add_builtin(e, "*", builtin_mul);
  lenv_add_builtin(e, "/", builtin_div);  
}

lval*
lval_eval_sexpr(lenv *e, lval *v)
{
  for (int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(e, v->cell[i]);
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
  if (f->type != LVAL_FUNC) {
    lval_del(f);
    lval_del(v);
    return lval_err("s-expression must start with a function");
  }

  lval* result = f->func(e, v);
  lval_del(f); 
  return result;
}

lval*
lval_eval(lenv* e, lval *v)
{
  if (v->type == LVAL_SYM) {
    lval *x = lenv_get(e, v);
    lval_del(v);
    return x;
  }
  
  if (v->type == LVAL_SEXPR) {
    return lval_eval_sexpr(e, v);
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
  mpc_parser_t* qexpr    = mpc_new("qexpr");

  mpca_lang(
      MPCA_LANG_DEFAULT,
      "                                                                     \
      number   : /-?[0-9]+/ ;                                               \
      fnumber  : /-?[0-9]+\\.[0-9]+/ ;                                      \
      symbol   : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/;                          \
      sexpr    : '(' <expr>* ')' ;                                          \
      qexpr    : '{' <expr>* '}' ;                                          \
      expr     : <fnumber> | <number> | <symbol> | <sexpr> | <qexpr>;       \
      lispy    : /^/ <expr> + /$/ ; ",
      number, fnumber, symbol, sexpr, qexpr, expr, lispy);

  lenv *env = lenv_new();
  lenv_init_builtins(env);
    
  while (1) {
    char *input = readline("lispy> ");
    if (!input) {
      printf("\nexit\n");
      break;
    }
    add_history(input);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, lispy, &r)) {
      mpc_ast_print(r.output);
      lval *x = lval_read(r.output);
      lval_println(x);      
      lval *v = lval_eval(env, x);
      lval_println(v);
      lval_del(v);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    free(input);
  }
  mpc_cleanup(7, number, fnumber, symbol, expr, sexpr, lispy, qexpr);
  return 0;
}
