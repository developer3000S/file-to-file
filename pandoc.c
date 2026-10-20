#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */
/* Pandoc                                                                       */
/* ------------------------------------------------------------------------- */

int run_pandoc(const char *from, const char *to, const char *in, const char *out) {
    if (need_tool("pandoc") != 0) return -1;
    char engine[32] = "";
    int have_engine = 0;
    if (strcmp(to, "pdf") == 0) {
        have_engine = pandoc_pdf_engine_args(engine, sizeof(engine));
        if (!have_engine) {
            fprintf(stderr, "Ошибка: для вывода PDF нужен 'xelatex' или 'pdflatex'\n");
            return -1;
        }
    }

    char *argv[24];
    int n = 0;
    argv[n++] = "pandoc";
    argv[n++] = (char *)in;
    argv[n++] = "-f";
    argv[n++] = (char *)from;
    argv[n++] = "-t";
    argv[n++] = (char *)to;
    if (have_engine) {
        static char pe[64], mf[64];
        snprintf(pe, sizeof(pe), "--pdf-engine=%s", engine);
        argv[n++] = pe;
        if (strcmp(engine, "xelatex") == 0) {
            snprintf(mf, sizeof(mf), "mainfont=Liberation Serif");
            argv[n++] = "-V";
            argv[n++] = mf;
        } else {
            argv[n++] = "-V";
            argv[n++] = "lang=russian";
            argv[n++] = "-V";
            argv[n++] = "babel-lang=russian";
        }
    }
    argv[n++] = "-o";
    argv[n++] = (char *)out;
    argv[n] = NULL;
    return run_cmd(argv);
}

int pandoc_pdf_engine_args(char *buf, size_t len) {
    if (tool_available("xelatex")) { snprintf(buf, len, "xelatex"); return 1; }
    if (tool_available("pdflatex")) { snprintf(buf, len, "pdflatex"); return 1; }
    return 0;
}

/* `gfm` вместо `markdown`: gfm-reader не падает на больших OCR-текстах. */
int md_to_docx(const char *in, const char *out) { return run_pandoc("gfm", "docx", in, out); }
int md_to_pdf(const char *in, const char *out)  { return run_pandoc("gfm", "pdf", in, out); }
int docx_to_md(const char *in, const char *out) {
    return run_pandoc("docx", "gfm", in, out);
}
int docx_to_pdf(const char *in, const char *out){ return run_pandoc("docx", "pdf", in, out); }

/* pandoc не умеет писать xlsx, поэтому маршрут идёт через разбор таблицы и
 * pandas: markdown -> table_t -> CSV -> xlsx. */
int md_to_xlsx(const char *in, const char *out) {
    if (need_tool("python3") != 0) return -1;
    table_t *t = md_read_first_table(in);
    if (!t || t->nrows == 0) { table_free(t); return -1; }
    char csv[4096];
    if (work_path(csv, sizeof(csv), "in.csv") != 0) { table_free(t); return -1; }
    int rc = csv_write_file(t, csv);
    table_free(t);
    if (rc == 0) rc = csv_to_xlsx(csv, out);
    unlink(csv);
    return rc;
}

int docx_to_xlsx(const char *in, const char *out) {
    char md[4096];
    if (work_path(md, sizeof(md), "tmp.md") != 0) return -1;
    int rc = docx_to_md(in, md);
    if (rc == 0) rc = md_to_xlsx(md, out);
    unlink(md);
    return rc;
}

int xlsx_to_md(const char *in, const char *out) {
    if (need_tool("python3") != 0) return -1;
    char csv[4096];
    if (work_path(csv, sizeof(csv), "in.csv") != 0) return -1;
    int rc = xlsx_to_csv(in, csv);
    if (rc == 0) {
        table_t *t = csv_read_file(csv);
        if (!t || t->nrows == 0) {
            table_free(t);
            unlink(csv);
            return -1;
        }
        rc = md_write_table(t, out);
        table_free(t);
    }
    unlink(csv);
    return rc;
}

int xlsx_to_docx(const char *in, const char *out) {
    if (need_tool("python3") != 0) return -1;
    char csv[4096];
    if (work_path(csv, sizeof(csv), "in.csv") != 0) return -1;
    int rc = xlsx_to_csv(in, csv);
    if (rc == 0) rc = run_pandoc("csv", "docx", csv, out);
    unlink(csv);
    return rc;
}

int xlsx_to_pdf(const char *in, const char *out) {
    if (need_tool("python3") != 0) return -1;
    char csv[4096];
    if (work_path(csv, sizeof(csv), "in.csv") != 0) return -1;
    int rc = xlsx_to_csv(in, csv);
    if (rc == 0) rc = run_pandoc("csv", "pdf", csv, out);
    unlink(csv);
    return rc;
}
