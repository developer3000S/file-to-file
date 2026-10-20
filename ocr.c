#include "utils.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */
/* OCR для отсканированных PDF (Tesseract)                                         */
/* ------------------------------------------------------------------------- */

/* Кэш распознавания переживает завершение процесса: повторный запуск на том же
 * PDF не гоняет pdftoppm+tesseract заново. Ключ — путь+размер+mtime, поэтому
 * изменённый файл автоматически уходит в новый проход. */

static uint64_t fnv1a(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

static int cache_key(const char *in, char *buf, size_t len) {
    struct stat st;
    if (stat(in, &st) != 0) return -1;
    char abs[4096];
    if (!realpath(in, abs)) snprintf(abs, sizeof(abs), "%s", in);
    char raw[4352];
    snprintf(raw, sizeof(raw), "%s|%lld|%lld", abs,
             (long long)st.st_size, (long long)st.st_mtime);
    snprintf(buf, len, "%016llx", (unsigned long long)fnv1a(raw));
    return 0;
}

static const char *cache_dir(void) {
    static char path[4096];
    if (path[0] == '\0') {
        const char *base = getenv("FF_OCR_CACHE_DIR");
        if (!base || !*base) {
            base = getenv("XDG_CACHE_HOME");
            if (base && *base) {
                snprintf(path, sizeof(path), "%s/file-to-file/ocr", base);
            } else {
                base = getenv("HOME");
                if (!base || !*base) base = "/tmp";
                snprintf(path, sizeof(path), "%s/.cache/file-to-file/ocr", base);
            }
            mkdir_p(path);
            return path;
        }
        snprintf(path, sizeof(path), "%s", base);
        mkdir_p(path);
    }
    return path;
}

static int cache_path_for(const char *in, char *buf, size_t len) {
    char key[32];
    if (cache_key(in, key, sizeof(key)) != 0) return -1;
    int n = snprintf(buf, len, "%s/%s.txt", cache_dir(), key);
    if (n < 0 || (size_t)n >= len) return -1;
    return 0;
}

static int copy_file(const char *src, const char *dst) {
    FILE *fi = fopen(src, "rb");
    if (!fi) return -1;
    FILE *fo = fopen(dst, "wb");
    if (!fo) { fclose(fi); return -1; }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fi)) > 0) fwrite(buf, 1, n, fo);
    fclose(fi);
    if (fclose(fo) != 0) return -1;
    return 0;
}

/* Сортировка по имени: pdftoppm нулевой дополняет номер страницы до ширины,
 * достаточной для общего числа страниц, поэтому лексикографический порядок
 * совпадает с порядком страниц. */
static int cmp_name(const void *a, const void *b) {
    const struct dirent *const *da = a;
    const struct dirent *const *db = b;
    return strcmp((*da)->d_name, (*db)->d_name);
}

/* Конвертирует PDF в текст с помощью Tesseract OCR. Возвращает 0 при успехе, -1 при ошибке. */
int ocr_pdf(const char *in, const char *out) {
    if (need_tool("tesseract") != 0) return -1;
    if (need_tool("pdftoppm") != 0) return -1;

    char cached[4096];
    if (cache_path_for(in, cached, sizeof(cached)) != 0) return -1;
    struct stat cst;
    if (cached[0] && stat(cached, &cst) == 0 && cst.st_size > 0) {
        logf_at(LOG_INFO, "OCR из кэша: %s\n", in);
        return copy_file(cached, out);
    }

    /* pdftotext/tesseract не читают PDF как изображение напрямую, поэтому
     * сначала растрируем страницы в PNG внутри приватного каталога. */
    char imgdir[4096];
    snprintf(imgdir, sizeof(imgdir), "%s/ocr", workdir());
    if (mkdir(imgdir, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "Ошибка: не удаётся создать каталог OCR: %s\n", strerror(errno));
        return -1;
    }
    char prefix[4096];
    if (snprintf(prefix, sizeof(prefix), "%s/pg", imgdir) >= (int)sizeof(prefix)) {
        fprintf(stderr, "Ошибка: слишком длинный рабочий путь\n");
        return -1;
    }

    char *ppm[] = { "pdftoppm", "-r", "200", "-png", (char *)in, prefix, NULL };
    if (run_cmd(ppm) != 0) return -1;

    DIR *d = opendir(imgdir);
    if (!d) {
        fprintf(stderr, "Ошибка: не удаётся открыть каталог растеризованных страниц\n");
        return -1;
    }
    struct dirent **names = NULL;
    int nnames = 0, cap_names = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l < 4 || strcmp(e->d_name + l - 4, ".png") != 0) continue;
        if (nnames >= cap_names) {
            cap_names = cap_names ? cap_names * 2 : 32;
            names = realloc(names, cap_names * sizeof(*names));
            if (!names) { closedir(d); return -1; }
        }
        struct dirent *dup = malloc(sizeof(*dup) + l + 1);
        if (!dup) continue;
        dup->d_name[l] = '\0';
        memcpy(dup->d_name, e->d_name, l + 1);
        names[nnames++] = dup;
    }
    closedir(d);

    if (nnames == 0) {
        fprintf(stderr, "Ошибка: pdftoppm не создал ни одного изображения\n");
        free(names);
        return -1;
    }
    qsort(names, nnames, sizeof(*names), cmp_name);
    logf_at(LOG_INFO, "Распознаю %d стр. через tesseract (rus+eng, psm 6)\n", nnames);

    char list[4096];
    work_path(list, sizeof(list), "ocr.list");
    FILE *f = fopen(list, "w");
    if (!f) { free(names); return -1; }
    for (int i = 0; i < nnames; i++) {
        fprintf(f, "%s/%s\n", imgdir, names[i]->d_name);
        free(names[i]);
    }
    fclose(f);
    free(names);

    /* Второй аргумент tesseract — префикс: он сам дописывает ".txt".
     * Имя не должно совпадать с imgdir, который тоже лежит в workdir. */
    char prefix_out[4096];
    if (snprintf(prefix_out, sizeof(prefix_out), "%s/text", workdir()) >= (int)sizeof(prefix_out)) {
        fprintf(stderr, "Ошибка: слишком длинный рабочий путь\n");
        return -1;
    }
    char *tess[] = { "tesseract", list, prefix_out, "-l", "rus+eng", "--psm", "6", NULL };
    int rc = run_cmd(tess);

    /* tesseract записывает результат в "<prefix>.txt" внутри приватного каталога. */
    char txt[4096];
    work_path(txt, sizeof(txt), "text.txt");

    remove_tree(imgdir);
    unlink(list);

    /* tesseract печатает "Page N : path" для каждой страницы и может вернуть
     * ненулевой код уже после того, как полный результат записан. Если артефакт
     * на месте и непустой — доверяем артефакту, а не коду возврата. */
    if (rc != 0) {
        struct stat st;
        if (stat(txt, &st) == 0 && st.st_size > 0) {
            logf_at(LOG_INFO, "tesseract завершился с кодом %d, но результат OCR сохранён\n", rc);
            rc = 0;
        } else {
            unlink(txt);
            return rc;
        }
    }

    if (cached[0]) {
        if (copy_file(txt, cached) != 0) logf_at(LOG_DEBUG, "не удалось записать кэш OCR\n");
    }

    if (rename(txt, out) != 0) {
        fprintf(stderr, "Ошибка: не удаётся переместить результат OCR: %s\n", strerror(errno));
        unlink(txt);
        return -1;
    }
    return 0;
}
