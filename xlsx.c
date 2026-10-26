#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */
/* Python-скрипт для чтения XLSX через openpyxl и вывод CSV в stdout            */
/* ------------------------------------------------------------------------- */

static const char *xlsx_to_csv_py =
"import sys, csv\n"
"import openpyxl\n"
"wb = openpyxl.load_workbook(sys.argv[1], data_only=True)\n"
"ws = wb.active\n"
"w = csv.writer(sys.stdout, lineterminator='\\n')\n"
"for row in ws.iter_rows(values_only=True):\n"
"    w.writerow([(str(c) if c is not None else '') for c in row])\n";

/* Новый скрипт: преобразование xlsx → Markdown с поддержкой всех листов */
static const char *xlsx_to_md_multi_py =
"import sys, openpyxl\n"
"wb = openpyxl.load_workbook(sys.argv[1], data_only=True)\n"
"for i, ws in enumerate(wb.worksheets):\n"
"    if i > 0:\n"
"        sys.stdout.write(\"\\n\")\n"
"    rows = list(ws.iter_rows(values_only=True))\n"
"    if not rows:\n"
"        continue\n"
"    header = [str(c) if c is not None else '' for c in rows[0]]\n"
"    sys.stdout.write(\"| \" + \" | \".join(header) + \" |\\n\")\n"
"    sys.stdout.write(\"| \" + \" | \".join([\"---\"] * len(header)) + \" |\\n\")\n"
"    for row in rows[1:]:\n"
"        cells = [str(c) if c is not None else '' for c in row]\n"
"        sys.stdout.write(\"| \" + \" | \".join(cells) + \" |\\n\")\n";

static const char *csv_to_xlsx_py =
"import sys, pandas as pd\n"
"df = pd.read_csv(sys.argv[1], dtype=str, keep_default_na=False)\n"
"df.to_excel(sys.argv[2], index=False)\n";

int csv_to_xlsx(const char *in, const char *out) {
    if (need_tool("python3") != 0) return -1;
    const char *args[] = { in, out, NULL };
    return run_python_script(csv_to_xlsx_py, args, 2);
}

/* Считывает CSV из stdout дочернего процесса в таблицу и записывает в out_path. */
int xlsx_to_csv(const char *in, const char *out) {
    char script[4096];
    if (work_path(script, sizeof(script), "xlsx_to_csv.py") != 0) return -1;
    FILE *f = fopen(script, "w");
    if (!f) return -1;
    fprintf(f, "%s", xlsx_to_csv_py);
    fclose(f);

    int p[2];
    if (pipe(p) != 0) { unlink(script); return -1; }
    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]); close(p[1]);
        unlink(script);
        return -1;
    }
    if (pid == 0) {
        close(p[0]);
        dup2(p[1], STDOUT_FILENO);
        close(p[1]);
        execlp("python3", "python3", script, in, NULL);
        _exit(127);
    }
    close(p[1]);
    FILE *outf = fopen(out, "w");
    if (!outf) {
        close(p[0]);
        unlink(script);
        return -1;
    }
    char buf[4096];
    ssize_t n;
    while ((n = read(p[0], buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, n, outf);
    }
    fclose(outf);
    close(p[0]);
    int status;
    waitpid(pid, &status, 0);
    unlink(script);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

int xlsx_to_md_multi(const char *in, const char *out) {
    char script[4096];
    if (work_path(script, sizeof(script), "xlsx_to_md_multi.py") != 0) return -1;
    FILE *f = fopen(script, "w");
    if (!f) return -1;
    fprintf(f, "%s", xlsx_to_md_multi_py);
    fclose(f);

    int p[2];
    if (pipe(p) != 0) { unlink(script); return -1; }
    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]); close(p[1]);
        unlink(script);
        return -1;
    }
    if (pid == 0) {
        close(p[0]);
        dup2(p[1], STDOUT_FILENO);
        close(p[1]);
        execlp("python3", "python3", script, in, NULL);
        _exit(127);
    }
    close(p[1]);
    FILE *outf = fopen(out, "w");
    if (!outf) {
        close(p[0]);
        unlink(script);
        return -1;
    }
    char buf[4096];
    ssize_t n;
    while ((n = read(p[0], buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, n, outf);
    }
    fclose(outf);
    close(p[0]);
    int status;
    waitpid(pid, &status, 0);
    unlink(script);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

/* ------------------------------------------------------------------------- */
/* Запуск Python-скрипта с аргументами                                       */
/* ------------------------------------------------------------------------- */

int run_python_script(const char *code, const char *const args[], int nargs) {
    char script[4096];
    if (work_path(script, sizeof(script), "helper.py") != 0) return -1;
    FILE *f = fopen(script, "w");
    if (!f) return -1;
    fprintf(f, "%s", code);
    fclose(f);

    char *argv[32];
    argv[0] = "python3";
    argv[1] = script;
    for (int i = 0; i < nargs; i++) argv[i + 2] = (char *)args[i];
    argv[nargs + 2] = NULL;

    int rc = run_cmd(argv);
    unlink(script);
    return rc;
}
