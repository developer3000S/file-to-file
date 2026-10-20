#include "utils.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */
/* Рабочий каталог                                                              */
/* ------------------------------------------------------------------------- */

static char g_workdir[4096];

static void workdir_cleanup(void) {
    remove_tree(g_workdir);
}

const char *workdir(void) {
    if (g_workdir[0] == '\0') {
        const char *base = getenv("TMPDIR");
        if (!base || !*base) base = "/tmp";
        snprintf(g_workdir, sizeof(g_workdir), "%s/file-to-file-XXXXXX", base);
        if (!mkdtemp(g_workdir)) {
            fprintf(stderr, "Ошибка: не удаётся создать рабочий каталог: %s\n", strerror(errno));
            g_workdir[0] = '\0';
            return NULL;
        }
        atexit(workdir_cleanup);
    }
    return g_workdir;
}

int work_path(char *buf, size_t len, const char *name) {
    int n = snprintf(buf, len, "%s/%s", workdir(), name);
    if (n < 0 || (size_t)n >= len) return -1;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Рекурсивное создание каталога                                              */
/* ------------------------------------------------------------------------- */

int mkdir_p(const char *path) {
    char buf[4096];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(buf)) return -1;
    strcpy(buf, path);
    if (buf[len - 1] == '/') buf[len - 1] = '\0';
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Рекурсивное удаление каталога                                                */
/* ------------------------------------------------------------------------- */

void remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            char sub[4096];
            snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
            remove_tree(sub);
        }
        closedir(d);
        rmdir(path);
    } else {
        unlink(path);
    }
}

/* ------------------------------------------------------------------------- */
/* Выполнение команд                                                         */
/* ------------------------------------------------------------------------- */

int run_cmd(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "Ошибка: не удаётся выполнить \'%s\': %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

int run_cmd_stdout(char *const argv[], const char *out_path) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        FILE *f = fopen(out_path, "w");
        if (!f) {
            fprintf(stderr, "Ошибка: не удаётся открыть \'%s\': %s\n", out_path, strerror(errno));
            _exit(127);
        }
        if (dup2(fileno(f), STDOUT_FILENO) < 0) _exit(127);
        fclose(f);
        execvp(argv[0], argv);
        fprintf(stderr, "Ошибка: не удаётся выполнить \'%s\': %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

/* ------------------------------------------------------------------------- */
/* Проверка наличия утилит в PATH                                              */
/* ------------------------------------------------------------------------- */

int tool_available(const char *tool) {
    if (!tool || !*tool) return 0;
    /* Проверяем, что строка содержит только разрешённые символы имени утилиты:
     * буквы, цифры, дефис, подчёркивание, точка, слэш. */
    for (const char *p = tool; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '/' && *p != '-' && *p != '_' && *p != '.') return 0;
    }
    char buf[4096];
    const char *path = getenv("PATH");
    if (!path) path = "/usr/local/bin:/usr/bin:/bin";
    char *dup = strdup(path);
    if (!dup) return 0;
    char *save = NULL;
    for (char *dir = strtok_r(dup, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        struct stat st;
        if (strchr(tool, '/')) {
            snprintf(buf, sizeof(buf), "%s", tool);
        } else {
            snprintf(buf, sizeof(buf), "%s/%s", dir, tool);
        }
        if (stat(buf, &st) == 0 && S_ISREG(st.st_mode) && access(buf, X_OK) == 0) {
            free(dup);
            return 1;
        }
    }
    free(dup);
    return 0;
}

int need_tool(const char *tool) {
    if (!tool_available(tool)) {
        fprintf(stderr, "Ошибка: требуемое средство '%s' не найдено в PATH\n", tool);
        return -1;
    }
    return 0;
}
