/*
 * Standalone driver for fuzz targets.
 *
 * When libFuzzer (clang -fsanitize=fuzzer) is unavailable, compile each
 * target with this file to get a plain main() that replays a corpus
 * directory. Lets GCC hosts smoke-test harnesses and regressions.
 */

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static int run_one(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "skip: cannot open %s\n", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) {
        fclose(f);
        return 0;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return 1;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    LLVMFuzzerTestOneInput(buf, got);
    free(buf);
    return 0;
}

static int run_dir(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "skip: cannot open dir %s\n", dir);
        return 0;
    }
    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        run_one(path);
        count++;
    }
    closedir(d);
    printf("standalone: replayed %d inputs from %s\n", count, dir);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        /* empty input as a baseline */
        LLVMFuzzerTestOneInput((const uint8_t *)"", 0);
        printf("standalone: no corpus supplied, ran empty input only\n");
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) != 0) {
            fprintf(stderr, "skip: %s (stat failed)\n", argv[i]);
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            run_dir(argv[i]);
        } else {
            run_one(argv[i]);
        }
    }
    return 0;
}
