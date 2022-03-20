#include <stdio.h>
#include <string.h>
#include <math.h>
#include <editline/readline.h>
#include <errno.h>
#include <assert.h>
#include "mpc.h"

mpc_parser_t* ParserNumber;
mpc_parser_t* ParserFnumber;
mpc_parser_t* ParserString;
mpc_parser_t* ParserSymbol;
mpc_parser_t* ParserSexpr;
mpc_parser_t* ParserExpr;
mpc_parser_t* ParserQexpr;
mpc_parser_t* ParserComment;
mpc_parser_t* ParserLispy;

struct lval_s;
struct lenv_s;
typedef struct lval_s lval;
typedef struct lenv_s lenv;

typedef lval*(*lbuiltin) (lenv*, lval*);

enum { LVAL_NUM, LVAL_ERR, LVAL_FNUM, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR, LVAL_FUNC, LVAL_BOOL, LVAL_STR};
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM, LERR_INVALID_OP_FMOD };

#define EXTRACT_NUM(x) (long)((x)->type == LVAL_NUM ? (x)->num : (x)->fnum)
#define EXTRACT_FNUM(x) (double)((x)->type == LVAL_NUM ? (x)->num : (x)->fnum)
#define EXTRACT_VALUE(x) ((x)->type == LVAL_NUM ? (x)->num : (x)->fnum)

#define LASSERT(args, cond, fmt, ...)                               \
  if (!(cond)) { lval* err = lval_err((fmt), ##__VA_ARGS__); lval_del((args)); return err; }

#define LASSERT_NUM(op, args, num)                               \
  LASSERT((args), (num) == (args)->count, "'%s' expects %i arguments, got %i", (op), (num), (args)->count);

#define LASSERT_TYPE(op, args, index, typ)                                                 \
  LASSERT((args), (typ) == (args)->cell[index]->type,                                      \
          "'%s' passed incorrect type for argument %i. "                          \
          "Got %s, Expected %s.", (op), index, ltype_name((args)->cell[(index)]->type), ltype_name((typ)));

#define LASSERT_TYPE_NUMBER(op, args, index)                                                 \
  LASSERT((args), LVAL_FNUM == (args)->cell[index]->type || LVAL_NUM == (args)->cell[index]->type, \
          "'%s' passed incorrect type for argument %i. "                          \
          "Got %s, Expected Number.", (op), index, ltype_name((args)->cell[(index)]->type));

#define LASSERT_EMPTY(op, args)                                 \
  LASSERT((args), (args)->count != 0, "'%s' passed {}!", (op));


struct lval_s {
  int type;
  union {
    long num;
    double fnum;
    lbuiltin func;
  };

  lenv* env;
  lval* formals;
  lval* body;

  char *err;
  char *sym;
  int count;
  lval **cell;
};

struct lenv_s {
  int count;
  lenv *parent;
  char** syms;
  lval** vals;
};

lval *lval_eval(lenv *e, lval *);
lval *lval_copy(lval *);
lenv* lenv_new(void);
void lenv_del(lenv *e);
lenv* lenv_copy(lenv *e);
lval* builtin_eval(lenv* e, lval *a);
lval* builtin_list(lenv* e, lval *a);
void lval_expr_print(lval *v, char open, char close);


lval*
new_lval()
{
  lval* v = malloc(sizeof(lval));
  bzero(v, sizeof(lval));
  return v;
}

lval*
lval_func(lbuiltin func)
{
  lval* v = new_lval();
  v->type = LVAL_FUNC;
  v->func = func;
  return v;
}

lval*
lval_str(char *str)
{
  lval* v = new_lval();
  v->type = LVAL_STR;  
  v->sym = malloc(strlen(str) + 1);
  strcpy(v->sym, str);
  return v;
}

lval*
lval_lambda(lval* formals, lval* body)
{
  lval* v= new_lval();
  v->type = LVAL_FUNC;
  v->func = NULL;

  v->env = lenv_new();
  v->formals = formals;
  v->body = body;
  return v;
}

lval*
lval_qexpr(void)
{
  lval* v = new_lval();
  v->type = LVAL_QEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

lval*
lval_num(long x)
{
  lval* v = new_lval();
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

lval*
lval_bool(int x)
{
  lval* v = new_lval();
  v->type = LVAL_BOOL;
  v->num = x;
  return v;
}

lval*
lval_fnum(double x)
{
  lval* v = new_lval();
  v->type = LVAL_FNUM;
  v->fnum = x;
  return v;
}

lval*
lval_err(char *fmt, ...)
{
  lval* v = new_lval();
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
  lval *v = new_lval();
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(s)+1);
  strcpy(v->sym, s);
  return v;
}

lval*
lval_sexpr(void)
{
  lval* v = new_lval();
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

void
lval_del(lval *v)
{
  switch (v->type) {
  case LVAL_STR:
    free(v->sym);
    break;
  case  LVAL_FUNC:
    if (!v->func) {
      lenv_del(v->env);
      lval_del(v->formals);
      lval_del(v->body);
    }
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
  e->parent = NULL;
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

lenv*
lenv_copy(lenv *e)
{
  lenv *n = malloc(sizeof(lenv));
  n->parent = e->parent;
  n->count = e->count;
  n->syms = malloc(sizeof(char*) * n->count);
  n->vals = malloc(sizeof(lval*) * n->count);
  for (int i = 0; i < n->count; i++) {
    n->syms[i] = malloc(strlen(e->syms[i]) + 1);
    strcpy(n->syms[i], e->syms[i]);
    n->vals[i] = lval_copy(e->vals[i]);
  }
  return n;
}

lval*
lenv_get(lenv *e, lval *k)
{
  for (int i = 0; i < e->count; i++) {
    if (strcmp(k->sym, e->syms[i]) == 0) {
      return lval_copy(e->vals[i]);
    }
  }

  if (e->parent) {
    return lenv_get(e->parent, k);
  }
  
  return lval_err("unbound symbol: %s", k->sym);
}

/* define local variable */
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
  e->vals = realloc(e->vals, sizeof(lval *) * e->count);
  e->syms = realloc(e->syms, sizeof(char *) * e->count);

  e->vals[e->count - 1] = lval_copy(v);
  e->syms[e->count - 1] = malloc(strlen(k->sym) + 1);
  strcpy(e->syms[e->count - 1], k->sym);
}

/* define global variable */
void
lenv_def(lenv *e, lval *k, lval *v)
{
  while (e->parent) {
    e = e->parent;
  }
  lenv_put(e, k, v);
}

lval*
lval_read_str(mpc_ast_t* t)
{
  // remove quote characters 
  t->contents[strlen(t->contents)-1] = 0;
  char *unescaped = malloc(strlen(t->contents+1)+1);
  strcpy(unescaped, t->contents+1);
  unescaped = mpcf_unescape(unescaped);
  lval* v = lval_str(unescaped);
  free(unescaped);
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

  if (strstr(t->tag, "string")) {
    return lval_read_str(t);
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
    if (strstr(t->children[i]->tag, "comment")) { continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }
  return x;
}

char*
ltype_name(int t)
{
  switch(t) {
  case LVAL_STR: return "string";
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

lval*
lval_tobool(lval *v)
{
  int i = 0;
  switch (v->type) {
  case LVAL_STR:
    i = strlen(v->sym);
    break;
  case LVAL_NUM:
  case LVAL_FNUM:
    i = EXTRACT_VALUE(v) != 0;
    break;
  case LVAL_QEXPR:
  case LVAL_SEXPR:
    i = v->count;
    break;
  case LVAL_SYM:
    i = strlen(v->sym);
    break;
  case LVAL_BOOL:
    return lval_copy(v);
  default:
    return lval_err("cannot convert %s to bool", ltype_name(v->type));
  }
  return lval_bool(i);
}

void
lval_print_str(lval *v)
{
  char *escaped = malloc(strlen(v->sym)+1);
  strcpy(escaped, v->sym);
  escaped = mpcf_escape(escaped);
  printf("\"%s\"", escaped);
  free(escaped);
}

void
lval_print(lval *v)
{
  switch (v->type) {
  case LVAL_STR:
    lval_print_str(v);
    break;
  case LVAL_BOOL: 
    if (v->num) {
      printf("<true>");
    } else {
      printf("<false>"); 
    }
    break;
  case LVAL_FUNC:
    if (v->func) {
      printf("<builtin function>");
    } else {
      printf("(\\ "); lval_print(v->formals);
      putchar(' '); lval_print(v->body); putchar(' ');
    }
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
  lval *x = new_lval();
  x->type = v->type;
  switch (x->type) {
  case LVAL_STR:
    x->sym = malloc(strlen(v->sym) + 1);
    strcpy(x->sym, v->sym);
    break;
  case LVAL_BOOL:
    x->num = v->num;
    break;
  case LVAL_FUNC:
    if (v->func) {
      x->func = v->func;
    } else {
      x->env = lenv_copy(v->env);
      x->formals = lval_copy(v->formals);
      x->body = lval_copy(v->body);
    }

    break;
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
lval_call(lenv* e, lval* f, lval *a)
{
  if (f->func) {
    return f->func(e, a);
  }
  
  int total = a->count;
  int given = f->formals->count;
  while (a->count) {
    if (f->formals->count == 0) {
      lval_del(a);
      return lval_err("Function passed too many arguments: Got: %i, Expected: %i.", given, total);
    }
    
    lval* sym = lval_pop(f->formals, 0);

    if (strcmp(sym->sym, "&") == 0) {
      // variable arguments
      if (f->formals->count != 1) {
        lval_del(a);
        return lval_err("Function format invalid. "
                        "Symbol '&' not followed by single symbol.");
      }
      lval* nsym = lval_pop(f->formals, 0);
      lenv_put(f->env, nsym, builtin_list(e, a));
      lval_del(nsym);
      lval_del(sym);
      break;
    }

    lval* val = lval_pop(a, 0);    
    lenv_put(f->env, sym, val);
    lval_del(sym);
    lval_del(val);
  }

  lval_del(a);

  /* if use doesnt support any variable arguments, assign empty list to the symbol after & */
  if (f->formals->count > 0 && strcmp(f->formals->cell[0]->sym, "&") == 0) {
    if (f->formals->count != 2) {
      return lval_err("Function format invalid. "
                      "Symbol '&' not followed by single symbol.");
    }

    lval_del(lval_pop(f->formals, 0));

    lval *sym = lval_pop(f->formals, 0);
    lval *val = lval_qexpr();

    lenv_put(f->env, sym, val);
    lval_del(sym);
    lval_del(val);
  }
  
  if (f->formals->count == 0) {
    f->env->parent = e;
    return builtin_eval(f->env, lval_add(lval_sexpr(), lval_copy(f->body)));
  } else {
    return lval_copy(f);;
  }
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
  LASSERT_NUM("head", a, 1);
  LASSERT_TYPE("head", a, 0, LVAL_QEXPR);
  LASSERT_EMPTY("head", a);

  lval* v = lval_take(a, 0);
  while (v->count > 1) {
    lval_del(lval_pop(v, 1));
  }
  return v;
}

lval*
builtin_tail(lenv* e, lval *a)
{
  LASSERT_NUM("tail", a, 1);
  LASSERT_TYPE("tail", a, 0, LVAL_QEXPR);
  LASSERT_EMPTY("tail", a);
  
  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v, 0));
  return v;
}

lval*
builtin_strtail(lenv* e, lval *a)
{
  LASSERT_NUM("strtail", a, 1);
  LASSERT_TYPE("strtail", a, 0, LVAL_STR);
  LASSERT_EMPTY("strtail", a);

  unsigned long length = strlen(a->cell[0]->sym);
  lval *v;
  if (length == 0) {
    v = lval_str("");
  } else {
    char *str = malloc(length);
    strncpy(str, a->cell[0]->sym + 1, length - 1);
    str[length - 1] = 0;
    v = lval_str(str);
    free(str);
  }
  lval_del(a);
  return v;
}

lval*
builtin_strjoin(lenv* e, lval *a)
{
  LASSERT_TYPE("strjoin", a, 0, LVAL_STR);
  LASSERT_EMPTY("strjoin", a);

  int length = 0;
  for (int i = 0; i < a->count; i++) {
    length += strlen(a->cell[i]->sym);
  }
  char *str = malloc(length+1);
  str[length] = 0;
   
  int offset = 0;
  for (int i = 0; i < a->count; i++) {
    strcpy(str+offset, a->cell[i]->sym);
    offset += strlen(a->cell[i]->sym);
  }

  lval* v = lval_str(str);
  free(str);
  lval_del(a);
  return v;
}

lval*
builtin_strhead(lenv* e, lval *a)
{
  LASSERT_NUM("strhead", a, 1);
  LASSERT_TYPE("strhead", a, 0, LVAL_STR);
  
  lval* v;
  int length = strlen(a->cell[0]->sym);
  if (length == 0) {
    v = lval_str("");
  } else {
    char str[2];
    str[0] = a->cell[0]->sym[0];
    str[1] = 0;
    v = lval_str(str);
  }

  lval_del(a);
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
  LASSERT_TYPE("len", a, 0, LVAL_QEXPR);
  lval* v = lval_num(a->cell[0]->count);
  lval_del(a);
  return v;
}

lval*
builtin_init(lenv* e, lval *a)
{
  LASSERT_NUM("init", a, 1);
  LASSERT_TYPE("init", a, 0, LVAL_QEXPR);
  LASSERT_EMPTY("init", a);
  
  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v, v->count-1));
  return v;
}

lval*
builtin_cons(lenv* e, lval *a)
{
  LASSERT_NUM("cons", a, 2);
  LASSERT_TYPE("cons", a, 1, LVAL_QEXPR);  
  
  lval *v = lval_pop(a, 0);
  lval *qlist = lval_take(a, 0);
  lval_add_front(qlist, v);
  return qlist;
}

lval*
builtin_eval(lenv* e, lval *a)
{
  LASSERT_NUM("eval", a, 1);
  LASSERT_TYPE("eval", a, 0, LVAL_QEXPR);
    
  lval *x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(e, x);
}

lval*
builtin_ord(lenv *e, lval *a, const char* op)
{
  LASSERT_NUM(op, a, 2);
  LASSERT_TYPE_NUMBER(op, a, 0);
  LASSERT_TYPE_NUMBER(op, a, 1);

  int i = 0;
  if (strcmp(op, ">") == 0) {
    i = EXTRACT_VALUE(a->cell[0]) > EXTRACT_VALUE(a->cell[1]);
  } else if (strcmp(op, ">=") == 0) {
    i = EXTRACT_VALUE(a->cell[0]) >= EXTRACT_VALUE(a->cell[1]);    
  } else if (strcmp(op, "<") == 0) {
    i = EXTRACT_VALUE(a->cell[0]) < EXTRACT_VALUE(a->cell[1]);
  } else if (strcmp(op, "<=") == 0) {
    i = EXTRACT_VALUE(a->cell[0]) <= EXTRACT_VALUE(a->cell[1]);
  }
  
  return lval_bool(i);
}

static inline lval*
builtin_gt(lenv *e, lval *a)
{
  return builtin_ord(e, a, ">");
}

static inline lval*
builtin_gte(lenv *e, lval *a)
{
  return builtin_ord(e, a, ">=");  
}

static inline lval*
builtin_lt(lenv *e, lval *a)
{
  return builtin_ord(e, a, "<");    
}

static inline lval*
builtin_lte(lenv *e, lval *a)
{
  return builtin_ord(e, a, "<=");
}

int
lval_eq(lval *x, lval *y)
{
  if (x->type != y->type)
    return 0;

  switch (x->type) {
  case LVAL_STR:
    return strcmp(x->sym, y->sym) == 0;
  case LVAL_NUM:
  case LVAL_FNUM:
    return EXTRACT_VALUE(x) == EXTRACT_VALUE(y);
  case LVAL_ERR:
    return strcmp(x->err, y->err);
  case LVAL_SYM:
    return strcmp(x->sym, y->sym);
  case LVAL_FUNC:
    if (x->func) {
      return x->func == y->func;
    } else {
      return lval_eq(x->formals, y->formals) && lval_eq(x->body, y->body);
    }
  case LVAL_QEXPR:
  case LVAL_SEXPR:
    if (x->count != y->count) return 0;
    for (int i = 0; i < x->count; i++) {
      if (!lval_eq(x->cell[i], y->cell[i])) {
        return 0;
      }
    }
    return 1;
  default:
    break;
  }
  return 0;
}

lval*
builtin_equality(lenv *e, lval *a, const char *op)
{
  LASSERT_NUM(op, a, 2);
  int r = 0;
  if (strcmp(op, "==") == 0) {
    r = lval_eq(a->cell[0], a->cell[1]);
  } else if (strcmp(op, "!=") == 0) {
    r = !lval_eq(a->cell[0], a->cell[1]);    
  }
  lval_del(a);
  return lval_bool(r);
}

static inline lval*
builtin_eq(lenv *e, lval *a)
{
  return builtin_equality(e, a, "==");
}

static inline lval*
builtin_neq(lenv *e, lval *a)
{
  return builtin_equality(e, a, "!=");
}

lval*
builtin_not(lenv *e, lval *a)
{
  LASSERT_NUM("!", a, 1);
  lval *r = lval_tobool(a);
  lval_del(a);
  if (r->type == LVAL_BOOL) {
    r->num = !r->num;
  }
  return r;
}

lval*
builtin_and(lenv *e, lval *a)
{
  int r = 1;
  lval* rl = NULL;
  for (int i = 0; i < a->count; i++) {
    lval* b = lval_tobool(a->cell[i]);
    if (b->type == LVAL_ERR) {
      rl = b;
      break;
    }
    r = r && b->num;
    if (!r) {
      rl = b;
      break;
    }
    lval_del(b);
  }
  lval_del(a);
  return rl;
}

lval*
builtin_or(lenv *e, lval *a)
{
  int r = 0;
  lval* rl = NULL;
  for (int i = 0; i < a->count; i++) {
    lval* b = lval_tobool(a->cell[i]);
    if (b->type == LVAL_ERR) {
      rl = b;
      break;
    }
    r = r || b->num;
    if (r) {
      rl = b;
      break;
    }
    lval_del(b);
  }
  lval_del(a);
  return rl;
}

lval*
builtin_show(lenv *e, lval *a)
{
  LASSERT_NUM("show", a, 1);
  LASSERT_TYPE("show", a, 0, LVAL_STR);
  printf("%s", a->cell[0]->sym);
  lval_del(a);
  return lval_sexpr();
}

lval*
builtin_print(lenv *e, lval *a)
{
  for (int i = 0; i < a->count; i++) {
    lval_print(a->cell[0]);
    putchar(' ');
  }
  putchar('\n');
  lval_del(a);
  return lval_sexpr();
}

lval*
builtin_error(lenv *e, lval *a)
{
  LASSERT_NUM("error", a, 1);
  LASSERT_TYPE("error", a, 0, LVAL_STR);
  lval* err = lval_err(a->cell[0]->sym);
  lval_del(a);
  return err;
}

lval*
builtin_read(lenv *e, lval *a)
{
  LASSERT_NUM("read", a, 1);
  LASSERT_TYPE("read", a, 0, LVAL_STR);

  mpc_result_t r;
  lval* ret;
  if (mpc_parse("<read>", a->cell[0]->sym, ParserLispy, &r)) {
    ret = lval_read(r.output);
    ret->type = LVAL_QEXPR;
  } else {
    char *error_msg = mpc_err_string(r.error);
    mpc_err_delete(r.error);
    ret = lval_err(error_msg);
  }
  lval_del(a);
  return ret;
}

lval*
builtin_load(lenv *e, lval *a)
{
  LASSERT_NUM("load", a, 1);
  LASSERT_TYPE("load", a, 0, LVAL_STR);

  mpc_result_t r;
  if (mpc_parse_contents(a->cell[0]->sym, ParserLispy, &r)) {
    lval* expr = lval_read(r.output);
    mpc_ast_delete(r.output);

    while (expr->count) {
      lval *x = lval_eval(e, lval_pop(expr, 0));
      if (x->type == LVAL_ERR) {
        lval_println(x);
      }
      lval_del(x);
    }
    lval_del(a);
    return lval_sexpr();
  } else {
    char *error_msg = mpc_err_string(r.error);
    mpc_err_delete(r.error);
    lval* err = lval_err(error_msg);
    lval_del(a);
    return err;
  }
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
    LASSERT_TYPE("join", a, i, LVAL_QEXPR);
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
  LASSERT_TYPE("def", a, 0, LVAL_QEXPR);
  lval *syms = a->cell[0];
  for (int i = 0; i < syms->count; i++) {
    LASSERT(a, syms->cell[i]->type == LVAL_SYM, "Function 'def' cannot define non-symbol");
  }
  LASSERT(a, syms->count == a->count-1, "Function 'def': number of symbol and value not match");
  for (int i = 0; i < syms->count; i++) {
    lenv_def(e, syms->cell[i], a->cell[i+1]);
  }
  lval_del(a);
  return lval_sexpr();  
}

lval*
builtin_put(lenv *e, lval *a)
{
  LASSERT_TYPE("=", a, 0, LVAL_QEXPR);
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
builtin_if(lenv *e, lval *a)
{
  LASSERT_NUM("if", a, 3);

  LASSERT_TYPE("if", a, 1, LVAL_QEXPR);
  LASSERT_TYPE("if", a, 2, LVAL_QEXPR);

  lval* b = lval_tobool(a->cell[0]);
  if (b->type == LVAL_ERR) {
    lval_del(a);
    return b;
  }
  
  lval* x;
  if (b->num) {
    a->cell[1]->type = LVAL_SEXPR;
    x = lval_eval(e, lval_pop(a, 1));
  } else {
    a->cell[2]->type = LVAL_SEXPR;
    x = lval_eval(e, lval_pop(a, 2));
  }
  
  lval_del(b);
  lval_del(a);
  return x;
}

lval*
builtin_exit(lenv *e, lval *a)
{
  LASSERT_NUM("exit", a, 1);
  LASSERT_TYPE("exit", a, 0, LVAL_NUM);
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

lval *builtin_lambda(lenv *e, lval *a)
{
  LASSERT_NUM("\\", a, 2);
  LASSERT_TYPE("\\", a, 0, LVAL_QEXPR);
  LASSERT_TYPE("\\", a, 1, LVAL_QEXPR);

  for (int i = 0; i < a->cell[0]->count; i++) {
    LASSERT(a, a->cell[0]->cell[i]->type == LVAL_SYM, "Cannot define non-symbol. Got %s, Expected: %s.",
            ltype_name(a->cell[0]->cell[i]->type), ltype_name(LVAL_SYM));
  }

  lval *formals = lval_pop(a, 0);
  lval *body = lval_pop(a, 0);
  lval_del(a);
  
  return lval_lambda(formals, body);
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
lenv_add_builtin_value(lenv *e, char *name, lval *v)
{
  lval* k = lval_sym(name);
  lenv_put(e, k, v);
  lval_del(k);
  lval_del(v);  
}

void
lenv_init_builtins(lenv *e) {
  lenv_add_builtin(e, "strtail", builtin_strtail);
  lenv_add_builtin(e, "strhead", builtin_strhead);
  lenv_add_builtin(e, "strjoin", builtin_strjoin);
  
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
  lenv_add_builtin(e, "if", builtin_if);
  lenv_add_builtin(e, "read", builtin_read);
  lenv_add_builtin(e, "=", builtin_put);
  lenv_add_builtin(e, "\\", builtin_lambda);

  lenv_add_builtin(e, "+", builtin_add);
  lenv_add_builtin(e, "-", builtin_sub);
  lenv_add_builtin(e, "*", builtin_mul);
  lenv_add_builtin(e, "/", builtin_div);

  lenv_add_builtin(e, ">", builtin_gt);
  lenv_add_builtin(e, ">=", builtin_gte);
  lenv_add_builtin(e, "<", builtin_lt);
  lenv_add_builtin(e, "<=", builtin_lte);
  lenv_add_builtin(e, "==", builtin_eq);
  lenv_add_builtin(e, "!=", builtin_neq);

  lenv_add_builtin(e, "||", builtin_or);
  lenv_add_builtin(e, "&&", builtin_and);
  lenv_add_builtin(e, "!", builtin_not);

  lenv_add_builtin(e, "load", builtin_load);
  lenv_add_builtin(e, "print", builtin_print);
  lenv_add_builtin(e, "show", builtin_show);  
  lenv_add_builtin(e, "error", builtin_error);
  
  lenv_add_builtin_value(e, "false", lval_bool(0));
  lenv_add_builtin_value(e, "true", lval_bool(1));
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
    int type = f->type;
    lval_del(f);
    lval_del(v);
    return lval_err("S-Expression starts with incorrect type. Got: %s, Expected: %s.", ltype_name(type), ltype_name(LVAL_FUNC));
  }

  lval* result = lval_call(e, f, v);
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
  
  ParserNumber  = mpc_new("number");
  ParserFnumber = mpc_new("fnumber");
  ParserString  = mpc_new("string"); 
  ParserSymbol  = mpc_new("symbol");
  ParserSexpr   = mpc_new("sexpr");  
  ParserExpr    = mpc_new("expr");
  ParserLispy   = mpc_new("lispy");
  ParserQexpr   = mpc_new("qexpr");
  ParserComment = mpc_new("comment");

  mpca_lang(
      MPCA_LANG_DEFAULT,
      "                                                                     \
      number   : /-?[0-9]+/ ;                                               \
      fnumber  : /-?[0-9]+\\.[0-9]+/ ;                                      \
      string   : /\"(\\\\.|[^\"])*\"/ ;                                     \
      comment  : /;[^\\r\\n]*/ ;                                            \
      symbol   : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/;                          \
      sexpr    : '(' <expr>* ')' ;                                          \
      qexpr    : '{' <expr>* '}' ;                                          \
      expr     : <fnumber> | <number> | <string> | <comment> |              \
                 <symbol> | <sexpr> | <qexpr>;                              \
      lispy    : /^/ <expr> + /$/ ; ",
      ParserNumber, ParserFnumber, ParserString, ParserSymbol, ParserSexpr, ParserQexpr, ParserExpr, ParserLispy, ParserComment);

  lenv *env = lenv_new();
  lenv_init_builtins(env);

  if (argc >= 2) {
    for (int i = 1; i < argc; i++) {
      lval *args = lval_add(lval_sexpr(), lval_str(argv[i]));
      lval* x = builtin_load(env, args);
      if (x->type == LVAL_ERR) {
        lval_println(x);
      }
      lval_del(x);
    }
  }
  
  while (1) {
    char *input = readline("lispy> ");
    if (!input) {
      printf("\nexit\n");
      break;
    }
    add_history(input);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, ParserLispy, &r)) {
      // mpc_ast_print(r.output);
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

  mpc_cleanup(9, ParserNumber, ParserFnumber, ParserSymbol, ParserExpr, ParserSexpr, ParserLispy, ParserQexpr, ParserString, ParserComment);
  return 0;
}
