#ifndef UTILS_H
#define UTILS_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>

#define FMT_PDF  0
#define FMT_DOCX 1
#define FMT_XLSX 2
#define FMT_MD   3

typedef struct { int ncols; int nrows; char ***data; } table_t;

/* --- Выполнение внешних команд -------------------------------------------- */
int run_cmd(char *const argv[]);
int run_cmd_stdout(char *const argv[], const char *out_path);
int need_tool(const char *tool);
int tool_available(const char *tool);

/* --- Приватный рабочий каталог -------------------------------------------- */
const char *workdir(void);
int work_path(char *buf, size_t len, const char *name);
void remove_tree(const char *path);
int mkdir_p(const char *path);

/* --- Логирование и прогресс ------------------------------------------------ */
enum { LOG_QUIET = 0, LOG_ERROR = 1, LOG_INFO = 2, LOG_DEBUG = 3 };
extern int g_verbosity;
void logf_at(int level, const char *fmt, ...);
void log_progress(int index, int total, const char *name);

/* --- OCR (Tesseract) ------------------------------------------------------- */
int ocr_pdf(const char *in, const char *out);
int is_scanned_pdf(const char *path);

/* --- PDF ------------------------------------------------------------------- */
int pdf_to_md(const char *in, const char *out);
int pdf_to_docx(const char *in, const char *out);
int pdf_to_xlsx(const char *in, const char *out);

/* --- pandoc-маршруты ------------------------------------------------------- */
int md_to_docx(const char *in, const char *out);
int md_to_pdf(const char *in, const char *out);
int md_to_xlsx(const char *in, const char *out);
int docx_to_md(const char *in, const char *out);
int docx_to_pdf(const char *in, const char *out);
int docx_to_xlsx(const char *in, const char *out);
int xlsx_to_md(const char *in, const char *out);
int xlsx_to_docx(const char *in, const char *out);
int xlsx_to_pdf(const char *in, const char *out);

int run_pandoc(const char *from, const char *to, const char *in, const char *out);
int pandoc_pdf_engine_args(char *buf, size_t len);

/* --- XLSX через python3 ---------------------------------------------------- */
int xlsx_to_csv(const char *in, const char *out);
int csv_to_xlsx(const char *in, const char *out);
int xlsx_to_md_multi(const char *in, const char *out);
int run_python_script(const char *code, const char *const args[], int nargs);

/* --- Таблицы / CSV --------------------------------------------------------- */
table_t *csv_read_file(const char *path);
int csv_write_file(table_t *t, const char *path);
table_t *md_read_first_table(const char *path);
table_t *table_from_text_lines(const char *path);
int md_write_table(table_t *t, const char *path);
void table_free(table_t *t);

#endif /* UTILS_H */
