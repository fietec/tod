#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <assert.h>

#include <cwalk.h>
#define CLAGS_IMPLEMENTATION
#include <clags.h>

#define MAX_LINE_LEN 4096
#define ALPHABET_SIZE 256

#define return_defer(value) do{result = (value); goto defer;}while(0)

bool in_paths(clags_list_t paths, const char *item)
{
    for (size_t i=0; i<paths.count; ++i){
        if (strcmp(item, clags_list_element(paths, char*, i)) == 0) return true;
    }
    return false;
}

void setup_shift_table(const char *needle, int needle_len, int shift_table[])
{
    for (int i = 0; i < ALPHABET_SIZE; i++) {
        shift_table[i] = needle_len;
    }
    for (int i = 0; i < needle_len - 1; i++) {
        shift_table[(unsigned char)needle[i]] = needle_len - i - 1;
    }
}

void search_line(const char *filename, const char *line, int line_len, const char *needle, int line_number)
{
    int needle_len = strlen(needle);
    if (needle_len == 0 || needle_len > line_len) return;

    int shift_table[ALPHABET_SIZE];
    setup_shift_table(needle, needle_len, shift_table);

    int i = 0;
    while (i <= line_len - needle_len) {
        int j = needle_len - 1;
        while (j >= 0 && needle[j] == line[i + j]) {
            j--;
        }
        if (j < 0) {
            int format_len = printf("%s:%d:%d: ", filename, line_number, i+1);
            printf("%s\n%*s^\n", line, format_len+i, "");
            i += needle_len;
        } else {
            unsigned char mismatched_char = line[i + needle_len - 1];
            i += shift_table[mismatched_char];
        }
    }
}

int search_file(const char *filename, const char *needle)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL){
        fprintf(stderr, "[ERROR] Could not open file '%s': %s!\n", filename, strerror(errno));
        return 1;
    }
    char line[MAX_LINE_LEN];
    int line_number = 1;
    
    while (fgets(line, sizeof(line), file)){
        size_t line_len = strlen(line);
        if (line_len > 0 && (line[line_len-1] == '\n' || line[line_len-1] == '\r')){
            line[--line_len] = '\0';
        }
        search_line(filename, line, line_len, needle, line_number);
        line_number++;
    }
    fclose(file);
    return 0;
}

int search_dir(const char *dirname, const char *needle, clags_list_t ignore)
{
    DIR *dir = opendir(dirname);
    if (dir == NULL){
        fprintf(stderr, "[ERROR] Could not open directory: '%s': %s!\n", dirname, strerror(errno));
        return 1;
    }
    
    struct dirent *entry;
    char item_path[FILENAME_MAX] = {0};
    while ((entry = readdir(dir))){
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (in_paths(ignore, entry->d_name)) continue;
        cwk_path_join(dirname, entry->d_name, item_path, sizeof(item_path));
        struct stat attr;
        if (stat(item_path, &attr) == -1){
            fprintf(stderr, "[ERROR] Could not access '%s': %s!\n", item_path, strerror(errno));
            continue;
        }
        if (S_ISDIR(attr.st_mode) && *entry->d_name != '.'){
            (void) search_dir(item_path, needle, ignore);
        }else if (S_ISREG(attr.st_mode)){
            (void) search_file(item_path, needle);
        }else{
            continue;
        }
    }
    closedir(dir);
    return 0;
}

clags_list_t input_paths = clags_path_list();
clags_list_t ignore_names = clags_list();
bool help = false;

clags_arg_t args[] = {
    clags_positional(&input_paths, "input_path", "the file or directory to search_in", .value_type=Clags_Path, .is_list=true),
    clags_option('i', "ignore", &ignore_names, "NAME", "a file or directory to ignore", .is_list=true),
    clags_flag_help(&help),
};

int main(int argc, char *argv[]) 
{
    int result = 0;
    const char *program_name = argv[0];
    clags_config_t config = clags_config(args);
    if (clags_parse(argc, argv, &config) != NULL){
        clags_usage(program_name, &config);
        return_defer(1);
    }
    if (help){
        clags_usage(program_name, &config);
        return_defer(0);
    }
    const char *needle = "TODO:"; // this line should pop up when you run tod on this directory
    for (size_t i=0; i<input_paths.count; ++i){
        char *input_path = clags_list_element(input_paths, char*, i);
        struct stat attrs;
        if (stat(input_path, &attrs) == -1) continue;
        if (S_ISREG(attrs.st_mode)){
            search_file(input_path, needle);
        } else if (S_ISDIR(attrs.st_mode)){
            search_dir(input_path, needle, ignore_names);
        } else {
            continue;
        }
    }

defer:
    clags_list_free(&ignore_names);
    return result;
}
