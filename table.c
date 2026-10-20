#include "utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* CSV-разбор (RFC 4180: кавычки, экранирование "", переводы строк в полях)      */
/* ------------------------------------------------------------------------- */

static char *xstrdup(const char *s) {
    char *r = strdup(s ? s : "");
    if (!r) { perror("strdup"); exit(1); }
    return r;
}

static void *xrealloc(void *p, size_t n) {
    void *r = realloc(p, n);
    if (!r) { perror("realloc"); exit(1); }
    return r;
}

/* Читает одну CSV-запись из f, возвращает malloc-буфер или NULL в конце файла.
 * Сохраняет кавычки как есть: разбиение на поля делает csv_split_record,
 * которому нужно видеть кавычки, чтобы не резать запятые внутри поля.
 * Запись заканчивается переводом строки, лежащим ВНЕ кавычек. */
static char *csv_read_record(FILE *f) {
    size_t cap = 256, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    int c, quoted = 0, any = 0;

    while ((c = fgetc(f)) != EOF) {
        any = 1;
        if (c == '"') quoted = !quoted;   /* "" переключает дважды — без изменений */
        if (!quoted && (c == '\n' || c == '\r')) {
            if (c == '\r') {
                int n = fgetc(f);
                if (n != '\n' && n != EOF) ungetc(n, f);
            }
            break;
        }
        if (len + 2 > cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        buf[len++] = (char)c;
    }
    if (!any) { free(buf); return NULL; }
    buf[len] = '\0';
    return buf;
}

/* Разбивает запись на поля; возвращает число полей, *row — malloc-массив. */
static char **csv_split_record(const char *rec, int *ncols) {
    int cap = 8, n = 0;
    char **row = malloc(cap * sizeof(*row));
    if (!row) return NULL;
    const char *p = rec;
    for (;;) {
        size_t cap_f = 64, len_f = 0;
        char *field = malloc(cap_f);
        if (!field) { for (int i = 0; i < n; i++) free(row[i]); free(row); return NULL; }
        int quoted = 0;
        if (*p == '"') { quoted = 1; p++; }
        for (;;) {
            if (quoted) {
                if (*p == '"') {
                    if (p[1] == '"') {
                        if (len_f + 2 > cap_f) { cap_f *= 2; field = realloc(field, cap_f); }
                        field[len_f++] = '"';
                        p += 2;
                        continue;
                    }
                    quoted = 0;
                    p++;
                    continue;
                }
                if (!*p) break;
                if (len_f + 2 > cap_f) { cap_f *= 2; field = realloc(field, cap_f); }
                field[len_f++] = *p++;
            } else {
                if (*p == ',' || *p == '\0') break;
                if (len_f + 2 > cap_f) { cap_f *= 2; field = realloc(field, cap_f); }
                field[len_f++] = *p++;
            }
        }
        field[len_f] = '\0';
        if (n >= cap) {
            cap *= 2;
            char **nr = realloc(row, cap * sizeof(*row));
            if (!nr) { free(field); for (int i = 0; i < n; i++) free(row[i]); free(row); return NULL; }
            row = nr;
        }
        row[n++] = field;
        if (*p == ',') { p++; continue; }
        break;
    }
    *ncols = n;
    return row;
}

table_t *csv_read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    table_t *t = calloc(1, sizeof(table_t));
    if (!t) { fclose(f); return NULL; }
    char *rec;
    while ((rec = csv_read_record(f))) {
        int ncols = 0;
        char **row = csv_split_record(rec, &ncols);
        free(rec);
        if (!row) { table_free(t); return NULL; }
        if (ncols == 1 && row[0][0] == '\0') { free(row[0]); free(row); continue; }
        char ***rows = realloc(t->data, (t->nrows + 1) * sizeof(char **));
        if (!rows) { for (int i = 0; i < ncols; i++) free(row[i]); free(row); break; }
        t->data = rows;
        t->data[t->nrows++] = row;
        if (ncols > t->ncols) t->ncols = ncols;
    }
    fclose(f);
    return t;
}

static void csv_write_cell(FILE *f, const char *s) {
    if (!s) { return; }
    int needs = (strpbrk(s, ",\"\n\r") != NULL);
    if (needs) fputc('"', f);
    for (const char *p = s; *p; p++) {
        if (*p == '"') fputc('"', f);
        fputc(*p, f);
    }
    if (needs) fputc('"', f);
}

int csv_write_file(table_t *t, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int r = 0; r < t->nrows; r++) {
        for (int c = 0; c < t->ncols; c++) {
            csv_write_cell(f, t->data[r][c]);
            if (c < t->ncols - 1) fputc(',', f);
        }
        fputc('\n', f);
    }
    if (fclose(f) != 0) return -1;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Markdown-таблицы                                                            */
/* ------------------------------------------------------------------------- */

static char *str_trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return s;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) *e-- = '\0';
    return s;
}

static int is_sep_row(const char *line) {
    int dashes = 0;
    for (const char *p = line; *p; p++) {
        if (*p == '-' || *p == ':' || *p == '|' || isspace((unsigned char)*p)) {
            if (*p == '-') dashes++;
            continue;
        }
        return 0;
    }
    return dashes > 0;
}

/* Разбирает строку markdown-таблицы в массив ячеек (снимает внешние |). */
static char **md_split_row(const char *line, int *ncols) {
    char *dup = xstrdup(line);
    char *s = str_trim(dup);
    int cap = 8, n = 0;
    char **row = malloc(cap * sizeof(*row));
    if (!row) { free(dup); return NULL; }
    if (*s == '|') s++;
    size_t l = strlen(s);
    if (l > 0 && s[l - 1] == '|') s[l - 1] = '\0';

    char *save = NULL;
    for (char *tok = strtok_r(s, "|", &save); tok; tok = strtok_r(NULL, "|", &save)) {
        if (n >= cap) {
            cap *= 2;
            char **nr = realloc(row, cap * sizeof(*nr));
            if (!nr) break;
            row = nr;
        }
        row[n++] = xstrdup(str_trim(tok));
    }
    free(dup);
    *ncols = n;
    return row;
}

static void table_grow(table_t *t, int new_ncols) {
    if (new_ncols <= t->ncols) return;
    for (int r = 0; r < t->nrows; r++) {
        char **nr = xrealloc(t->data[r], new_ncols * sizeof(*nr));
        t->data[r] = nr;
        for (int c = t->ncols; c < new_ncols; c++) t->data[r][c] = xstrdup("");
    }
    t->ncols = new_ncols;
}

static void table_append(table_t **tp, char **row, int ncols) {
    table_t *t = *tp;
    if (!t) {
        t = calloc(1, sizeof(table_t));
        if (!t) { for (int i = 0; i < ncols; i++) free(row[i]); free(row); return; }
        *tp = t;
    }
    if (ncols > t->ncols) table_grow(t, ncols);
    char ***rows = xrealloc(t->data, (t->nrows + 1) * sizeof(char **));
    t->data = rows;
    t->data[t->nrows++] = row;
}

table_t *md_read_first_table(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    table_t *t = NULL;
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    int seen_header = 0;
    while ((len = getline(&line, &cap, f)) > 0) {
        char *s = str_trim(line);
        int is_table_line = (strchr(s, '|') != NULL);
        if (!is_table_line) {
            if (seen_header) break;
            continue;
        }
        if (is_sep_row(s)) continue;
        int ncols = 0;
        char **row = md_split_row(s, &ncols);
        if (!row) continue;
        if (ncols == 0) { free(row); continue; }
        seen_header = 1;
        table_append(&t, row, ncols);
    }
    free(line);
    fclose(f);
    return t;
}

/* Каждая строка текста — одна ячейка в первой колонке (для PDF без таблиц). */
table_t *table_from_text_lines(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    table_t *t = calloc(1, sizeof(table_t));
    if (!t) { fclose(f); return NULL; }
    t->ncols = 1;
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    while ((len = getline(&line, &cap, f)) > 0) {
        char *s = str_trim(line);
        if (!*s) continue;
        char **row = malloc(sizeof(*row));
        if (!row) break;
        row[0] = xstrdup(s);
        table_append(&t, row, 1);
    }
    free(line);
    fclose(f);
    return t;
}

int md_write_table(table_t *t, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    if (!t || t->nrows == 0) { fclose(f); return 0; }
    for (int c = 0; c < t->ncols; c++) {
        fputs(c ? " | " : "| ", f);
        fputs(t->data[0][c] ? t->data[0][c] : "", f);
    }
    fputs(" |\n", f);
    for (int c = 0; c < t->ncols; c++) fputs(c ? " | ---" : "| ---", f);
    fputs(" |\n", f);
    for (int r = 1; r < t->nrows; r++) {
        for (int c = 0; c < t->ncols; c++) {
            fputs(c ? " | " : "| ", f);
            fputs(t->data[r][c] ? t->data[r][c] : "", f);
        }
        fputs(" |\n", f);
    }
    if (fclose(f) != 0) return -1;
    return 0;
}

void table_free(table_t *t) {
    if (!t) return;
    for (int r = 0; r < t->nrows; r++) {
        if (!t->data[r]) continue;
        for (int c = 0; c < t->ncols; c++) free(t->data[r][c]);
        free(t->data[r]);
    }
    free(t->data);
    free(t);
}

