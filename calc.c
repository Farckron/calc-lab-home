// Serhii Voitov 251ADC006
// https://github.com/Farckron/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: AI (Claude Code, Opus 5.5); course slides "Files, Arguments, Directories" (argv loop, opendir, mkdir)

// AI (Claude Code): prompt "Implement the arithmetic parser lab in C from the
//   task description. Keep the code clean, short, simple and easy to
//   understand." It also got the task text and the sample tests. The whole
//   file was written in that session, one grade level per commit (see the git
//   history). The grammar is the one from the hint in the task. The comments
//   explain why each part works the way it does.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>

#define FULL_ID "serhii_voitov_251ADC006"   // used in output file names
#define SHORT_ID "serhii_251ADC006"         // used in the default output folder name

enum { NUM, PLUS, MINUS, STAR, SLASH, POW, LPAREN, RPAREN, END, BAD };

// A number from the file or a result. Integers and floats are kept apart
// because int / int must truncate (7 / 2 is 3) while 7 / 2.0 is 3.5.
typedef struct {
    int is_float;   // 1: the value is in f, 0: the value is in i
    long long i;
    double f;
} Value;

typedef struct {
    int type;
    size_t start;   // index of the first character of the token in the file
    Value val;      // only used when type is NUM
} Token;

// The whole file is in memory, so a position is just an index into it.
// Positions in error messages are 1-based, so they are always index + 1.
static const char *src;
static size_t len;
static size_t pos;      // where the tokenizer continues reading
static Token tok;       // the token the parser is looking at right now
static size_t err;      // 1-based position of the first error, 0 means no error

static void fail(size_t index) {
    if (!err) err = index + 1;
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }

// Reads the next token into tok. A line ends at '\n' or at the end of the
// file, and both become END, so "missing operand at end of line" points
// at the '\n' (or at file length + 1 when there is no final '\n').
static void next(void) {
    while (pos < len && (src[pos] == ' ' || src[pos] == '\t')) pos++;
    tok.start = pos;
    if (pos >= len || src[pos] == '\n') { tok.type = END; return; }

    char c = src[pos];
    if (is_digit(c)) {
        // A number is digits, optionally followed by '.' and more digits.
        // If no digit follows the '.', the number stops before it and the
        // '.' becomes a BAD token, so "5." and ".5" fail at the '.', and
        // "1.5e3" fails at the 'e', exactly where the task wants it.
        tok.type = NUM;
        while (pos < len && is_digit(src[pos])) pos++;
        int is_float = pos + 1 < len && src[pos] == '.' && is_digit(src[pos + 1]);
        if (is_float) {
            pos++;
            while (pos < len && is_digit(src[pos])) pos++;
        }
        // strtoll/strtod stop at the first character that is not part of
        // a number, and the buffer ends with '\0', so they cannot run past it.
        tok.val.is_float = is_float;
        if (is_float) tok.val.f = strtod(src + tok.start, NULL);
        else tok.val.i = strtoll(src + tok.start, NULL, 10);
        return;
    }
    pos++;
    if (c == '+') tok.type = PLUS;
    else if (c == '-') tok.type = MINUS;
    // "**" is one token only when the two stars touch, so "2 * * 3" is two
    // operators in a row and fails at the second '*'.
    else if (c == '*' && pos < len && src[pos] == '*') { tok.type = POW; pos++; }
    else if (c == '*') tok.type = STAR;
    else if (c == '/') tok.type = SLASH;
    else if (c == '(') tok.type = LPAREN;
    else if (c == ')') tok.type = RPAREN;
    else tok.type = BAD;
}

// Integer power by repeated squaring, so even 1 ** 1000000000 takes about
// 30 steps. b is squared only while bits of e are left, so it never grows
// past the final result (the tests promise that result fits).
static long long ipow(long long b, long long e) {
    long long r = 1;    // also makes x ** 0 == 1, including 0 ** 0
    while (e > 0) {
        if (e & 1) r *= b;
        e >>= 1;
        if (e > 0) b *= b;
    }
    return r;
}

// Applies one operator to a and b. If both are integers the math stays in
// long long. If at least one is a float, both are turned into double first.
// op_at is where the operator starts, rhs_at where the right operand starts:
// dividing by zero and a negative integer exponent are the right operand's
// fault, but an infinite or NaN result only exists because of the operator.
static Value apply(int op, Value a, Value b, size_t op_at, size_t rhs_at) {
    Value r = {0, 0, 0};
    if (!a.is_float && !b.is_float) {
        if (op == PLUS) r.i = a.i + b.i;
        else if (op == MINUS) r.i = a.i - b.i;
        else if (op == STAR) r.i = a.i * b.i;
        else if (op == SLASH) {
            if (b.i == 0) fail(rhs_at);
            else r.i = a.i / b.i;   // C already truncates toward zero
        } else {
            if (b.i < 0) fail(rhs_at);
            else r.i = ipow(a.i, b.i);
        }
        return r;
    }
    double x = a.is_float ? a.f : (double)a.i;
    double y = b.is_float ? b.f : (double)b.i;
    r.is_float = 1;
    if (op == PLUS) r.f = x + y;
    else if (op == MINUS) r.f = x - y;
    else if (op == STAR) r.f = x * y;
    else if (op == SLASH) {
        if (y == 0) { fail(rhs_at); return r; }
        r.f = x / y;
    } else r.f = pow(x, y);
    // For example 10.0 ** 400 is inf and (0 - 8) ** 0.5 is NaN.
    if (!isfinite(r.f)) fail(op_at);
    return r;
}

static Value expr(void);

// primary := NUMBER | '(' expr ')'
// There are no unary operators, so a number or '(' must come here. Anything
// else (an operator, ')', a bad character, the end of the line) is reported
// at the start of that token, because that is where the parser got stuck.
// A missing ')' is found the same way: after the inner expr the parser
// expects ')', sees END instead, and reports the '\n' position.
static Value primary(void) {
    Value v = tok.val;
    if (tok.type == NUM) {
        next();
        return v;
    }
    if (tok.type != LPAREN) { fail(tok.start); return v; }
    next();
    v = expr();
    if (err) return v;
    if (tok.type != RPAREN) { fail(tok.start); return v; }
    next();
    return v;
}

// One operand of a ** chain, with the positions its errors are reported at.
typedef struct {
    Value v;
    size_t op_at;   // the ** in front of this operand
    size_t rhs_at;  // where this operand starts
} PowItem;

static PowItem *chain;  // operands of the ** chains being parsed right now
static size_t chain_len, chain_cap;

// power := primary [ '**' power ]
// ** is right-associative: 2 ** 3 ** 2 is 2 ** (3 ** 2) = 512. Calling
// power() again for the exponent would give that grouping, but a line can
// hold a chain of 100000 **, and one C call per ** overflows the call stack.
// So the operands are first collected in the chain array and then combined
// starting from the right end, which gives the same grouping with a loop.
// A primary can contain parentheses with their own chain. That chain is
// added above ours and removed again before we continue, so remembering
// where ours starts (first) is enough.
static Value power(void) {
    size_t first = chain_len;
    size_t op_at = 0, rhs_at = tok.start;
    for (;;) {
        Value v = primary();
        if (err) { chain_len = first; return v; }
        if (chain_len == chain_cap) {
            chain_cap = chain_cap ? chain_cap * 2 : 64;
            chain = realloc(chain, chain_cap * sizeof *chain);
            if (!chain) { fprintf(stderr, "calc: out of memory\n"); exit(1); }
        }
        chain[chain_len++] = (PowItem){v, op_at, rhs_at};
        if (tok.type != POW) break;
        op_at = tok.start;
        next();
        rhs_at = tok.start;
    }
    // r is everything to the right of chain[i - 1], so it is the exponent
    // of the ** in front of chain[i].
    Value r = chain[chain_len - 1].v;
    for (size_t i = chain_len - 1; i > first && !err; i--)
        r = apply(POW, chain[i - 1].v, r, chain[i].op_at, chain[i].rhs_at);
    chain_len = first;
    return r;
}

// Precedence comes from the call chain: expr() only sees results that term()
// has already finished, and term() only sees finished power() results, so
// ** binds tighter than * and /, which bind tighter than + and -. The while
// loops apply operators of the same level from left to right, which makes
// 8 - 3 - 2 mean (8 - 3) - 2.

// term := power { ('*' | '/') power }
static Value term(void) {
    Value v = power();
    while (!err && (tok.type == STAR || tok.type == SLASH)) {
        int op = tok.type;
        size_t op_at = tok.start;
        next();
        size_t rhs_at = tok.start;
        Value r = power();
        if (err) break;
        v = apply(op, v, r, op_at, rhs_at);
    }
    return v;
}

// expr := term { ('+' | '-') term }
static Value expr(void) {
    Value v = term();
    while (!err && (tok.type == PLUS || tok.type == MINUS)) {
        int op = tok.type;
        size_t op_at = tok.start;
        next();
        size_t rhs_at = tok.start;
        Value r = term();
        if (err) break;
        v = apply(op, v, r, op_at, rhs_at);
    }
    return v;
}

// Evaluates every line and writes one result per line to out.
// Returns 0, or the 1-based position of the first error.
static size_t eval_all(FILE *out) {
    pos = 0;
    err = 0;
    while (pos < len && !err) {
        next();
        if (tok.type == BAD && src[tok.start] == '#') {
            // '#' as the first thing on a line makes it a comment. Skipping
            // still moves pos through the comment, so later positions keep
            // counting its characters. A '#' anywhere else stays BAD.
            while (pos < len && src[pos] != '\n') pos++;
            tok.start = pos;
        } else if (tok.type != END) {
            Value v = expr();
            // A full expression must end the line; whatever is left is an error.
            if (!err && tok.type != END) fail(tok.start);
            if (err) break;
            if (v.is_float) fprintf(out, "%.15g\n", v.f);
            else fprintf(out, "%lld\n", v.i);
        }
        pos = tok.start + 1;    // step over the '\n'
    }
    return err;
}

// Reads the whole file with one fread into one buffer of the exact size,
// plus one '\0' at the end so strtoll/strtod always stop inside the buffer.
// The parser then walks over this buffer with an index, so even a file of
// several megabytes is read once and never copied or reallocated again.
static char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *buf = n < 0 ? NULL : malloc(n + 1);
    if (buf) {
        *size = fread(buf, 1, n, f);
        buf[*size] = '\0';
    }
    fclose(f);
    return buf;
}

// Writes the last part of path into name: "labs/task1.txt" gives "task1"
// (with strip_txt) and "../labs/" gives "labs".
static void base_name(const char *path, int strip_txt, char *name, size_t size) {
    size_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/') end--;
    size_t start = end;
    while (start > 0 && path[start - 1] != '/') start--;
    size_t n = end - start;
    if (strip_txt && n > 4 && strncmp(path + end - 4, ".txt", 4) == 0) n -= 4;
    snprintf(name, size, "%.*s", (int)n, path + start);
}

// Evaluates one input file into <outdir>/<base>_<name>_<lastname>_<id>.txt.
// Returns 0 on success and 1 if the input or the output file fails.
static int run_file(const char *input, const char *outdir) {
    char *buf = read_file(input, &len);
    if (!buf) { perror(input); return 1; }
    src = buf;

    char name[1024], path[4096];
    base_name(input, 1, name, sizeof name);
    snprintf(path, sizeof path, "%s/%s_%s.txt", outdir, name, FULL_ID);
    FILE *out = fopen(path, "w");
    if (!out) { perror(path); free(buf); return 1; }

    size_t e = eval_all(out);
    free(chain);
    chain = NULL;
    chain_cap = 0;
    // Only the first error may be in the file, so throw away the results
    // written so far by opening the file again for writing (this empties it).
    if (e) {
        out = freopen(path, "w", out);
        if (out) fprintf(out, "ERROR:%zu\n", e);
    }
    // fclose() flushes the last buffered results, so a full disk shows up here.
    int failed = !out || fclose(out) != 0;
    if (failed) fprintf(stderr, "calc: could not write %s\n", path);
    free(buf);
    return failed;
}

static void usage(void) {
    fprintf(stderr, "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n");
}

int main(int argc, char *argv[]) {
    const char *dir = NULL, *outdir = NULL, *input = NULL;
    // Same strcmp() loop as in the lecture on command-line arguments.
    // An option without its value falls through to the "-" check below.
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((!strcmp(a, "-d") || !strcmp(a, "--dir")) && i + 1 < argc) dir = argv[++i];
        else if ((!strcmp(a, "-o") || !strcmp(a, "--output-dir")) && i + 1 < argc) outdir = argv[++i];
        else if (a[0] == '-' || input) {
            fprintf(stderr, "calc: unexpected argument '%s'\n", a);
            usage();
            return 1;
        } else input = a;
    }
    // Exactly one of -d DIR or input.txt must be given.
    if (!dir == !input) { usage(); return 1; }

    // Check the input before creating anything, so a typo in the path does
    // not leave an empty output folder behind.
    const char *target = dir ? dir : input;
    struct stat st;
    if (stat(target, &st) != 0) { perror(target); return 1; }
    if (dir ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)) {
        fprintf(stderr, "calc: %s is not a %s\n", target, dir ? "folder" : "file");
        return 1;
    }

    // Default output folder: <input_base>_<name>_<id> in the current folder.
    char default_out[1100];
    if (!outdir) {
        char name[1024];
        base_name(target, !dir, name, sizeof name);
        snprintf(default_out, sizeof default_out, "%s_%s", name, SHORT_ID);
        outdir = default_out;
    }
    // The output folder may already exist, which is fine.
    if (mkdir(outdir, 0755) != 0 && errno != EEXIST) { perror(outdir); return 1; }

    if (!dir) return run_file(input, outdir);

    // -d: every *.txt directly inside DIR. readdir() does not go into
    // subfolders, and stat() skips a folder that happens to be named x.txt.
    DIR *d = opendir(dir);
    if (!d) { perror(dir); return 1; }
    int failed = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t n = strlen(ent->d_name);
        if (n <= 4 || strcmp(ent->d_name + n - 4, ".txt") != 0) continue;
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", dir, ent->d_name);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) failed |= run_file(path, outdir);
    }
    closedir(d);
    return failed;
}
