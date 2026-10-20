#!/bin/bash

# Пакетный OCR для отсканированных PDF
# Конвертирует PDF в DOCX через file-to-file

# Использование:
#   ./ocr_batch.sh [список_PDF]
# или
#   ./ocr_batch.sh < список_PDF

src="/home/user/MEGA/suvorov_disk/Клинреки_pdf/"
dst="/home/user/soft/file-to-file/converted_docx"

# Создаём каталог для результатов, если его нет
mkdir -p "$dst"

# Если передан список PDF через аргументы, используем его
if [ $# -gt 0 ]; then
    files=("$@")
else
    # Иначе читаем из stdin
    files=()
    while IFS= read -r line; do
        files+=("$line")
    done
fi

# Если /tmp/badfiles.tsv существует, добавляем его содержимое
if [ -f "/tmp/badfiles.tsv" ]; then
    while IFS=$'\t' read -r n rest; do
        files+=("$n.pdf")
    done < "/tmp/badfiles.tsv"
    rm -f "/tmp/badfiles.tsv"
fi

# Удаляем дубликаты, сохраняя имена с пробелами целиком
declare -A seen=()
uniq_files=()
for f in "${files[@]}"; do
    [ -z "$f" ] && continue
    if [ -z "${seen[$f]:-}" ]; then
        seen[$f]=1
        uniq_files+=("$f")
    fi
done
files=("${uniq_files[@]}")

if [ ${#files[@]} -eq 0 ]; then
    echo "Список PDF пуст: передайте файлы аргументами или через stdin." >&2
    exit 1
fi

# Запускаем OCR для каждого PDF
for ((i=0; i<${#files[@]}; i++)); do
    f="${files[i]}"
    # Аргументы могут приходить как абсолютными путями, так и именами внутри $src
    if [[ "$f" = /* ]]; then path="$f"; else path="$src/$f"; fi
    n=$(basename "$f" .pdf)
    echo -n "[$((i+1))/${#files[@]}] $n... "
    /home/user/soft/file-to-file/file-to-file "$path" "$dst/$n.docx" 2>"$dst/err_$n.txt"
    rc=$?
    size=$(stat -c %s "$dst/$n.docx" 2>/dev/null || echo none)
    echo "rc=$rc size=$size"
    [ $rc -eq 0 ] && rm -f "$dst/err_$n.txt"
done

# Итог
echo "=== ALL DONE ==="
