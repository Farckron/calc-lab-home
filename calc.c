// Serhii Voitov 251ADC006
// https://github.com/Farckron/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: course slides "Files, Arguments, Directories" and "Advanced pointers, dynamic memory"; cppreference.com; AI (Claude Code, Opus 5.5), see the AI notes below

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#define STUDENT "serhii_voitov_251ADC006"

enum { NUM, PLUS, MINUS, STAR, SLASH, POW, LPAREN, RPAREN, END, BAD };

typedef struct {
    int type;
    size_t start;   // index of the first character of the token in the file
    long long val;
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

// Reads the next token into tok. A line ends at '\n' or at the end of the
// file, and both become END, so "missing operand at end of line" naturally
// points at the '\n' (or at file length + 1 when there is no final '\n').
static void next(void) {
    while (pos < len && (src[pos] == ' ' || src[pos] == '\t')) pos++;
    tok.start = pos;
    if (pos >= len || src[pos] == '\n') { tok.type = END; return; }

    char c = src[pos];
    if (c >= '0' && c <= '9') {
        tok.type = NUM;
        tok.val = strtoll(src + pos, NULL, 10);
        while (pos < len && src[pos] >= '0' && src[pos] <= '9') pos++;
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

static long long expr(void);

// primary := NUMBER | '(' expr ')'
// There are no unary operators, so a number or '(' must come here. Anything
// else (an operator, ')', a bad character, the end of the line) is reported
// at the start of that token, because that is where the parser got stuck.
// A missing ')' is found the same way: after the inner expr the parser
// expects ')', sees END instead, and reports the '\n' position.
static long long primary(void) {
    if (tok.type == NUM) {
        long long v = tok.val;
        next();
        return v;
    }
    if (tok.type != LPAREN) { fail(tok.start); return 0; }
    next();
    long long v = expr();
    if (err) return 0;
    if (tok.type != RPAREN) { fail(tok.start); return 0; }
    next();
    return v;
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

// power := primary [ '**' power ]
// The exponent is parsed by calling power() again, not with a loop like the
// other levels. That recursion finishes the right side first, so
// 2 ** 3 ** 2 groups as 2 ** (3 ** 2) = 512 (right-associative).
static long long power(void) {
    long long v = primary();
    if (err || tok.type != POW) return v;
    next();
    size_t exp_at = tok.start;  // a negative exponent is reported here
    long long e = power();
    if (err) return 0;
    if (e < 0) { fail(exp_at); return 0; }
    return ipow(v, e);
}

// Precedence comes from the call chain: expr() only sees results that term()
// has already finished, and term() only sees finished power() results, so
// ** binds tighter than * and /, which bind tighter than + and -. The while
// loops apply operators of the same level from left to right, which makes
// 8 - 3 - 2 mean (8 - 3) - 2.

// term := power { ('*' | '/') power }
static long long term(void) {
    long long v = power();
    while (!err && (tok.type == STAR || tok.type == SLASH)) {
        int op = tok.type;
        next();
        size_t rhs = tok.start;     // division by zero is reported here
        long long r = power();
        if (err) break;
        if (op == STAR) v *= r;
        else if (r == 0) fail(rhs);
        else v /= r;    // C already truncates toward zero, as the task wants
    }
    return v;
}

// expr := term { ('+' | '-') term }
static long long expr(void) {
    long long v = term();
    while (!err && (tok.type == PLUS || tok.type == MINUS)) {
        int op = tok.type;
        next();
        long long r = term();
        v = op == PLUS ? v + r : v - r;
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
            long long v = expr();
            // A full expression must end the line; whatever is left is an error.
            if (!err && tok.type != END) fail(tok.start);
            if (!err) fprintf(out, "%lld\n", v);
        }
        pos = tok.start + 1;    // step over the '\n'
    }
    return err;
}

// Reads the whole file with one fread, so every token and position refers to
// the same buffer and nothing is copied again while parsing.
static char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *buf = malloc(n > 0 ? n : 1);
    if (buf) *size = fread(buf, 1, n, f);
    fclose(f);
    return buf;
}

int main(int argc, char *argv[]) {
    const char *outdir = NULL, *input = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) outdir = argv[++i];
        else input = argv[i];
    }
    if (!input || !outdir) {
        fprintf(stderr, "usage: calc -o OUTDIR input.txt\n");
        return 1;
    }

    char *buf = read_file(input, &len);
    if (!buf) { perror(input); return 1; }
    src = buf;

    // Output name: <OUTDIR>/<input name without .txt>_<name>_<lastname>_<id>.txt
    const char *base = strrchr(input, '/') ? strrchr(input, '/') + 1 : input;
    size_t blen = strlen(base);
    if (blen > 4 && strcmp(base + blen - 4, ".txt") == 0) blen -= 4;
    char path[4096];
    snprintf(path, sizeof path, "%s/%.*s_%s.txt", outdir, (int)blen, base, STUDENT);

    // The output folder may already exist, which is fine.
    if (mkdir(outdir, 0755) != 0 && errno != EEXIST) { perror(outdir); free(buf); return 1; }
    FILE *out = fopen(path, "w");
    if (!out) { perror(path); free(buf); return 1; }
    size_t e = eval_all(out);
    // Only the first error may be in the file, so throw away the results
    // written so far by opening the file again for writing (this empties it).
    if (e) {
        out = freopen(path, "w", out);
        if (out) fprintf(out, "ERROR:%zu\n", e);
    }
    if (out) fclose(out);
    free(buf);
    return 0;
}
