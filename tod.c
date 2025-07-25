#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#include <cwalk.h>

#define MAX_LINE_LEN 4096
#define ALPHABET_SIZE 256

void setup_shift_table(const char *needle, int needle_len, int shift_table[]) {
    for (int i = 0; i < ALPHABET_SIZE; i++) {
        shift_table[i] = needle_len;
    }
    for (int i = 0; i < needle_len - 1; i++) {
        shift_table[(unsigned char)needle[i]] = needle_len - i - 1;
    }
}

void search_line(const char *filename, const char *line, int line_len, const char *needle, int line_number) {
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

int search_dir(const char *dirname, const char *needle)
{
    DIR *dir = opendir(dirname);
    if (dir == NULL){
        fprintf(stderr, "[ERROR] Could not open directory: %s: %s!\n", dirname, strerror(errno));
        return 1;
    }
    
    struct dirent *entry;
    char item_path[FILENAME_MAX] = {0};
    while ((entry = readdir(dir))){
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        cwk_path_join(dirname, entry->d_name, item_path, sizeof(item_path));
        struct stat attr;
        if (stat(item_path, &attr) == -1){
            fprintf(stderr, "[ERROR] Could not access '%s': %s!\n", item_path, strerror(errno));
            continue;
        }
        if (S_ISDIR(attr.st_mode) && *entry->d_name != '.'){
            (void) search_dir(item_path, needle);
        }else if (S_ISREG(attr.st_mode)){
            (void) search_file(item_path, needle);
        }else{
            continue;
        }
    }
    closedir(dir);
    return 0;
}

int main(int argc, char *argv[]) 
{
    const char *program_name = argv[0];
    if (argc < 2){
        fprintf(stderr, "[ERROR] No directory provided!\n");
        printf("Usage: %s <dir>\n", program_name);
        return 1;
    }
    const char *dirpath = argv[1];
    return search_dir(dirpath, "TODO:");
}
