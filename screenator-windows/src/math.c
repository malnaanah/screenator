/*
 * Screenator for Windows -- math expression parser for the Math Function tool.
 * GPL-2.0-or-later.  See app.h.
 *
 * Grammar (recursive descent):
 *   expr    := term (('+'|'-') term)*
 *   term    := unary (('*'|'/') unary | implicit-unary)*
 *   unary   := ('-'|'+') unary | power
 *   power   := primary ('^' unary)?
 *   primary := number | constant | variable | function '(' expr ')' | '(' expr ')'
 *
 * Implied multiplication is supported, so "2pi*x" and "2x" work.
 */

#include "app.h"

enum
{
  OP_NUM = 1, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW, OP_NEG, OP_FUNC
};

enum
{
  F_SIN = 1, F_COS, F_TAN, F_ASIN, F_ACOS, F_ATAN, F_SINH, F_COSH, F_TANH,
  F_EXP, F_LOG, F_SQRT, F_ABS, F_FLOOR, F_CEIL, F_ROUND, F_SIGN, F_UNIT,
  F_DELTA, F_RECT, F_MIN, F_MAX, F_POW, F_ATAN2
};

typedef struct MathNode
{
  int op;
  double num;
  int func;
  struct MathNode *a, *b;
} MathNode;

struct MathParser
{
  MathNode *root;
};

typedef struct
{
  const char *s;
  int pos;
  int type;          /* 0 end, 1 number, 2 ident, 3 punctuation */
  double num;
  char ident[64];
  char punct;
} Lexer;

static void lex_next(Lexer *l)
{
  while (l->s[l->pos] == ' ' || l->s[l->pos] == '\t' ||
         l->s[l->pos] == '\r' || l->s[l->pos] == '\n')
    l->pos++;

  char c = l->s[l->pos];
  if (!c)
    {
      l->type = 0;
      return;
    }

  if ((c >= '0' && c <= '9') || c == '.')
    {
      char *end = NULL;
      l->num = strtod(l->s + l->pos, &end);
      l->pos = (int) (end - l->s);
      l->type = 1;
      return;
    }

  if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')
    {
      int n = 0;
      while (((l->s[l->pos] >= 'A' && l->s[l->pos] <= 'Z') ||
              (l->s[l->pos] >= 'a' && l->s[l->pos] <= 'z') ||
              (l->s[l->pos] >= '0' && l->s[l->pos] <= '9') ||
              l->s[l->pos] == '_') && n < 63)
        l->ident[n++] = l->s[l->pos++];
      l->ident[n] = 0;
      l->type = 2;
      return;
    }

  l->punct = c;
  l->pos++;
  l->type = 3;
}

static MathNode *node_new(int op)
{
  MathNode *n = (MathNode *) calloc(1, sizeof(MathNode));
  if (n) n->op = op;
  return n;
}

static void node_free(MathNode *n)
{
  if (!n) return;
  node_free(n->a);
  node_free(n->b);
  free(n);
}

static int func_id(const char *name)
{
  if (!strcmp(name, "sin")) return F_SIN;
  if (!strcmp(name, "cos")) return F_COS;
  if (!strcmp(name, "tan")) return F_TAN;
  if (!strcmp(name, "asin")) return F_ASIN;
  if (!strcmp(name, "acos")) return F_ACOS;
  if (!strcmp(name, "atan")) return F_ATAN;
  if (!strcmp(name, "sinh")) return F_SINH;
  if (!strcmp(name, "cosh")) return F_COSH;
  if (!strcmp(name, "tanh")) return F_TANH;
  if (!strcmp(name, "exp")) return F_EXP;
  if (!strcmp(name, "log") || !strcmp(name, "ln")) return F_LOG;
  if (!strcmp(name, "sqrt")) return F_SQRT;
  if (!strcmp(name, "abs")) return F_ABS;
  if (!strcmp(name, "floor")) return F_FLOOR;
  if (!strcmp(name, "ceil")) return F_CEIL;
  if (!strcmp(name, "round")) return F_ROUND;
  if (!strcmp(name, "sign")) return F_SIGN;
  if (!strcmp(name, "unit") || !strcmp(name, "step")) return F_UNIT;
  if (!strcmp(name, "delta")) return F_DELTA;
  if (!strcmp(name, "rect")) return F_RECT;
  if (!strcmp(name, "min")) return F_MIN;
  if (!strcmp(name, "max")) return F_MAX;
  if (!strcmp(name, "pow")) return F_POW;
  if (!strcmp(name, "atan2")) return F_ATAN2;
  return 0;
}

static MathNode *parse_expr(Lexer *l, const char **error);

static MathNode *parse_primary(Lexer *l, const char **error)
{
  if (l->type == 1)
    {
      MathNode *n = node_new(OP_NUM);
      if (!n) { *error = "Out of memory"; return NULL; }
      n->num = l->num;
      lex_next(l);
      return n;
    }

  if (l->type == 2)
    {
      char name[64];
      strcpy(name, l->ident);
      lex_next(l);

      if (l->type == 3 && l->punct == '(')
        {
          int f = func_id(name);
          if (!f)
            {
              *error = "Unknown function";
              return NULL;
            }
          lex_next(l);
          MathNode *a = parse_expr(l, error);
          if (!a) return NULL;
          if (l->type == 3 && l->punct == ',')
            {
              /* Two-argument functions. */
              lex_next(l);
              MathNode *b = parse_expr(l, error);
              if (!b) { node_free(a); return NULL; }
              if (f != F_MIN && f != F_MAX && f != F_POW && f != F_ATAN2)
                {
                  *error = "Function takes one argument";
                  node_free(a); node_free(b);
                  return NULL;
                }
              if (!(l->type == 3 && l->punct == ')'))
                {
                  *error = "Missing ')'";
                  node_free(a); node_free(b);
                  return NULL;
                }
              lex_next(l);
              MathNode *n = node_new(OP_FUNC);
              if (!n) { node_free(a); node_free(b); *error = "Out of memory"; return NULL; }
              n->func = f;
              n->a = a;
              n->b = b;
              return n;
            }
          if (!(l->type == 3 && l->punct == ')'))
            {
              *error = "Missing ')'";
              node_free(a);
              return NULL;
            }
          lex_next(l);
          MathNode *n = node_new(OP_FUNC);
          if (!n) { node_free(a); *error = "Out of memory"; return NULL; }
          n->func = f;
          n->a = a;
          return n;
        }

      if (!strcmp(name, "pi")) { MathNode *n = node_new(OP_NUM); if (!n) { *error = "Out of memory"; return NULL; } n->num = M_PI; return n; }
      if (!strcmp(name, "e")) { MathNode *n = node_new(OP_NUM); if (!n) { *error = "Out of memory"; return NULL; } n->num = M_E; return n; }
      if (!strcmp(name, "x") || !strcmp(name, "theta"))
        {
          MathNode *n = node_new(OP_FUNC);
          if (!n) { *error = "Out of memory"; return NULL; }
          n->func = 0;                /* identity: the variable */
          return n;
        }
      *error = "Unknown name in expression";
      return NULL;
    }

  if (l->type == 3 && l->punct == '(')
    {
      lex_next(l);
      MathNode *n = parse_expr(l, error);
      if (!n) return NULL;
      if (!(l->type == 3 && l->punct == ')'))
        {
          *error = "Missing ')'";
          node_free(n);
          return NULL;
        }
      lex_next(l);
      return n;
    }

  *error = l->type == 0 ? "Unexpected end of expression"
                        : "Unexpected character in expression";
  return NULL;
}

static MathNode *parse_power(Lexer *l, const char **error)
{
  MathNode *base = parse_primary(l, error);
  if (!base) return NULL;

  if (l->type == 3 && l->punct == '^')
    {
      lex_next(l);
      MathNode *exp = parse_power(l, error);   /* right associative */
      if (!exp) { node_free(base); return NULL; }
      MathNode *n = node_new(OP_POW);
      if (!n) { node_free(base); node_free(exp); *error = "Out of memory"; return NULL; }
      n->a = base;
      n->b = exp;
      return n;
    }
  return base;
}

static MathNode *parse_unary(Lexer *l, const char **error)
{
  if (l->type == 3 && (l->punct == '-' || l->punct == '+'))
    {
      char op = l->punct;
      lex_next(l);
      MathNode *a = parse_unary(l, error);
      if (!a) return NULL;
      if (op == '+') return a;
      MathNode *n = node_new(OP_NEG);
      if (!n) { node_free(a); *error = "Out of memory"; return NULL; }
      n->a = a;
      return n;
    }
  return parse_power(l, error);
}

static int starts_primary(Lexer *l)
{
  return l->type == 1 || l->type == 2 ||
         (l->type == 3 && l->punct == '(');
}

static MathNode *parse_term(Lexer *l, const char **error)
{
  MathNode *left = parse_unary(l, error);
  if (!left) return NULL;

  for (;;)
    {
      int op = 0;
      if (l->type == 3 && (l->punct == '*' || l->punct == '/'))
        {
          op = l->punct == '*' ? OP_MUL : OP_DIV;
          lex_next(l);
        }
      else if (starts_primary(l))
        {
          op = OP_MUL;             /* implied multiplication */
        }
      else
        break;

      MathNode *right = parse_unary(l, error);
      if (!right)
        {
          node_free(left);
          return NULL;
        }
      MathNode *n = node_new(op);
      if (!n) { node_free(left); node_free(right); *error = "Out of memory"; return NULL; }
      n->a = left;
      n->b = right;
      left = n;
    }
  return left;
}

static MathNode *parse_expr(Lexer *l, const char **error)
{
  MathNode *left = parse_term(l, error);
  if (!left) return NULL;

  while (l->type == 3 && (l->punct == '+' || l->punct == '-'))
    {
      char op = l->punct;
      lex_next(l);
      MathNode *right = parse_term(l, error);
      if (!right) { node_free(left); return NULL; }
      MathNode *n = node_new(op == '+' ? OP_ADD : OP_SUB);
      if (!n) { node_free(left); node_free(right); *error = "Out of memory"; return NULL; }
      n->a = left;
      n->b = right;
      left = n;
    }
  return left;
}

MathParser *math_parse(const char *expr, const char **error)
{
  Lexer l;
  const char *err = NULL;
  MathNode *root;

  memset(&l, 0, sizeof(l));
  l.s = expr;
  lex_next(&l);

  if (l.type == 0)
    {
      if (error) *error = "Expression is empty";
      return NULL;
    }

  root = parse_expr(&l, &err);
  if (!root)
    {
      if (error) *error = err ? err : "Invalid expression";
      return NULL;
    }
  if (l.type != 0)
    {
      node_free(root);
      if (error) *error = "Unexpected text after the expression";
      return NULL;
    }

  MathParser *p = (MathParser *) calloc(1, sizeof(MathParser));
  if (!p) { node_free(root); if (error) *error = "Out of memory"; return NULL; }
  p->root = root;
  return p;
}

void math_free(MathParser *m)
{
  if (!m) return;
  node_free(m->root);
  free(m);
}

static double math_eval_node(MathNode *n, double x);

int math_eval(MathParser *m, double x, double *out)
{
  if (!m || !m->root)
    {
      *out = NAN;
      return 0;
    }

  /* Recursion depth is bounded by the expression length, which is short. */
  double v = math_eval_node(m->root, x);
  *out = v;
  return isfinite(v) ? 1 : 0;
}

static double math_eval_node(MathNode *n, double x)
{
  if (!n) return NAN;

  switch (n->op)
    {
    case OP_NUM: return n->num;
    case OP_ADD: return math_eval_node(n->a, x) + math_eval_node(n->b, x);
    case OP_SUB: return math_eval_node(n->a, x) - math_eval_node(n->b, x);
    case OP_MUL: return math_eval_node(n->a, x) * math_eval_node(n->b, x);
    case OP_DIV:
      {
        double d = math_eval_node(n->b, x);
        return d == 0.0 ? NAN : math_eval_node(n->a, x) / d;
      }
    case OP_POW: return pow(math_eval_node(n->a, x), math_eval_node(n->b, x));
    case OP_NEG: return -math_eval_node(n->a, x);
    case OP_FUNC:
      {
        if (n->func == 0) return x;
        double a = math_eval_node(n->a, x);
        double b = n->b ? math_eval_node(n->b, x) : 0.0;
        switch (n->func)
          {
          case F_SIN: return sin(a);
          case F_COS: return cos(a);
          case F_TAN: return tan(a);
          case F_ASIN: return asin(a);
          case F_ACOS: return acos(a);
          case F_ATAN: return atan(a);
          case F_SINH: return sinh(a);
          case F_COSH: return cosh(a);
          case F_TANH: return tanh(a);
          case F_EXP: return exp(a);
          case F_LOG: return a <= 0.0 ? NAN : log(a);
          case F_SQRT: return a < 0.0 ? NAN : sqrt(a);
          case F_ABS: return fabs(a);
          case F_FLOOR: return floor(a);
          case F_CEIL: return ceil(a);
          case F_ROUND: return floor(a + 0.5);
          case F_SIGN: return a > 0 ? 1.0 : (a < 0 ? -1.0 : 0.0);
          case F_UNIT: return a >= 0.0 ? 1.0 : 0.0;
          /* The Dirac delta is zero at ordinary samples; the plot code
           * collects the zeros of its argument and draws the arrows. */
          case F_DELTA: return 0.0;
          case F_RECT: return fabs(a) <= 0.5 ? 1.0 : 0.0;
          case F_MIN: return a < b ? a : b;
          case F_MAX: return a > b ? a : b;
          case F_POW: return pow(a, b);
          case F_ATAN2: return atan2(a, b);
          }
        return NAN;
      }
    }
  return NAN;
}

/* ------------------------------------------------------------------ */
/* delta roots                                                        */
/* ------------------------------------------------------------------ */

#define MATH_DELTA_SAMPLES 900

static void delta_add_root(double *roots, int *count, int max, double root,
                           double tolerance)
{
  for (int i = 0; i < *count; i++)
    if (fabs(roots[i] - root) <= tolerance) return;
  if (*count < max) roots[(*count)++] = root;
}

static void delta_collect_node(MathNode *n, double lo, double hi, int polar,
                               double *roots, int *count, int max)
{
  if (!n || *count >= max) return;

  if (n->op == OP_FUNC && n->func == F_DELTA)
    {
      double span = hi - lo;
      if (!(span > 0.0)) return;

      (void) polar;                /* x and theta both use the identity */
      double value = lo;
      double arg = math_eval_node(n->a, value);
      double zero_start = lo;
      int in_zero = isfinite(arg) && arg == 0.0;
      double tolerance = span * 1e-6;

      for (int i = 1; i <= MATH_DELTA_SAMPLES; i++)
        {
          double next_value = lo + span * (double) i / MATH_DELTA_SAMPLES;
          double next_arg = math_eval_node(n->a, next_value);

          if (isfinite(next_arg) && next_arg == 0.0)
            {
              if (!in_zero)
                {
                  in_zero = 1;
                  zero_start = next_value;
                }
            }
          else
            {
              if (in_zero)
                {
                  delta_add_root(roots, count, max,
                                 (zero_start + value) / 2.0, tolerance);
                  in_zero = 0;
                }
              if (isfinite(arg) && isfinite(next_arg) &&
                  arg != 0.0 && next_arg != 0.0 &&
                  ((arg < 0.0 && next_arg > 0.0) ||
                   (arg > 0.0 && next_arg < 0.0)))
                {
                  double root = value +
                    (0.0 - arg) / (next_arg - arg) * (next_value - value);
                  delta_add_root(roots, count, max, root, tolerance);
                }
            }

          value = next_value;
          arg = next_arg;
        }

      if (in_zero)
        delta_add_root(roots, count, max, (zero_start + value) / 2.0,
                       tolerance);
      return;                      /* do not descend into a delta argument */
    }

  delta_collect_node(n->a, lo, hi, polar, roots, count, max);
  delta_collect_node(n->b, lo, hi, polar, roots, count, max);
}

int math_delta_roots(MathParser *m, double lo, double hi, int polar,
                     double *roots, int max)
{
  int count = 0;
  if (!m || !m->root || !roots || max <= 0 || !(hi > lo)) return 0;
  delta_collect_node(m->root, lo, hi, polar, roots, &count, max);
  return count;
}

int math_validate(const char *expr, const char **error)
{
  MathParser *p = math_parse(expr, error);
  if (!p) return 0;
  math_free(p);
  return 1;
}
