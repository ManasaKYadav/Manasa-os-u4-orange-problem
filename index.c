#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int remaining = index->count - i - 1;
            if (remaining > 0)
                memmove(&index->entries[i], &index->entries[i + 1],
                        remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

int index_status(const Index *index) {
    printf("Staged changes:\n");
    for (int i = 0; i < index->count; i++)
        printf("  staged:     %s\n", index->entries[i].path);
    printf("\n");

    printf("Unstaged changes:\n");
    for (int i = 0; i < index->count; i++) {
        struct stat st;
        if (stat(index->entries[i].path, &st) != 0) {
            printf("  deleted:    %s\n", index->entries[i].path);
        } else if (st.st_mtime != (time_t)index->entries[i].mtime_sec ||
                   st.st_size != (off_t)index->entries[i].size) {
            printf("  modified:   %s\n", index->entries[i].path);
        }
    }
    printf("\n");

    printf("Untracked files:\n");
    DIR *dir = opendir(".");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            if (strcmp(ent->d_name, ".pes") == 0) continue;
            if (strcmp(ent->d_name, "pes") == 0) continue;

            int tracked = 0;
            for (int i = 0; i < index->count; i++) {
                if (strcmp(index->entries[i].path, ent->d_name) == 0) {
                    tracked = 1;
                    break;
                }
            }

            if (!tracked) {
                struct stat st;
                stat(ent->d_name, &st);
                if (S_ISREG(st.st_mode))
                    printf("  untracked:  %s\n", ent->d_name);
            }
        }
        closedir(dir);
    }
    printf("\n");
    return 0;
}

// ─── IMPLEMENTATION ─────────────────────────────────────────────────────────

int index_load(Index *index) {
    FILE *f = fopen(".pes/index", "r");
    if (!f) {
        index->count = 0;
        return 0;
    }

    index->count = 0;

    while (1) {
        IndexEntry entry;
        char hash_hex[HASH_HEX_SIZE + 1];

        if (fscanf(f, "%o %64s %ld %ld %s",
                   &entry.mode,
                   hash_hex,
                   &entry.mtime_sec,
                   &entry.size,
                   entry.path) != 5)
            break;

        hex_to_hash(hash_hex, &entry.id);
        index->entries[index->count++] = entry;
    }

    fclose(f);
    return 0;
}

int compare_entries(const void *a, const void *b) {
    return strcmp(((IndexEntry*)a)->path, ((IndexEntry*)b)->path);
}

int index_save(const Index *index) {
    FILE *f = fopen(".pes/index.tmp", "w");
    if (!f) return -1;

    Index temp = *index;
    qsort(temp.entries, temp.count, sizeof(IndexEntry), compare_entries);

    for (int i = 0; i < temp.count; i++) {
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&temp.entries[i].id, hex);

        fprintf(f, "%o %s %ld %ld %s\n",
                temp.entries[i].mode,
                hex,
                temp.entries[i].mtime_sec,
                temp.entries[i].size,
                temp.entries[i].path);
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    rename(".pes/index.tmp", ".pes/index");
    return 0;
}

int index_add(Index *index, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    rewind(f);

    char *data = malloc(size);
    if (!data) {
        fclose(f);
        return -1;
    }

    fread(data, 1, size, f);
    fclose(f);

    ObjectID id;
    if (object_write(OBJ_BLOB, data, size, &id) != 0) {
        free(data);
        return -1;
    }

    free(data);

    struct stat st;
    if (stat(path, &st) != 0) return -1;

    IndexEntry *e = index_find(index, path);

    if (e) {
        e->id = id;
        e->mtime_sec = st.st_mtime;
        e->size = st.st_size;
        e->mode = st.st_mode;
    } else {
        IndexEntry new_entry;
        new_entry.id = id;
        new_entry.mtime_sec = st.st_mtime;
        new_entry.size = st.st_size;
        new_entry.mode = st.st_mode;
        strcpy(new_entry.path, path);

        index->entries[index->count++] = new_entry;
    }

    return index_save(index);
}