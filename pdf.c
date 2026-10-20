#include "utils.h"

#include <ctype.h>
#include <unistd.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Примитивы конвертации (каждый: входной путь -> выходной путь)                   */
/* ------------------------------------------------------------------------- */

int pdf_to_md(const char *in, const char *out) {
    /* Отсканированный PDF не имеет текстового слоя: pdftotext вернул бы пустой
     * вывод, поэтому распознаём текст через OCR (Tesseract). */
    if (is_scanned_pdf(in) == 1) {
        fprintf(stderr, "PDF без текстового слоя, распознаю через OCR: %s\n", in);
        return ocr_pdf(in, out);
    }
    if (tool_available("pdftotext")) {
        char *argv[] = { "pdftotext", "-layout", (char *)in, (char *)out, NULL };
        return run_cmd(argv);
    }
    if (tool_available("mutool")) {
        char *argv[] = { "mutool", "draw", "-F", "txt", (char *)in, NULL };
        return run_cmd_stdout(argv, out);
    }
    fprintf(stderr, "Ошибка: ни 'pdftotext', ни 'mutool' недоступны для PDF\n");
    return -1;
}

int pdf_to_docx(const char *in, const char *out) {
    char md[4096];
    work_path(md, sizeof(md), "tmp.md");
    int rc = pdf_to_md(in, md);
    if (rc == 0) rc = md_to_docx(md, out);
    unlink(md);
    return rc;
}

int pdf_to_xlsx(const char *in, const char *out) {
    if (need_tool("python3") != 0) return -1;
    char md[4096], csv[4096];
    work_path(md, sizeof(md), "tmp.md");
    work_path(csv, sizeof(csv), "tmp.csv");
    int rc = pdf_to_md(in, md);
    if (rc == 0) {
        table_t *t = md_read_first_table(md);
        if (!t || t->nrows == 0) {
            table_free(t);
            /* pdftotext мог извлечь текст, но без табличных разделителей '|'.
             * table_from_text_lines читает каждую строку как одну ячейку. */
            t = table_from_text_lines(md);
            if (!t || t->nrows == 0) { table_free(t); rc = -1; }
            else {
                rc = csv_write_file(t, csv);
                table_free(t);
                if (rc == 0) rc = csv_to_xlsx(csv, out);
            }
        } else {
            rc = csv_write_file(t, csv);
            table_free(t);
            if (rc == 0) rc = csv_to_xlsx(csv, out);
        }
    }
    unlink(md); unlink(csv);
    return rc;
}

int is_scanned_pdf(const char *path) {
    if (need_tool("pdftotext") != 0) return -1;
    char out[4096];
    work_path(out, sizeof(out), "pdftotext.out");
    char *argv[] = { "pdftotext", (char *)path, out, NULL };
    int rc = run_cmd(argv);
    if (rc != 0) return -1;
    FILE *f = fopen(out, "r");
    if (!f) return -1;
    int nlines = 0;
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        int nws = 0;
        for (char *p = buf; *p; p++) if (!isspace((unsigned char)*p)) nws++;
        if (nws > 0) nlines++;
        if (nlines >= 3) { fclose(f); unlink(out); return 0; }
    }
    fclose(f);
    unlink(out);
    return 1;
}
