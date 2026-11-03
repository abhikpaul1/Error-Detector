#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_SRC  6000          /* max characters of source code  */
#define MAX_TOK  1500          /* max tokens                     */
#define MAX_SYM   300          /* max symbol-table entries       */
#define MAX_NEST  100          /* max bracket nesting            */

/* ----------------------------- data ------------------------------- */
typedef enum { T_KEYWORD, T_IDENT, T_NUMBER, T_STRING, T_CHAR, T_OP } TokType;
typedef struct { TokType type; char text[128]; int line; } Token;
typedef struct { char name[128]; int depth; char kind; int addr_ok; } Symbol;
/* Symbol kind: 'v' variable, 'f' function/macro, 't' struct tag/typedef,
   'h' parameter or for-loop variable.  addr_ok = array/pointer (no '&' needed). */

static Token  tok[MAX_TOK + 1];            /* +1 : empty "sentinel" token at the end */
static Symbol sym[MAX_SYM];
static int ntok, nsym, nerr, nwarn, has_stdio;

static const char *KEYWORDS[] = { "if", "else", "for", "while", "do", "switch", "case", "default",
    "break", "continue", "return", "struct", "union", "enum", "typedef", "const", "static",
    "sizeof", "goto", 0 };
static const char *TYPES[] = { "int", "float", "double", "char", "void", "long", "short",
    "unsigned", "signed", "FILE", "size_t", 0 };
static const char *LIBRARY[] = { "main", "printf", "scanf", "puts", "gets", "getchar", "putchar",
    "fgets", "fputs", "fopen", "fclose", "fprintf", "fscanf", "strlen", "strcpy", "strncpy",
    "strcmp", "strcat", "strstr", "malloc", "calloc", "free", "exit", "abs", "sqrt", "pow",
    "fabs", "ceil", "floor", "sin", "cos", "tan", "log", "exp", "rand", "srand", "time",
    "atoi", "atof", "toupper", "tolower", "isalpha", "isdigit", "NULL", "EOF",
    "stdin", "stdout", "stderr", 0 };
static const char *DIRECTIVES[] = { "include", "define", "undef", "if", "ifdef", "ifndef",
    "else", "elif", "endif", "pragma", "error", 0 };
static const char *OPS2[] = { "==", "!=", "<=", ">=", "&&", "||", "++", "--", "+=", "-=", "*=",
    "/=", "%=", "->", "<<", ">>", 0 };

/* ------------------------- small helpers -------------------------- */
static int in_list(const char *w, const char **list) {
    int i;
    for (i = 0; list[i]; i++) if (!strcmp(w, list[i])) return 1;
    return 0;
}

/* Print one problem: ERROR = code will not compile, WARNING = compiles but is probably a bug */
static void report(int is_error, int line, const char *msg, const char *fix) {
    printf("  [%s] Line %d: %s\n", is_error ? "ERROR  " : "WARNING", line, msg);
    printf("             -> Suggestion: %s\n", fix);
    if (is_error) nerr++; else nwarn++;
}

/* Edit distance (insert / delete / replace / swap neighbours) for "Did you mean ...?" */
static int edit_distance(const char *a, const char *b) {
    int la = (int)strlen(a), lb = (int)strlen(b), i, j, d[41][41];
    if (la > 40 || lb > 40) return 99;
    for (i = 0; i <= la; i++) d[i][0] = i;
    for (j = 0; j <= lb; j++) d[0][j] = j;
    for (i = 1; i <= la; i++)
        for (j = 1; j <= lb; j++) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            int m = d[i-1][j] + 1;                                   /* delete  */
            if (d[i][j-1] + 1 < m) m = d[i][j-1] + 1;                /* insert  */
            if (d[i-1][j-1] + cost < m) m = d[i-1][j-1] + cost;      /* replace */
            if (i > 1 && j > 1 && a[i-1] == b[j-2] && a[i-2] == b[j-1] && d[i-2][j-2] + 1 < m)
                m = d[i-2][j-2] + 1;                                 /* swap    */
            d[i][j] = m;
        }
    return d[la][lb];
}

static const char *best;
static int best_d;
static void consider(const char *word, const char *candidate) {
    int d = edit_distance(word, candidate);
    if (d > 0 && d < best_d) { best = candidate; best_d = d; }
}
static const char *suggest_from(const char *word, const char **list) {   /* closest word in a list */
    int i;
    best = NULL; best_d = 3;
    for (i = 0; list[i]; i++) consider(word, list[i]);
    return best;
}
static const char *suggest_name(const char *word) {      /* closest variable / keyword / library name */
    int i;
    best = NULL; best_d = (strlen(word) <= 3) ? 2 : 3;
    if (strlen(word) < 2) return NULL;
    for (i = 0; i < nsym; i++)       consider(word, sym[i].name);
    for (i = 0; KEYWORDS[i]; i++)    consider(word, KEYWORDS[i]);
    for (i = 0; TYPES[i]; i++)       consider(word, TYPES[i]);
    for (i = 0; LIBRARY[i]; i++)     consider(word, LIBRARY[i]);
    return best;
}

/* symbol table */
static int find_symbol(const char *name) {                /* newest match first */
    int i;
    for (i = nsym - 1; i >= 0; i--) if (!strcmp(sym[i].name, name)) return i;
    return -1;
}
static void add_symbol(const char *name, int depth, char kind, int addr_ok) {
    if (nsym >= MAX_SYM) return;
    snprintf(sym[nsym].name, sizeof sym[nsym].name, "%s", name);
    sym[nsym].depth = depth; sym[nsym].kind = kind; sym[nsym].addr_ok = addr_ok;
    nsym++;
}

/* token list */
static void add_token(TokType type, const char *text, int line) {
    if (ntok >= MAX_TOK) return;
    tok[ntok].type = type;
    snprintf(tok[ntok].text, sizeof tok[ntok].text, "%s", text);
    tok[ntok].line = line;
    ntok++;
}
static TokType type_at(int i)    { return (i >= 0 && i <= ntok) ? tok[i].type : T_OP; }
static const char *text_at(int i){ return (i >= 0 && i <= ntok) ? tok[i].text : ""; }
static int is_text(int i, const char *s) { return strcmp(text_at(i), s) == 0; }

static void take(char *dst, size_t cap, const char *src, int from, int to) {   /* copy src[from..to) */
    size_t n = (size_t)(to - from);
    if (n > cap - 1) n = cap - 1;
    memcpy(dst, src + from, n);
    dst[n] = 0;
}

/* ===================== PHASE 1 : LEXICAL ANALYSIS ===================== */
static void preprocessor(const char *src, int *pi, int line) {      /* handles one #... line */
    char dir[24] = "", rest[128] = "", name[64], msg[320], fix[320];
    int i = *pi + 1, k = 0;
    while (src[i] == ' ' || src[i] == '\t') i++;
    while (isalpha((unsigned char)src[i]) && k < 23) dir[k++] = src[i++];
    dir[k] = 0;
    for (k = 0; src[i] && src[i] != '\n'; i++) if (k < 127) rest[k++] = src[i];
    rest[k] = 0;
    *pi = i;
    if (!in_list(dir, DIRECTIVES)) {
        const char *s = suggest_from(dir, DIRECTIVES);
        snprintf(msg, sizeof msg, "Unknown preprocessor directive '#%s'", dir);
        if (s) snprintf(fix, sizeof fix, "Did you mean '#%s'?", s);
        else   snprintf(fix, sizeof fix, "Use a valid directive such as #include or #define");
        report(1, line, msg, fix);
    }
    if (!strcmp(dir, "include") && strstr(rest, "stdio.h")) has_stdio = 1;
    if (!strcmp(dir, "define") && sscanf(rest, " %63[A-Za-z0-9_]", name) == 1)
        add_symbol(name, 0, 'f', 1);               /* macro names count as declared */
}

static void lexical_analysis(const char *src) {
    int i = 0, line = 1, start, k;
    char buf[128], msg[320], fix[320];
    ntok = 0;
    while (src[i]) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\n') { line++; i++; }
        else if (isspace(c)) i++;
        else if (c == '#') preprocessor(src, &i, line);
        else if (c == '/' && src[i+1] == '/') { while (src[i] && src[i] != '\n') i++; }
        else if (c == '/' && src[i+1] == '*') {                       /* block comment */
            start = line; i += 2;
            while (src[i] && !(src[i] == '*' && src[i+1] == '/')) { if (src[i] == '\n') line++; i++; }
            if (src[i]) i += 2;
            else report(1, start, "Unterminated comment: '/*' has no closing '*/'",
                        "Close the comment with '*/'");
        }
        else if (isalpha(c) || c == '_') {                            /* keyword or identifier */
            start = i;
            while (isalnum((unsigned char)src[i]) || src[i] == '_') i++;
            take(buf, sizeof buf, src, start, i);
            add_token((in_list(buf, KEYWORDS) || in_list(buf, TYPES)) ? T_KEYWORD : T_IDENT, buf, line);
        }
        else if (isdigit(c)) {                                        /* number (or a bad name like 2abc) */
            int dots = 0, bad = 0;
            start = i;
            while (isalnum((unsigned char)src[i]) || src[i] == '.') { if (src[i] == '.') dots++; i++; }
            take(buf, sizeof buf, src, start, i);
            if (!(buf[1] == 'x' || buf[1] == 'X'))                    /* hex numbers may contain letters */
                for (k = 0; buf[k]; k++)
                    if (isalpha((unsigned char)buf[k]) && !strchr("eEfFlLuU", buf[k])) bad = 1;
            if (bad) {
                snprintf(msg, sizeof msg, "Invalid name '%s': a name cannot start with a digit", buf);
                snprintf(fix, sizeof fix, "Start it with a letter or '_' (for example '_%s')", buf);
                report(1, line, msg, fix);
            } else if (dots > 1) {
                snprintf(msg, sizeof msg, "Malformed number '%s'", buf);
                report(1, line, msg, "A number can contain only one decimal point");
            }
            add_token(T_NUMBER, buf, line);
        }
        else if (c == '"' || c == '\'') {                             /* string or char literal */
            char q = (char)c;
            int closed = 0;
            start = i++;
            while (src[i] && src[i] != '\n') {
                if (src[i] == '\\' && src[i+1] && src[i+1] != '\n') i++;      /* skip escaped char */
                else if (src[i] == q) { closed = 1; i++; break; }
                i++;
            }
            if (closed) {
                take(buf, sizeof buf, src, start, i);
                add_token(q == '"' ? T_STRING : T_CHAR, buf, line);
            } else if (q == '"') {
                report(1, line, "Unterminated string literal: closing double quote (\") is missing",
                       "Add the closing \" where the string should end (before any ',' ')' or ';')");
            } else {
                report(1, line, "Unterminated character literal: closing single quote (') is missing",
                       "Add the closing ' (a character literal looks like 'A')");
            }
        }
        else if (c >= 128) {                                          /* e.g. smart quotes pasted from Word */
            report(1, line, "Non-ASCII character found (often a 'smart quote' copied from a document)",
                   "Retype it with a normal keyboard character such as \" or '");
            while (src[i] && (unsigned char)src[i] >= 128) i++;
        }
        else if (strchr("(){}[];,+-*/%=<>!&|^~?:.", c)) {             /* operators and punctuation */
            char pair[3] = { (char)c, src[i+1], 0 };
            int len = in_list(pair, OPS2) ? 2 : 1;
            take(buf, sizeof buf, src, i, i + len);
            i += len;
            add_token(T_OP, buf, line);
        }
        else {
            snprintf(msg, sizeof msg, "Invalid character '%c'", c);
            report(1, line, msg, "Remove it - this character is not valid in C code");
            i++;
        }
    }
    tok[ntok].type = T_OP; tok[ntok].text[0] = 0; tok[ntok].line = line;   /* sentinel */
}

/* ===================== PHASE 2 : SYNTAX ANALYSIS ===================== */
static char closer(char open) { return open == '(' ? ')' : open == '[' ? ']' : '}'; }

static void syntax_analysis(void) {
    char stk[MAX_NEST], msg[320], fix[320];
    int  open_at[MAX_NEST], is_init[MAX_NEST];       /* token index of each open bracket / is it an initializer list */
    int  top = 0, pd = 0, i, j, semis;               /* pd = how many '(' are currently open */

    for (i = 0; i < ntok; i++) {
        const char *t = tok[i].text;
        char c = (tok[i].type == T_OP && t[1] == 0) ? t[0] : 0;
        int ctrl_close = 0;                          /* is this ')' the end of an if/for/while/switch header? */

        if (c == '(' || c == '[' || c == '{') {                          /* opening bracket: push */
            if (top < MAX_NEST) {
                stk[top] = c; open_at[top] = i;
                is_init[top] = (c == '{') && (is_text(i-1, "=") || is_text(i-1, "enum") || is_text(i-2, "enum")
                               || (top > 0 && is_init[top-1] && (is_text(i-1, "{") || is_text(i-1, ","))));
                top++;
            }
            if (c == '(') pd++;
        }
        else if (c == ')' || c == ']' || c == '}') {                     /* closing bracket: match it */
            char want = (c == ')') ? '(' : (c == ']') ? '[' : '{';
            int k = top - 1;
            while (k >= 0 && stk[k] != want) k--;
            if (k < 0) {
                snprintf(msg, sizeof msg, "Unexpected '%c' - there is no matching '%c'", c, want);
                snprintf(fix, sizeof fix, "Remove this '%c', or add the missing '%c' before it", c, want);
                report(1, tok[i].line, msg, fix);
            } else {
                while (top - 1 > k) {                                    /* opened later but never closed */
                    char o = stk[top-1];
                    snprintf(msg, sizeof msg, "Missing '%c' to close the '%c' opened here", closer(o), o);
                    snprintf(fix, sizeof fix, "Add '%c' (it must come before the '%c' on line %d)",
                             closer(o), c, tok[i].line);
                    report(1, tok[open_at[top-1]].line, msg, fix);
                    if (o == '(') pd--;
                    top--;
                }
                top--;                                                   /* pop the matching bracket */
                if (c == ')') {
                    int o = open_at[top];                                /* index of the matching '(' */
                    pd--;
                    if (is_text(o-1, "if") || is_text(o-1, "while") || is_text(o-1, "for") || is_text(o-1, "switch")) {
                        ctrl_close = 1;
                        if (is_text(o-1, "for")) {                       /* for(init; cond; update) needs 2 ';' */
                            for (j = o + 1, semis = 0; j < i; j++) if (is_text(j, ";")) semis++;
                            if (semis != 2) {
                                snprintf(msg, sizeof msg, "for-loop header has %d ';' but needs exactly 2", semis);
                                report(1, tok[i].line, msg,
                                       "Write it as: for (init; condition; update) - separate the parts with ';' not ','");
                            }
                        }
                        if ((is_text(o-1, "if") || is_text(o-1, "for")) && is_text(i+1, ";"))
                            report(0, tok[i].line, "Empty statement: a ';' right after the ')' ends the statement",
                                   "Remove the ';' after the ')' so the body below is controlled by it");
                    }
                }
            }
        }
        else if (!strcmp(t, "=") && top > 0 && stk[top-1] == '(' &&
                 (is_text(open_at[top-1] - 1, "if") || is_text(open_at[top-1] - 1, "while"))) {
            report(0, tok[i].line, "Assignment '=' used inside a condition",
                   "Did you mean '==' (compare)? Use '=' only to store a value");
        }

        /* missing ';' : the line ends like a statement and the next token starts a new one */
        if (i + 1 < ntok && tok[i+1].line > tok[i].line && pd == 0 && !ctrl_close &&
            !(top > 0 && stk[top-1] == '{' && is_init[top-1])) {
            TokType a = tok[i].type, b = tok[i+1].type;
            int ends   = a == T_IDENT || a == T_NUMBER || a == T_STRING || a == T_CHAR || c == ')' || c == ']'
                         || !strcmp(t, "++") || !strcmp(t, "--") || !strcmp(t, "break") || !strcmp(t, "continue");
            int starts = b == T_IDENT || b == T_KEYWORD || b == T_NUMBER || b == T_STRING || b == T_CHAR
                         || is_text(i+1, "}") || is_text(i+1, "++") || is_text(i+1, "--");
            if (ends && starts) {
                snprintf(msg, sizeof msg, "Missing ';' at the end of the statement");
                snprintf(fix, sizeof fix, "Add ';' after '%s'", t);
                report(1, tok[i].line, msg, fix);
            }
        }
    }
    while (top > 0) {                                                    /* brackets still open at the end */
        char o = stk[--top];
        snprintf(msg, sizeof msg, "Missing '%c' to close the '%c' opened here", closer(o), o);
        snprintf(fix, sizeof fix, "Add '%c' to close it", closer(o));
        report(1, tok[open_at[top]].line, msg, fix);
    }
}

/* ==================== PHASE 3 : SEMANTIC ANALYSIS ===================== */
static void check_format(int i) {          /* tok[i] is printf/scanf and tok[i+2] is its format string */
    const char *f = tok[i + 2].text;
    int specs = 0, args = 0, depth = 0, j;
    char msg[320];
    for (j = 0; f[j]; j++)
        if (f[j] == '%') { if (f[j+1] == '%') j++; else specs++; }       /* count %d %f %s ... */
    for (j = i + 3; j < ntok && !is_text(j, ";"); j++) {                  /* count the arguments */
        if (is_text(j, "(")) depth++;
        else if (is_text(j, ")")) { if (depth == 0) break; depth--; }
        else if (depth == 0 && is_text(j, ",")) args++;
    }
    if (specs != args) {
        snprintf(msg, sizeof msg, "%s has %d format specifier(s) but %d argument(s)", tok[i].text, specs, args);
        report(0, tok[i].line, msg, specs > args
               ? "Give one argument for every % in the format string"
               : "Add a matching specifier (like %d) or remove the extra argument(s)");
    }
}

static void semantic_analysis(void) {
    int i, k, bd = 0, pd = 0;                        /* bd = brace depth (scope), pd = parenthesis depth */
    int in_decl = 0, decl_pd = 0, decl_bd = 0, expect_decl = 0;     /* for lists like  int a, b, c;  */
    int in_typedef = 0, typedef_bd = 0, scanf_pd = 0, warned_stdio = 0;
    char last_name[128] = "", msg[320], fix[320];

    for (i = 0; i < ntok; i++) {
        const char *t = tok[i].text, *next = text_at(i + 1);
        int line = tok[i].line;

        if (tok[i].type == T_OP) {
            if (!strcmp(t, "{")) { bd++; pd = scanf_pd = 0; }             /* a '{' never sits inside '(' : recover */
            else if (!strcmp(t, "}")) {                                   /* leave a scope: forget its names */
                if (bd > 0) bd--;
                pd = scanf_pd = 0;
                while (nsym > 0 && sym[nsym-1].depth > bd) nsym--;
            }
            else if (!strcmp(t, "(")) pd++;
            else if (!strcmp(t, ")")) {
                if (pd > 0) pd--;
                if (pd < scanf_pd) scanf_pd = 0;
                if (pd < decl_pd) in_decl = 0;
            }
            else if (!strcmp(t, ";") && pd == 0) {
                in_decl = expect_decl = 0;
                while (nsym > 0 && sym[nsym-1].kind == 'h' && sym[nsym-1].depth > bd) nsym--;
                if (in_typedef && bd == typedef_bd) {                     /* typedef ... Name;  */
                    if (last_name[0]) add_symbol(last_name, bd, 't', 1);
                    in_typedef = 0;
                }
            }
            else if (!strcmp(t, ",") && in_decl && pd == decl_pd && bd == decl_bd) expect_decl = 1;
            else if ((!strcmp(t, "/") || !strcmp(t, "%")) && type_at(i+1) == T_NUMBER
                     && strspn(next, "0.") == strlen(next))
                report(0, line, "Division by zero", "Use a non-zero divisor, or test the divisor with an if before dividing");
            continue;
        }
        if (tok[i].type == T_KEYWORD) {
            if (!strcmp(t, "typedef")) { in_typedef = 1; typedef_bd = bd; last_name[0] = 0; }
            if (in_list(t, TYPES) && (type_at(i+1) == T_IDENT || type_at(i+1) == T_KEYWORD || is_text(i+1, "*"))) {
                in_decl = 1; decl_pd = pd; decl_bd = bd;                  /* a declaration starts here */
            }
            continue;
        }
        if (tok[i].type != T_IDENT) continue;                            /* numbers, strings, chars: nothing to check */

        /* ------------------------ an identifier ------------------------ */
        if (is_text(i-1, ".") || is_text(i-1, "->")) continue;           /* struct member name */
        if (is_text(i-1, "struct") || is_text(i-1, "union") || is_text(i-1, "enum")) {   /* tag name */
            if (find_symbol(t) < 0) add_symbol(t, bd, 't', 1);
            continue;
        }
        if (in_typedef && bd == typedef_bd) { snprintf(last_name, sizeof last_name, "%s", t); continue; }

        {
            int p = i - 1, is_decl;
            while (p >= 0 && is_text(p, "*")) p--;                       /* skip pointer stars:  int *p */
            is_decl = expect_decl || (p >= 0 && (in_list(text_at(p), TYPES) ||
                      ((k = find_symbol(text_at(p))) >= 0 && sym[k].kind == 't')));

            if (is_decl) {                                               /* --- a declaration --- */
                char kind = is_text(i+1, "(") ? 'f' : (pd > 0 ? 'h' : 'v');
                int dup = 0;
                if (kind == 'v')
                    for (k = 0; k < nsym; k++)
                        if (sym[k].kind == 'v' && sym[k].depth == bd && !strcmp(sym[k].name, t)) dup = 1;
                expect_decl = 0;
                if (dup) {
                    snprintf(msg, sizeof msg, "Redeclaration of '%s' in the same scope", t);
                    snprintf(fix, sizeof fix, "Remove the type to reuse it ('%s = ...;') or pick a different name", t);
                    report(1, line, msg, fix);
                } else
                    add_symbol(t, kind == 'h' ? bd + 1 : bd, kind, is_text(i+1, "[") || p != i - 1);
                continue;
            }
        }

        /* --------------------- use of an identifier --------------------- */
        k = find_symbol(t);
        if (k < 0 && !in_list(t, LIBRARY)) {                             /* not declared anywhere */
            const char *s = suggest_name(t);
            int kw = s && (in_list(s, KEYWORDS) || in_list(s, TYPES));
            if (kw || type_at(i+1) == T_IDENT) {                         /* "itn x", "retrun y", "whlie (...)" */
                if (kw) {
                    snprintf(msg, sizeof msg, "Misspelled keyword '%s'", t);
                    snprintf(fix, sizeof fix, "Did you mean '%s'?", s);
                } else {
                    snprintf(msg, sizeof msg, "Unknown type or keyword '%s' (followed by '%s')", t, next);
                    snprintf(fix, sizeof fix, "Check the spelling of '%s' - it is not a known type or keyword", t);
                }
                report(1, line, msg, fix);
                if (type_at(i+1) == T_IDENT && find_symbol(next) < 0) {   /* treat the next name as declared */
                    in_decl = 1; decl_pd = pd; decl_bd = bd; expect_decl = 1;
                }
            } else {
                int call = is_text(i+1, "(");
                if (call) snprintf(msg, sizeof msg, "Call to undeclared function '%s'", t);
                else      snprintf(msg, sizeof msg, "Undeclared identifier '%s'", t);
                if (s)         snprintf(fix, sizeof fix, "Did you mean '%s'?", s);
                else if (call) snprintf(fix, sizeof fix, "Define '%s' (or add a prototype for it) above main()", t);
                else           snprintf(fix, sizeof fix, "Declare it before use, for example:  int %s;", t);
                report(1, line, msg, fix);
            }
            continue;
        }

        if (!strcmp(t, "printf") || !strcmp(t, "scanf")) {
            if (!has_stdio && !warned_stdio) {
                warned_stdio = 1;
                snprintf(msg, sizeof msg, "'%s' is used but <stdio.h> is not included", t);
                report(0, line, msg, "Add  #include <stdio.h>  at the top of the program");
            }
            if (is_text(i+1, "(") && type_at(i+2) == T_STRING) check_format(i);
            if (!strcmp(t, "scanf")) scanf_pd = pd + 1;
        }
        else if (scanf_pd && pd == scanf_pd && k >= 0 && (sym[k].kind == 'v' || sym[k].kind == 'h')
                 && !sym[k].addr_ok && is_text(i-1, ",") && (is_text(i+1, ",") || is_text(i+1, ")"))) {
            snprintf(msg, sizeof msg, "scanf needs the address of '%s' but the '&' is missing", t);
            snprintf(fix, sizeof fix, "Write &%s instead of %s", t, t);
            report(0, line, msg, fix);
        }
    }
}

/* ============================ SAMPLES ============================== */
static const char SAMPLE1[] =                 /* correct program */
    "#include <stdio.h>\n"
    "#define LIMIT 5\n"
    "\n"
    "int square(int n) {\n"
    "    return n * n;\n"
    "}\n"
    "\n"
    "int main() {\n"
    "    int i, sum = 0;\n"
    "    for (i = 1; i <= LIMIT; i++) {\n"
    "        sum = sum + square(i);\n"
    "    }\n"
    "    printf(\"Sum of squares = %d\\n\", sum);\n"
    "    return 0;\n"
    "}\n";

static const char SAMPLE2[] =                 /* lexical errors */
    "#inlcude <stdio.h>\n"
    "int main() {\n"
    "    int 2ndNum = 10;\n"
    "    int price = 5 @ 2;\n"
    "    char grade = 'A;\n"
    "    return 0;\n"
    "}\n"
    "/* end of program\n";

static const char SAMPLE3[] =                 /* syntax errors */
    "#include <stdio.h>\n"
    "int main() {\n"
    "    int a = 10, b = 3\n"
    "    if (a = b) {\n"
    "        printf(\"Equal\\n\");\n"
    "    }\n"
    "    for (int i = 0, i < 3, i++) {\n"
    "        printf(\"%d\\n\", i);\n"
    "    }\n"
    "    printf(\"Done\\n\"\n"
    "    return 0;\n"
    "}\n";

static const char SAMPLE4[] =                 /* semantic errors */
    "int main() {\n"
    "    int count = 5, total;\n"
    "    float avg;\n"
    "    int count = 7;\n"
    "    scanf(\"%d\", total);\n"
    "    totl = count * 2;\n"
    "    prinf(\"Total is %d\\n\", totl);\n"
    "    avg = total / 0;\n"
    "    printf(\"Average: %f\\n\", avg, count);\n"
    "    return 0;\n"
    "}\n";

static const char *SAMPLES[] = { SAMPLE1, SAMPLE2, SAMPLE3, SAMPLE4 };

/* ========================= DRIVER / MENU =========================== */
static void show_source(const char *src) {                /* print code with line numbers */
    int i, line = 1;
    printf("%3d | ", line);
    for (i = 0; src[i]; i++) {
        putchar(src[i]);
        if (src[i] == '\n' && src[i+1]) printf("%3d | ", ++line);
    }
    if (i > 0 && src[i-1] != '\n') putchar('\n');
}

static void analyze(const char *src) {
    int before;
    nerr = nwarn = nsym = ntok = has_stdio = 0;

    printf("\n----- SOURCE CODE -----\n");
    show_source(src);

    printf("\n----- PHASE 1: LEXICAL ANALYSIS -----\n");
    before = nerr + nwarn;
    lexical_analysis(src);
    if (nerr + nwarn == before) printf("  OK - %d tokens found, no lexical problems.\n", ntok);

    printf("\n----- PHASE 2: SYNTAX ANALYSIS -----\n");
    before = nerr + nwarn;
    syntax_analysis();
    if (nerr + nwarn == before) printf("  OK - no syntax problems found.\n");

    printf("\n----- PHASE 3: SEMANTIC ANALYSIS -----\n");
    before = nerr + nwarn;
    semantic_analysis();
    if (nerr + nwarn == before) printf("  OK - no semantic problems found.\n");

    printf("\n===== RESULT: %d error(s), %d warning(s) =====\n", nerr, nwarn);
    if (nerr + nwarn == 0) printf("The program looks correct.\n");
    else printf("Tip: fix the first error first - later messages can be side-effects of earlier ones.\n");
}

static void read_program(char *buf) {                     /* read lines until a line that says END */
    char line[256], trimmed[256];
    int n = 0;
    printf("Type or paste your C program. Finish with a line containing only END\n");
    while (fgets(line, sizeof line, stdin)) {
        strcpy(trimmed, line);
        trimmed[strcspn(trimmed, "\r\n")] = 0;
        if (!strcmp(trimmed, "END")) break;
        if (n + (int)strlen(line) >= MAX_SRC) { printf("(program too long - the rest is ignored)\n"); break; }
        strcpy(buf + n, line);
        n += (int)strlen(line);
    }
    buf[n] = 0;
}

int main(void) {
    static char src[MAX_SRC];
    char in[32];
    int choice;
    for (;;) {
        printf("\n==== C ERROR DETECTOR & CORRECTION SUGGESTER ====\n"
               "  1. Sample 1 - correct program\n"
               "  2. Sample 2 - lexical errors\n"
               "  3. Sample 3 - syntax errors\n"
               "  4. Sample 4 - semantic errors\n"
               "  5. Check my own program\n"
               "  0. Exit\n"
               "Enter choice: ");
        if (!fgets(in, sizeof in, stdin) || in[0] == '0') break;
        choice = atoi(in);
        if (choice >= 1 && choice <= 4) analyze(SAMPLES[choice - 1]);
        else if (choice == 5) { read_program(src); analyze(src); }
        else printf("Please enter a number from 0 to 5.\n");
    }
    printf("Thank you!\n");
    return 0;
}
