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

enum { NUM, PLUS, MINUS, END, BAD };

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
    else tok.type = BAD;
}

// There are no unary operators, so a number must come here. Anything else
// (an operator, a bad character, the end of the line) is reported at the
// start of that token, because that is where the parser got stuck.
static long long primary(void) {
    if (tok.type != NUM) { fail(tok.start); return 0; }
    long long v = tok.val;
    next();
    return v;
}

// expr := primary { ('+' | '-') primary }
static long long expr(void) {
    long long v = primary();
    while (!err && (tok.type == PLUS || tok.type == MINUS)) {
        int op = tok.type;
        next();
        long long r = primary();
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
        if (tok.type != END) {
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
