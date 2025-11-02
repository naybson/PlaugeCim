// disease_io.c — load/save disease presets with overwrite/rename prompt

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "disease_io.h"
#include "utils.h"     /* skip_bom(FILE*) */
#include "config.h"
#include "disease.h"

/* ========================== Small local helpers ========================== */

#define LINE_CAP  1024  /* generous, keeps fgets simple */

/* Copy C-string with truncation to fit cap (always NUL-terminated). */
static int cstr_copy_trunc(char* dst, int cap, const char* src)
{
    if (dst == NULL) return 0;
    if (cap <= 0) return 0;

    if (src == NULL) {
        dst[0] = '\0';
        return 1;
    }

    int i = 0;
    while (i < cap - 1 && src[i] != '\0') {
        dst[i] = src[i];
        i += 1;
    }
    dst[i] = '\0';
    return 1;
}

/* Trim leading and trailing whitespace (spaces, tabs, CRLF). Modifies s. */
static void trim_inplace(char* s)
{
    if (s == NULL) return;

    int len = (int)strlen(s);
    int start = 0;
    while (start < len && isspace((unsigned char)s[start]) != 0) {
        start += 1;
    }

    int end = len;
    while (end > start && isspace((unsigned char)s[end - 1]) != 0) {
        end -= 1;
    }

    if (start > 0) {
        /* move the trimmed span to the front */
        memmove(s, s + start, (size_t)(end - start));
    }

    s[end - start] = '\0';
}

/* Remove trailing comment that starts with '#' (standard INI-style). */
static void strip_comment_inplace(char* s)
{
    if (s == NULL) return;

    char* p = strchr(s, '#');
    if (p != NULL) {
        *p = '\0';
    }
}

/* Detect section header line "[Name]"; copy Name into buf if found. */
static int try_parse_section(const char* line, char* buf, int cap)
{
    if (line == NULL) return 0;
    if (buf == NULL) return 0;
    if (cap <= 0) return 0;

    const char* p = line;

    /* skip leading spaces/tabs only (keep simple; trim was done by caller) */
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    if (*p != '[') {
        return 0;
    }

    const char* close = strchr(p + 1, ']');
    if (close == NULL) {
        return 0;
    }

    int name_len = (int)(close - (p + 1));
    if (name_len <= 0) {
        return 0;
    }
    if (name_len >= cap) {
        /* too long to fit; reject to avoid truncation surprises */
        return 0;
    }

    memcpy(buf, p + 1, (size_t)name_len);
    buf[name_len] = '\0';
    return 1;
}

/* Split "key=value" into key and value pointers (both trimmed). Returns 1 on success. */
static int split_key_value(char* line, char** out_key, char** out_val)
{
    if (line == NULL) return 0;
    if (out_key == NULL) return 0;
    if (out_val == NULL) return 0;

    char* eq = strchr(line, '=');
    if (eq == NULL) {
        return 0;
    }

    *eq = '\0';
    char* key = line;
    char* val = eq + 1;

    strip_comment_inplace(key);
    strip_comment_inplace(val);
    trim_inplace(key);
    trim_inplace(val);

    if (key[0] == '\0') return 0;
    if (val[0] == '\0') return 0;

    *out_key = key;
    *out_val = val;
    return 1;
}

/* Read a line from stdin, strip trailing '\n', always NUL-terminate. */
static void read_line_stdin(char* buf, int cap)
{
    if (buf == NULL) return;
    if (cap <= 0) return;

    buf[0] = '\0';

    if (fgets(buf, cap, stdin) == NULL) {
        buf[0] = '\0';
        return;
    }

    int n = (int)strlen(buf);
    if (n > 0) {
        if (buf[n - 1] == '\n') {
            buf[n - 1] = '\0';
        }
    }
}

/* Very basic name validation: non-empty, and no '[' or ']' to avoid INI breakage. */
static int is_valid_disease_name(const char* s)
{
    if (s == NULL) return 0;
    if (s[0] == '\0') return 0;

    const char* p = s;
    while (*p != '\0') {
        if (*p == '[') return 0;
        if (*p == ']') return 0;
        p += 1;
    }
    return 1;
}

/* ========================== Key mapping for loader ========================== */

typedef enum { K_DBL, K_INT, K_UINT } KeyKind;

typedef struct {
    const char* name;
    KeyKind     kind;
    void* ptr;
} KeyEntry;

/* Build the key map that points into dz+cfg fields. */
static void build_key_map(KeyEntry* out, int* outN, Disease* dz, SimConfig* cfg)
{
    int i = 0;

    /* Disease fundamentals */
    out[i++] = (KeyEntry){ "beta_within", K_DBL, &dz->beta_within };
    out[i++] = (KeyEntry){ "beta_neighbors", K_DBL, &dz->beta_neighbors };
    out[i++] = (KeyEntry){ "gamma_recover", K_DBL, &dz->gamma_recover };
    out[i++] = (KeyEntry){ "mu_mortality", K_DBL, &dz->mu_mortality };

    /* Multipliers & ports & rng & mutations */
    out[i++] = (KeyEntry){ "symptom_mult_within", K_DBL, &dz->symptom_mult_within };
    out[i++] = (KeyEntry){ "symptom_mult_neighbor", K_DBL, &dz->symptom_mult_neighbor };
    out[i++] = (KeyEntry){ "symptom_mult_mortality", K_DBL, &dz->symptom_mult_mortality };
    out[i++] = (KeyEntry){ "death_slowdown_k", K_DBL, &dz->death_supp_k };
    out[i++] = (KeyEntry){ "port_shutdown_pct", K_INT, &dz->port_shutdown_pct };
    out[i++] = (KeyEntry){ "rng_seed", K_UINT, &dz->rng_seed };
    out[i++] = (KeyEntry){ "mutations_enabled", K_INT, &dz->mutations_enabled };
    out[i++] = (KeyEntry){ "mutation_chance_pct", K_DBL, &cfg->mutation_chance_pct };

    /* Awareness & cure */
    out[i++] = (KeyEntry){ "cure.base_awareness_per_tick", K_DBL, &cfg->cure.base_awareness_per_tick };
    out[i++] = (KeyEntry){ "cure.k_infected_awareness", K_DBL, &cfg->cure.k_infected_awareness };
    out[i++] = (KeyEntry){ "cure.k_death_awareness", K_DBL, &cfg->cure.k_death_awareness };
    out[i++] = (KeyEntry){ "cure.progress_per_awareness", K_DBL, &cfg->cure.progress_per_awareness };
    out[i++] = (KeyEntry){ "cure.death_slowdown_k", K_DBL, &cfg->cure.death_slowdown_k };
    out[i++] = (KeyEntry){ "cure.post_cure_gamma_recover", K_DBL, &cfg->cure.post_cure_gamma_recover };

    *outN = i;
}

/* Apply parsed string value to the target pointer according to kind. */
static int apply_value(KeyKind kind, void* ptr, const char* val)
{
    if (ptr == NULL) return 0;
    if (val == NULL) return 0;

    if (kind == K_DBL) {
        char* endp = NULL;
        double d = strtod(val, &endp);
        if (endp == val) return 0;
        *(double*)ptr = d;
        return 1;
    }

    if (kind == K_INT) {
        char* endp = NULL;
        long v = strtol(val, &endp, 10);
        if (endp == val) return 0;
        *(int*)ptr = (int)v;
        return 1;
    }

    if (kind == K_UINT) {
        char* endp = NULL;
        unsigned long v = strtoul(val, &endp, 10);
        if (endp == val) return 0;
        *(unsigned*)ptr = (unsigned)v;
        return 1;
    }

    return 0;
}

/* Find key in the map; returns index or -1. */
static int key_lookup(const KeyEntry* map, int n, const char* key)
{
    if (map == NULL) return -1;
    if (key == NULL) return -1;

    int i = 0;
    while (i < n) {
        if (strcmp(map[i].name, key) == 0) {
            return i;
        }
        i += 1;
    }
    return -1;
}

/* ============================= Public API ============================== */

/* Return 1 if section [name] exists, 0 if not, -1 on error (e.g., bad args). */
int disease_name_exists(const char* path, const char* name)
{
    if (path == NULL) return -1;
    if (name == NULL) return -1;

    FILE* f = fopen(path, "r");
    if (f == NULL) {
        /* No file yet → definitely does not exist */
        return 0;
    }

    skip_bom(f);

    char line[LINE_CAP];
    char sec[DISEASE_NAME_MAX];

    while (fgets(line, sizeof(line), f) != NULL) {
        strip_comment_inplace(line);
        trim_inplace(line);
        if (line[0] == '\0') {
            continue;
        }
        if (try_parse_section(line, sec, (int)sizeof(sec)) != 0) {
            if (strcmp(sec, name) == 0) {
                fclose(f);
                return 1; /* found */
            }
        }
    }

    fclose(f);
    return 0; /* not found */
}

int disease_list_names(const char* path, char names[][DISEASE_NAME_MAX], int max_names, int* out_count)
{
    if (out_count != NULL) {
        *out_count = 0;
    }

    if (path == NULL) {
        return 0;
    }
    if (names == NULL) {
        return 0;
    }
    if (max_names <= 0) {
        return 0;
    }

    FILE* f = fopen(path, "r");
    if (f == NULL) {
        /* No file yet: treat as empty list, not an error for the UI. */
        if (out_count != NULL) {
            *out_count = 0;
        }
        return 1;
    }

    skip_bom(f);

    char line[LINE_CAP];
    int count = 0;

    while (fgets(line, sizeof(line), f) != NULL) {
        strip_comment_inplace(line);
        trim_inplace(line);
        if (line[0] == '\0') {
            continue;
        }

        char sec[DISEASE_NAME_MAX];
        if (try_parse_section(line, sec, (int)sizeof(sec)) != 0) {
            if (count < max_names) {
                /* Guaranteed NUL fit because try_parse_section enforced size */
                (void)cstr_copy_trunc(names[count], DISEASE_NAME_MAX, sec);
                count += 1;
            }
            else {
                /* silently stop collecting more to avoid overflow */
                break;
            }
        }
    }

    fclose(f);

    if (out_count != NULL) {
        *out_count = count;
    }
    return 1;
}

int disease_load_by_name(const char* path, const char* name, Disease* dz, SimConfig* cfg)
{
    if (path == NULL) return 0;
    if (name == NULL) return 0;
    if (dz == NULL || cfg == NULL) return 0;

    FILE* f = fopen(path, "r");
    if (f == NULL) {
        return 0;
    }

    skip_bom(f);

    KeyEntry map[32];
    int mapN = 0;
    build_key_map(map, &mapN, dz, cfg);

    char line[LINE_CAP];
    int in_target = 0;
    int applied_any = 0;

    while (fgets(line, sizeof(line), f) != NULL) {
        strip_comment_inplace(line);
        trim_inplace(line);
        if (line[0] == '\0') {
            continue;
        }

        /* Section header? */
        char sec[DISEASE_NAME_MAX];
        if (try_parse_section(line, sec, (int)sizeof(sec)) != 0) {
            /* If we were in target and a new section starts, we are done. */
            if (in_target != 0) {
                break;
            }
            if (strcmp(sec, name) == 0) {
                in_target = 1;
            }
            else {
                in_target = 0;
            }
            continue;
        }

        /* Only apply key/values inside the target section. */
        if (in_target != 0) {
            char* key = NULL;
            char* val = NULL;
            if (split_key_value(line, &key, &val) != 0) {
                int idx = key_lookup(map, mapN, key);
                if (idx >= 0) {
                    if (apply_value(map[idx].kind, map[idx].ptr, val) != 0) {
                        applied_any = 1;
                    }
                }
            }
        }
    }

    fclose(f);
    return applied_any;
}

int disease_save_append(const char* path, const char* name, const Disease* dz, const SimConfig* cfg)
{
    if (path == NULL) return 0;
    if (name == NULL) return 0;
    if (dz == NULL || cfg == NULL) return 0;

    FILE* f = fopen(path, "a");
    if (f == NULL) {
        perror("fopen");
        return 0;
    }

    /* Write a full, consistent block (values formatted like setup UI). */
    fprintf(f, "\n[%s]\n", name);

    /* Disease fundamentals */
    fprintf(f, "beta_within=%.6g\n", dz->beta_within);
    fprintf(f, "beta_neighbors=%.6g\n", dz->beta_neighbors);
    fprintf(f, "gamma_recover=%.6g\n", dz->gamma_recover);
    fprintf(f, "mu_mortality=%.6g\n", dz->mu_mortality);

    /* Multipliers & ports & rng & mutations */
    fprintf(f, "symptom_mult_within=%.6g\n", dz->symptom_mult_within);
    fprintf(f, "symptom_mult_neighbor=%.6g\n", dz->symptom_mult_neighbor);
    fprintf(f, "symptom_mult_mortality=%.6g\n", dz->symptom_mult_mortality);
    fprintf(f, "death_slowdown_k=%.6g\n", dz->death_supp_k);
    fprintf(f, "port_shutdown_pct=%d\n", dz->port_shutdown_pct);
    fprintf(f, "rng_seed=%u\n", dz->rng_seed);
    fprintf(f, "mutations_enabled=%d\n", dz->mutations_enabled);
    fprintf(f, "mutation_chance_pct=%.6g\n", cfg->mutation_chance_pct);

    /* Awareness & cure */
    fprintf(f, "cure.base_awareness_per_tick=%.6g\n", cfg->cure.base_awareness_per_tick);
    fprintf(f, "cure.k_infected_awareness=%.6g\n", cfg->cure.k_infected_awareness);
    fprintf(f, "cure.k_death_awareness=%.6g\n", cfg->cure.k_death_awareness);
    fprintf(f, "cure.progress_per_awareness=%.6g\n", cfg->cure.progress_per_awareness);
    fprintf(f, "cure.death_slowdown_k=%.6g\n", cfg->cure.death_slowdown_k);
    fprintf(f, "cure.post_cure_gamma_recover=%.6g\n", cfg->cure.post_cure_gamma_recover);

    fclose(f);
    return 1;
}

/* ===================== Save with Overwrite/Rename/Cancel prompt ===================== */
/*
   disease_save_with_prompt:
     - If 'name_io' does not exist → append and return 1.
     - If exists → prompt:
           O) Overwrite (append new block with same section; last block wins on load)
           R) Rename (ask for unique, valid name; then append)
           C) Cancel  (return 0)
     - name_io is IN/OUT and will be updated if user renames.
*/
int disease_save_with_prompt(const char* path,
    char* name_io,
    const Disease* dz,
    const SimConfig* cfg)
{
    if (path == NULL) return 0;
    if (name_io == NULL) return 0;
    if (dz == NULL) return 0;
    if (cfg == NULL) return 0;

    int exists = disease_name_exists(path, name_io);
    if (exists < 0) {
        /* error checking names */
        return 0;
    }
    if (exists == 0) {
        /* unique → save directly */
        return disease_save_append(path, name_io, dz, cfg);
    }

    /* Name exists → ask user what to do */
    while (1) {
        printf("\n=========================================\n");
        printf(" A disease named \"%s\" already exists.\n", name_io);
        printf(" What do you want to do?\n");
        printf("   O) Overwrite the existing one\n");
        printf("   R) Rename and save as a new entry\n");
        printf("   C) Cancel\n");
        printf(" Enter choice [O/R/C]: ");
        fflush(stdout);

        char choice[16];
        read_line_stdin(choice, (int)sizeof(choice));

        /* Normalize to uppercase first letter */
        char ch = '\0';
        if (choice[0] != '\0') {
            ch = (char)toupper((unsigned char)choice[0]);
        }
		// overwrite , the exsisting name
        if (ch == 'O') {
            /* Simple overwrite policy:
               We append a new block with the same section name.
               Loader will use the last block (your loader stops at next section,
               so later block effectively overwrites earlier ones). */
            printf("Overwriting \"%s\"...\n", name_io);
            fflush(stdout);
            return disease_save_append(path, name_io, dz, cfg);
        }

        // Rename current disease
        if (ch == 'R') {
            /* Loop until user provides a valid, unique name or backs out */
            while (1) {
                char newname[DISEASE_NAME_MAX];

                printf("Enter a new name (no '[' or ']'), or leave empty to cancel: ");
                fflush(stdout);
                read_line_stdin(newname, (int)sizeof(newname));
                trim_inplace(newname);

                if (newname[0] == '\0') {
                    /* user canceled rename → go back to O/R/C menu */
                    break;
                }

                if (is_valid_disease_name(newname) == 0) {
                    printf("Invalid name. Please avoid '[' and ']' and use a non-empty name.\n");
                    continue;
                }

                int e2 = disease_name_exists(path, newname);
                if (e2 < 0) {
                    printf("Error checking names. Aborting.\n");
                    return 0;
                }
                if (e2 == 1) {
                    printf("The name \"%s\" already exists. Try another.\n", newname);
                    continue;
                }

                /* Good name → update caller buffer and save */
                (void)cstr_copy_trunc(name_io, DISEASE_NAME_MAX, newname);
                printf("Saving as \"%s\"...\n", name_io);
                fflush(stdout);
                return disease_save_append(path, name_io, dz, cfg);
            }

            /* fallthrough: back to O/R/C prompt */
            continue;
        }

        // cancel
        if (ch == 'C') {
            printf("Cancelled.\n");
            return 0;
        }

        /* Unrecognized input → prompt again */
        printf("Invalid choice. Please enter O, R, or C.\n");
    }
}
