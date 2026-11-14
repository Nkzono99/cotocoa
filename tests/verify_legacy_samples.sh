#!/bin/bash
# Build original samples without changing their source or public interfaces.
set -euo pipefail
repo=$(pwd)
destination="$repo/build/legacy-original"
mkdir -p "$destination/baseline"
baseline=8f705f8c6aa8e7c51b98f50c211dd360d247191a
git diff --exit-code "$baseline" -- include/ctca.h src/ctca_mod.f90 src/ctca_int8_mod.f90 src/ctca_f.c test
for source_file in src/ctca.c src/ctca_f.c include/ctca.h; do
    git show "$baseline:$source_file" > "$destination/baseline/${source_file##*/}"
done
mpiicc -shared -fPIC -I"$destination/baseline" \
    "$destination/baseline/ctca.c" "$destination/baseline/ctca_f.c" \
    -o "$destination/baseline/libctca_original.so"
nm -D --defined-only "$destination/baseline/libctca_original.so" | awk '{print $3}' | sort > "$destination/baseline-symbols.txt"
nm -D --defined-only build/libcotocoa.so | awk '{print $3}' | sort > "$destination/current-symbols.txt"
comm -23 "$destination/baseline-symbols.txt" "$destination/current-symbols.txt" > "$destination/missing-symbols.txt"
if test -s "$destination/missing-symbols.txt"; then
    cat "$destination/missing-symbols.txt"
    exit 1
fi
for source_file in test/sample1_*.c test/sample4_*.c; do
    executable=${source_file##*/}
    mpiicc -Iinclude "$source_file" -Lbuild -lcotocoa -Wl,-rpath,"$repo/build" -o "$destination/${executable%.c}"
done
for source_file in test/sample2_*.f90; do
    executable=${source_file##*/}
    mpiifort -Ibuild/modules "$source_file" build/libcotocoa_fortran.a \
        -Lbuild -lcotocoa -Wl,-rpath,"$repo/build" -o "$destination/${executable%.f90}"
done
echo 'PASS original public C/Fortran interfaces and samples unchanged; every baseline C symbol preserved'
