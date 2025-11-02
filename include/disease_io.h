/* disease_io.h */
#ifndef DISEASE_IO_H
#define DISEASE_IO_H
#include "config.h"   /* SimConfig */
#include "disease.h"  /* Disease */

#define DISEASES_FILE_PATH   "diseases.txt"
#define DISEASE_NAME_MAX     64
#define DISEASE_LIST_MAX     128

int disease_list_names(const char* path, char names[][DISEASE_NAME_MAX], int max_names, int* out_count);
/* Load the block under [name] into dz+cfg (keys optional; missing ones keep current values) */
int disease_load_by_name(const char* path, const char* name, Disease* dz, SimConfig* cfg);
/* Append a new section [name] with all current fields (overwrite policy: append; last wins) */
int disease_save_append(const char* path, const char* name, const Disease* dz, const SimConfig* cfg);

/* Returns 1 if a section [name] exists in file at path; 0 if not; -1 on error. */
int disease_name_exists(const char* path, const char* name);

/* Save with collision handling.
   name_io: in/out — may be updated if user chooses Rename.
   Returns: 1 on success (file written), 0 on cancel/error. */
int disease_save_with_prompt(const char* path,
    char* name_io,
    const Disease* dz,
    const SimConfig* cfg);

#endif
