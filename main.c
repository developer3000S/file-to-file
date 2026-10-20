#include "utils.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */
/* Форматы и маршрутизация                                                     */
/* ------------------------------------------------------------------------- */

static const char *fmt_name(int f) {
    switch (f) {
        case FMT_PDF:  return "pdf";
        case FMT_DOCX: return "docx";
        case FMT_XLSX: return "xlsx";
        case FMT_MD:   return "md";
        default:       return "?";
    }
}

static int fmt_from_ext(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) {
        if (strcasecmp(path, "pdf") == 0) return FMT_PDF;
        if (strcasecmp(path, "docx") == 0) return FMT_DOCX;
        if (strcasecmp(path, "xlsx") == 0) return FMT_XLSX;
        if (strcasecmp(path, "md") == 0) return FMT_MD;
        return -1;
    }
    if (strcasecmp(dot, ".pdf") == 0) return FMT_PDF;
    if (strcasecmp(dot, ".docx") == 0) return FMT_DOCX;
    if (strcasecmp(dot, ".xlsx") == 0) return FMT_XLSX;
    if (strcasecmp(dot, ".md") == 0 || strcasecmp(dot, ".markdown") == 0) return FMT_MD;
    return -1;
}

typedef int (*conv_fn)(const char *in, const char *out);

static int copy_file_bytes(const char *in, const char *out) {
    FILE *fi = fopen(in, "rb");
    if (!fi) {
        fprintf(stderr, "Ошибка: не удаётся открыть '%s': %s\n", in, strerror(errno));
        return -1;
    }
    FILE *fo = fopen(out, "wb");
    if (!fo) {
        fprintf(stderr, "Ошибка: не удаётся открыть '%s': %s\n", out, strerror(errno));
        fclose(fi);
        return -1;
    }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fi)) > 0) fwrite(buf, 1, n, fo);
    fclose(fi);
    if (fclose(fo) != 0) return -1;
    return 0;
}

static int convert(const char *in_path, const char *out_path) {
    struct stat st;
    if (stat(in_path, &st) != 0) {
        fprintf(stderr, "Ошибка: входной файл '%s' не существует\n", in_path);
        return -1;
    }

    int fin = fmt_from_ext(in_path);
    int fout = fmt_from_ext(out_path);
    if (fin < 0 || fout < 0) {
        fprintf(stderr, "Ошибка: нераспознанное расширение (%s -> %s)\n", in_path, out_path);
        return -1;
    }

    /* Уже целевой формат: копируем (батч-режим опирается на это для файлов,
     * совпадающих с требуемым форматом). Перезапись самого себя запрещена. */
    if (fin == fout) {
        if (strcmp(in_path, out_path) == 0) {
            fprintf(stderr, "Предупреждение: отказ от перезаписи входного файла '%s'\n", in_path);
            return -1;
        }
        return copy_file_bytes(in_path, out_path);
    }

    static const conv_fn table[4][4] = {
        /*        to: PDF         DOCX          XLSX          MD                        from */
        /* PDF  */ { NULL,         pdf_to_docx,  pdf_to_xlsx,  pdf_to_md   },
        /* DOCX */ { docx_to_pdf,  NULL,         docx_to_xlsx, docx_to_md  },
        /* XLSX */ { xlsx_to_pdf,  xlsx_to_docx, NULL,         xlsx_to_md  },
        /* MD   */ { md_to_pdf,    md_to_docx,   md_to_xlsx,   NULL        },
    };

    conv_fn fn = table[fin][fout];
    if (!fn) {
        fprintf(stderr, "Ошибка: неподдерживаемая конвертация %s -> %s\n",
                fmt_name(fin), fmt_name(fout));
        return -1;
    }
    return fn(in_path, out_path);
}

/* ------------------------------------------------------------------------- */
/* Батч-режим                                                                  */
/* ------------------------------------------------------------------------- */

static int is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int process_directory(const char *indir, const char *outdir, int target_fmt) {
    char *ai = realpath(indir, NULL);
    char *ao = realpath(outdir, NULL);
    if (ai && ao && strcmp(ai, ao) == 0) {
        fprintf(stderr, "Ошибка: входной и выходной каталоги должны отличаться\n");
        free(ai); free(ao);
        return -1;
    }
    free(ai); free(ao);

    if (mkdir_p(outdir) != 0) {
        fprintf(stderr, "Ошибка: не удаётся создать выходной каталог '%s': %s\n", outdir, strerror(errno));
        return -1;
    }

    DIR *d = opendir(indir);
    if (!d) {
        fprintf(stderr, "Ошибка: не удаётся открыть каталог '%s': %s\n", indir, strerror(errno));
        return -1;
    }

    /* Снимает список файлов целиком: readdir() неустойчива к добавлению файлов
     * во время обхода, а счётчик нужен заранее для прогресса. */
    char **names = NULL;
    int n = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char full[4096];
        int w = snprintf(full, sizeof(full), "%s/%s", indir, e->d_name);
        if (w < 0 || w >= (int)sizeof(full)) {
            fprintf(stderr, "Предупреждение: слишком длинный путь, пропущен: %s/%s\n", indir, e->d_name);
            continue;
        }
        if (is_dir(full)) continue;
        if (fmt_from_ext(e->d_name) < 0) continue;
        if (n >= cap) {
            cap = cap ? cap * 2 : 32;
            char **nn = realloc(names, cap * sizeof(*nn));
            if (!nn) break;
            names = nn;
        }
        names[n++] = strdup(e->d_name);
    }
    closedir(d);

    if (n == 0) {
        fprintf(stderr, "Ошибка: не найдено поддерживаемых входных файлов в '%s'\n", indir);
        free(names);
        return -1;
    }

    int converted = 0, errors = 0, skipped = 0, done = 0;
    for (int i = 0; i < n; i++) {
        char in_path[4096], out_path[4096];
        int w = snprintf(in_path, sizeof(in_path), "%s/%s", indir, names[i]);
        if (w < 0 || w >= (int)sizeof(in_path)) {
            fprintf(stderr, "Предупреждение: слишком длинный путь, пропущен: %s/%s\n", indir, names[i]);
            skipped++;
            free(names[i]);
            continue;
        }
        const char *dot = strrchr(names[i], '.');
        size_t stem = dot ? (size_t)(dot - names[i]) : strlen(names[i]);
        int fin = fmt_from_ext(names[i]);

        /* Имя вывода: <основа>_<исходный_формат>.<целевой> для реальных
         * конвертаций; для файла, уже находящегося в целевом формате, — просто
         * <основа>.<целевой> (копия). */
        int wo;
        if (fin == target_fmt) {
            wo = snprintf(out_path, sizeof(out_path), "%s/%.*s.%s",
                          outdir, (int)stem, names[i], fmt_name(target_fmt));
        } else {
            wo = snprintf(out_path, sizeof(out_path), "%s/%.*s_%s.%s",
                          outdir, (int)stem, names[i], fmt_name(fin), fmt_name(target_fmt));
        }
        if (wo < 0 || wo >= (int)sizeof(out_path)) {
            fprintf(stderr, "Предупреждение: слишком длинный путь вывода, пропущен: %s\n", out_path);
            skipped++;
            free(names[i]);
            continue;
        }

        log_progress(++done, n, names[i]);
        logf_at(LOG_INFO, "Конвертирую %s -> %s (%s)\n", in_path, out_path, fmt_name(target_fmt));
        if (convert(in_path, out_path) == 0) {
            converted++;
        } else {
            logf_at(LOG_ERROR, "Ошибка конвертации: %s -> %s\n", in_path, out_path);
            errors++;
        }
        free(names[i]);
    }
    free(names);

    logf_at(LOG_INFO, "Итог: %d конвертировано, %d ошибок, %d пропущено -> %s/\n",
            converted, errors, skipped, outdir);
    return errors == 0 ? 0 : 1;
}

static void print_help(const char *prog) {
    printf("file-to-file: конвертирует документы между форматами.\n");
    printf("\nПоддерживаемые конвертации:\n");
    static const char *routes[] = {
        "pdf  -> docx", "pdf  -> xlsx", "pdf  -> md",
        "docx -> pdf",  "docx -> xlsx", "docx -> md",
        "xlsx -> pdf",  "xlsx -> docx", "xlsx -> md",
        "md   -> pdf",  "md   -> docx", "md   -> xlsx",
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
        printf("  %s\n", routes[i]);
    printf("\nИспользование:\n");
    printf("  %s <входной_файл> <выходной_файл>\n", prog);
    printf("  %s <входной_каталог> <выходной_каталог> <формат>\n", prog);
    printf("\nПримеры:\n");
    printf("  %s document.pdf output.md\n", prog);
    printf("  %s ./docs ./docs_pdf pdf\n", prog);
    printf("\nБатч-режим конвертирует каждый обычный файл в <входной_каталог> (не рекурсивно) в");
    printf(" <формат>; файлы без поддерживаемых конвертаций пропускаются.\n");
    printf("\nОпции:\n");
    printf("  -v, --verbose   подробный вывод\n");
    printf("  -q, --quiet     только ошибки\n");
    printf("  -h, --help      эта справка\n");
    printf("\nСканированные PDF (без текстового слоя) распознаются через Tesseract;\n");
    printf("результаты OCR кэшируются в $XDG_CACHE_HOME/file-to-file/ocr (или $FF_OCR_CACHE_DIR).\n");
    printf("\nЗависимости: pandoc (docx/pdf/md), pdftotext или mutool (pdf),\n");
    printf("python3 + pandas + openpyxl (xlsx), xelatex или pdflatex (вывод PDF).\n");
}

/* ------------------------------------------------------------------------- */
/* Main                                                                       */
/* ------------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    const char *prog = argv[0];
    char *pos[3] = { NULL, NULL, NULL };
    int npos = 0;
    int verbosity = LOG_INFO;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) { print_help(prog); return 0; }
        else if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) verbosity = LOG_DEBUG;
        else if (strcmp(a, "-q") == 0 || strcmp(a, "--quiet") == 0) verbosity = LOG_ERROR;
        else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "Ошибка: неизвестный ключ '%s'\n", a);
            return 2;
        }
        else if (npos < 3) pos[npos++] = (char *)a;
        else { fprintf(stderr, "Ошибка: лишний аргумент '%s'\n", a); return 2; }
    }
    g_verbosity = verbosity;

    if (npos == 2) {
        if (convert(pos[0], pos[1]) == 0) {
            logf_at(LOG_INFO, "Конвертация успешна: %s -> %s\n", pos[0], pos[1]);
            return 0;
        }
        return 1;
    }
    if (npos == 3 && is_dir(pos[0])) {
        int target_fmt = fmt_from_ext(pos[2]);
        if (target_fmt < 0) {
            fprintf(stderr, "Ошибка: неподдерживаемый целевой формат '%s'\n", pos[2]);
            fprintf(stderr, "Поддерживаемые: pdf, docx, xlsx, md\n");
            return 2;
        }
        return process_directory(pos[0], pos[1], target_fmt) == 0 ? 0 : 1;
    }
    if (npos == 3) {
        fprintf(stderr, "Ошибка: для входного каталога нужен целевой формат\n");
        return 2;
    }
    fprintf(stderr, "Использование: %s <входной_файл> <выходной_файл>\n", prog);
    fprintf(stderr, "           или: %s <входной_каталог> <выходной_каталог> <формат>\n", prog);
    fprintf(stderr, "Для справки: %s --help\n", prog);
    return 2;
}
