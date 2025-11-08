/**
 * ==============================================================================
 * EventChains Build System - Dependency Resolution Implementation
 * ==============================================================================
 */

#include "dependency_resolver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

/* Platform-specific includes for directory iteration */
#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define stat _stat
    #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
    #define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#else
    #include <dirent.h>
    #include <unistd.h>
#endif

/* ==============================================================================
 * Error Strings
 * ==============================================================================
 */

static const char *error_strings[] = {
    "Success",
    "NULL pointer provided",
    "File not found",
    "Parse failed",
    "Circular dependency detected",
    "Too many source files",
    "Too many includes",
    "Out of memory",
    "Invalid path",
    "Topological sort failed"
};

const char *dependency_error_string(DependencyErrorCode code) {
    if (code >= 0 && code <= DEP_ERROR_TOPOLOGICAL_SORT_FAILED) {
        return error_strings[code];
    }
    return "Unknown error";
}

/* ==============================================================================
 * Utility Functions
 * ==============================================================================
 */

/**
 * Check if a path is a C/C++ source file
 */
static bool is_source_file(const char *path) {
    if (!path) return false;

    size_t len = strlen(path);
    if (len < 3) return false;

    const char *ext = path + len - 2;
    return strcmp(ext, ".c") == 0 ||
           strcmp(ext, ".h") == 0 ||
           (len >= 4 && strcmp(path + len - 4, ".cpp") == 0) ||
           (len >= 4 && strcmp(path + len - 4, ".hpp") == 0) ||
           (len >= 3 && strcmp(path + len - 3, ".cc") == 0);
}

/**
 * Check if a file is a header
 */
static bool is_header_file(const char *path) {
    if (!path) return false;

    size_t len = strlen(path);
    if (len < 3) return false;

    return (len >= 2 && strcmp(path + len - 2, ".h") == 0) ||
           (len >= 4 && strcmp(path + len - 4, ".hpp") == 0);
}

/**
 * Normalize a path (resolve relative components and separators)
 */
static void normalize_path(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;

    /* Copy and normalize path separators */
    size_t i;
    for (i = 0; i < dest_size - 1 && src[i]; i++) {
#ifdef _WIN32
        /* On Windows, normalize all separators to backslash */
        dest[i] = (src[i] == '/') ? '\\' : src[i];
#else
        /* On Unix, normalize all separators to forward slash */
        dest[i] = (src[i] == '\\') ? '/' : src[i];
#endif
    }
    dest[i] = '\0';
}

/**
 * Check if a file exists
 */
static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/**
 * Resolve an include path relative to include directories
 */
static bool resolve_include(
    const DependencyGraph *graph,
    const char *include_name,
    const char *referencing_file,
    char *resolved_path,
    size_t path_size
) {
    /* Try as absolute or relative to referencing file */
    if (include_name[0] == '"') {
        /* Local include - try relative to referencing file */
        char dir[MAX_PATH_LENGTH];
        strncpy(dir, referencing_file, MAX_PATH_LENGTH - 1);
        dir[MAX_PATH_LENGTH - 1] = '\0';

        char *last_slash = strrchr(dir, '/');
        if (last_slash) {
            *(last_slash + 1) = '\0';
            snprintf(resolved_path, path_size, "%s%s", dir, include_name);
            if (file_exists(resolved_path)) {
                return true;
            }
        }
    }

    /* Try include paths */
    for (size_t i = 0; i < graph->include_path_count; i++) {
        snprintf(resolved_path, path_size, "%s/%s",
                graph->include_paths[i], include_name);
        if (file_exists(resolved_path)) {
            return true;
        }
    }

    /* Try current directory */
    if (file_exists(include_name)) {
        strncpy(resolved_path, include_name, path_size - 1);
        resolved_path[path_size - 1] = '\0';
        return true;
    }

    return false;
}

/* ==============================================================================
 * Source File Management
 * ==============================================================================
 */

/**
 * Create a new source file
 */
static SourceFile *source_file_create(const char *path) {
    SourceFile *file = malloc(sizeof(SourceFile));
    if (!file) return NULL;

    normalize_path(file->path, path, MAX_PATH_LENGTH);
    file->include_count = 0;
    file->is_header = is_header_file(path);
    file->visited = false;
    file->in_stack = false;
    file->sort_order = -1;

    return file;
}

/**
 * Destroy a source file
 */
static void source_file_destroy(SourceFile *file) {
    if (!file) return;

    for (size_t i = 0; i < file->include_count; i++) {
        free(file->includes[i]);
    }

    free(file);
}

/**
 * Add an include to a source file
 */
static DependencyErrorCode source_file_add_include(
    SourceFile *file,
    const char *include_path
) {
    if (!file || !include_path) return DEP_ERROR_NULL_POINTER;

    if (file->include_count >= MAX_INCLUDES_PER_FILE) {
        return DEP_ERROR_TOO_MANY_INCLUDES;
    }

    char *include_copy = strdup(include_path);
    if (!include_copy) return DEP_ERROR_OUT_OF_MEMORY;

    file->includes[file->include_count++] = include_copy;
    return DEP_SUCCESS;
}

/**
 * Parse #include directives from a source file
 */
static DependencyErrorCode parse_includes(
    DependencyGraph *graph,
    SourceFile *file
) {
    FILE *fp = fopen(file->path, "r");
    if (!fp) return DEP_ERROR_FILE_NOT_FOUND;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        /* Skip leading whitespace */
        char *p = line;
        while (*p && isspace(*p)) p++;

        /* Check for #include */
        if (*p != '#') continue;
        p++;
        while (*p && isspace(*p)) p++;

        if (strncmp(p, "include", 7) != 0) continue;
        p += 7;
        while (*p && isspace(*p)) p++;

        /* Extract include name */
        char include_name[MAX_PATH_LENGTH];
        char *dest = include_name;

        if (*p == '"' || *p == '<') {
            char end_char = (*p == '"') ? '"' : '>';
            p++; /* Skip opening quote/bracket */

            while (*p && *p != end_char && dest < include_name + MAX_PATH_LENGTH - 1) {
                *dest++ = *p++;
            }
            *dest = '\0';

            /* Resolve include path */
            char resolved[MAX_PATH_LENGTH];
            if (resolve_include(graph, include_name, file->path,
                              resolved, MAX_PATH_LENGTH)) {
                DependencyErrorCode err = source_file_add_include(file, resolved);
                if (err != DEP_SUCCESS) {
                    fclose(fp);
                    return err;
                }
            }
            /* If we can't resolve, that's okay - might be system header */
        }
    }

    fclose(fp);
    return DEP_SUCCESS;
}

/**
 * Check if a source file contains a main() function
 */
static bool has_main_function(const SourceFile *file) {
    if (!file || file->is_header) return false;

    FILE *fp = fopen(file->path, "r");
    if (!fp) return false;

    char line[1024];
    bool found = false;

    while (fgets(line, sizeof(line), fp)) {
        /* Simple heuristic: look for "int main" or "void main" */
        if (strstr(line, "int main") || strstr(line, "void main")) {
            /* Check it's not in a comment or string */
            /* TODO: More sophisticated parsing */
            found = true;
            break;
        }
    }

    fclose(fp);
    return found;
}

/* ==============================================================================
 * Dependency Graph Management
 * ==============================================================================
 */

DependencyGraph *dependency_graph_create(void) {
    DependencyGraph *graph = malloc(sizeof(DependencyGraph));
    if (!graph) return NULL;

    graph->file_count = 0;
    graph->include_path_count = 0;

    return graph;
}

void dependency_graph_destroy(DependencyGraph *graph) {
    if (!graph) return;

    for (size_t i = 0; i < graph->file_count; i++) {
        source_file_destroy(graph->files[i]);
    }

    for (size_t i = 0; i < graph->include_path_count; i++) {
        free(graph->include_paths[i]);
    }

    free(graph);
}

DependencyErrorCode dependency_graph_add_include_path(
    DependencyGraph *graph,
    const char *path
) {
    if (!graph || !path) return DEP_ERROR_NULL_POINTER;

    if (graph->include_path_count >= MAX_INCLUDE_PATHS) {
        return DEP_ERROR_TOO_MANY_INCLUDES;
    }

    char *path_copy = strdup(path);
    if (!path_copy) return DEP_ERROR_OUT_OF_MEMORY;

    graph->include_paths[graph->include_path_count++] = path_copy;
    return DEP_SUCCESS;
}

SourceFile *dependency_graph_find_file(
    const DependencyGraph *graph,
    const char *path
) {
    if (!graph || !path) return NULL;

    for (size_t i = 0; i < graph->file_count; i++) {
        if (strcmp(graph->files[i]->path, path) == 0) {
            return graph->files[i];
        }
    }

    return NULL;
}

DependencyErrorCode dependency_graph_add_file(
    DependencyGraph *graph,
    const char *file_path
) {
    if (!graph || !file_path) return DEP_ERROR_NULL_POINTER;

    if (!file_exists(file_path)) return DEP_ERROR_FILE_NOT_FOUND;

    if (!is_source_file(file_path)) return DEP_ERROR_INVALID_PATH;

    /* Check if already added */
    if (dependency_graph_find_file(graph, file_path)) {
        return DEP_SUCCESS; /* Already added */
    }

    if (graph->file_count >= MAX_SOURCE_FILES) {
        return DEP_ERROR_TOO_MANY_FILES;
    }

    /* Create source file */
    SourceFile *file = source_file_create(file_path);
    if (!file) return DEP_ERROR_OUT_OF_MEMORY;

    /* Parse includes */
    DependencyErrorCode err = parse_includes(graph, file);
    if (err != DEP_SUCCESS) {
        source_file_destroy(file);
        return err;
    }

    /* Add to graph */
    graph->files[graph->file_count++] = file;

    /* Recursively add included files */
    for (size_t i = 0; i < file->include_count; i++) {
        dependency_graph_add_file(graph, file->includes[i]);
        /* Ignore errors for system headers we can't find */
    }

    return DEP_SUCCESS;
}

/* ==============================================================================
 * Directory Exclusion Support
 * ==============================================================================
 */

/**
 * Default directories to always exclude
 */
static const char *DEFAULT_EXCLUSIONS[] = {
    "build",
    "builds",
    ".git",
    ".svn",
    ".hg",
    "node_modules",
    "vendor",
    "__pycache__",
    ".eventchains",
    "CMakeFiles",
    ".vs",
    ".vscode",
    ".idea",
    NULL
};

/**
 * Check if a directory should be excluded
 */
static bool should_exclude_directory(
    const char *dir_name,
    const char **exclude_dirs,
    size_t exclude_count
) {
    if (!dir_name) return false;

    /* Extract just the directory name (not full path) */
    const char *base_name = strrchr(dir_name, '/');
    if (!base_name) {
        base_name = strrchr(dir_name, '\\');
    }
    if (base_name) {
        base_name++; /* Skip the slash */
    } else {
        base_name = dir_name;
    }

    /* Check against default exclusions */
    for (size_t i = 0; DEFAULT_EXCLUSIONS[i] != NULL; i++) {
        if (strcmp(base_name, DEFAULT_EXCLUSIONS[i]) == 0) {
            return true;
        }
    }

    /* Check against user-provided exclusions */
    if (exclude_dirs) {
        for (size_t i = 0; i < exclude_count; i++) {
            if (strcmp(base_name, exclude_dirs[i]) == 0) {
                return true;
            }
        }
    }

    return false;
}

DependencyErrorCode dependency_graph_scan_directory(
    DependencyGraph *graph,
    const char *directory,
    bool recursive
) {
    return dependency_graph_scan_directory_with_exclusions(
        graph, directory, recursive, NULL, 0
    );
}

DependencyErrorCode dependency_graph_scan_directory_with_exclusions(
    DependencyGraph *graph,
    const char *directory,
    bool recursive,
    const char **exclude_dirs,
    size_t exclude_count
) {
    if (!graph || !directory) return DEP_ERROR_NULL_POINTER;

#ifdef _WIN32
    /* Windows implementation using FindFirstFile/FindNextFile */
    WIN32_FIND_DATA find_data;
    char search_path[MAX_PATH_LENGTH];
    snprintf(search_path, MAX_PATH_LENGTH, "%s\\*", directory);

    HANDLE hFind = FindFirstFile(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        return DEP_ERROR_FILE_NOT_FOUND;
    }

    do {
        /* Skip . and .. */
        if (strcmp(find_data.cFileName, ".") == 0 ||
            strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }

        char full_path[MAX_PATH_LENGTH];
        snprintf(full_path, MAX_PATH_LENGTH, "%s\\%s", directory, find_data.cFileName);

        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            /* Check if directory should be excluded */
            if (should_exclude_directory(find_data.cFileName, exclude_dirs, exclude_count)) {
                continue;
            }

            if (recursive) {
                dependency_graph_scan_directory_with_exclusions(
                    graph, full_path, true, exclude_dirs, exclude_count
                );
            }
        } else if (is_source_file(full_path)) {
            dependency_graph_add_file(graph, full_path);
        }
    } while (FindNextFile(hFind, &find_data) != 0);

    FindClose(hFind);

#else
    /* POSIX implementation using opendir/readdir */
    DIR *dir = opendir(directory);
    if (!dir) return DEP_ERROR_FILE_NOT_FOUND;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[MAX_PATH_LENGTH];
        snprintf(full_path, MAX_PATH_LENGTH, "%s/%s", directory, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            /* Check if directory should be excluded */
            if (should_exclude_directory(entry->d_name, exclude_dirs, exclude_count)) {
                continue;
            }

            if (recursive) {
                dependency_graph_scan_directory_with_exclusions(
                    graph, full_path, true, exclude_dirs, exclude_count
                );
            }
        } else if (S_ISREG(st.st_mode) && is_source_file(full_path)) {
            dependency_graph_add_file(graph, full_path);
        }
    }

    closedir(dir);
#endif

    return DEP_SUCCESS;
}

/* ==============================================================================
 * Topological Sort Implementation
 * ==============================================================================
 */

/**
 * DFS helper for topological sort
 */
static bool topological_sort_dfs(
    DependencyGraph *graph,
    SourceFile *file,
    BuildOrder *order,
    char *cycle_path,
    size_t path_size
) {
    file->visited = true;
    file->in_stack = true;

    /* Visit dependencies */
    for (size_t i = 0; i < file->include_count; i++) {
        SourceFile *dep = dependency_graph_find_file(graph, file->includes[i]);
        if (!dep) continue; /* System header or not found */

        if (dep->in_stack) {
            /* Cycle detected */
            if (cycle_path) {
                snprintf(cycle_path, path_size, "%s -> %s",
                        file->path, dep->path);
            }
            return false;
        }

        if (!dep->visited) {
            if (!topological_sort_dfs(graph, dep, order, cycle_path, path_size)) {
                return false;
            }
        }
    }

    file->in_stack = false;

    /* Add to build order (reverse postorder) */
    file->sort_order = order->file_count;
    order->ordered_files[order->file_count++] = file;

    return true;
}

DependencyErrorCode dependency_graph_topological_sort(
    DependencyGraph *graph,
    BuildOrder *order
) {
    if (!graph || !order) return DEP_ERROR_NULL_POINTER;

    /* Reset visited flags */
    for (size_t i = 0; i < graph->file_count; i++) {
        graph->files[i]->visited = false;
        graph->files[i]->in_stack = false;
        graph->files[i]->sort_order = -1;
    }

    order->file_count = 0;

    /* Process headers first, then sources */
    for (int pass = 0; pass < 2; pass++) {
        bool processing_headers = (pass == 0);

        for (size_t i = 0; i < graph->file_count; i++) {
            SourceFile *file = graph->files[i];

            if (file->is_header != processing_headers) continue;
            if (file->visited) continue;

            char cycle_path[MAX_PATH_LENGTH * 2];
            if (!topological_sort_dfs(graph, file, order,
                                     cycle_path, sizeof(cycle_path))) {
                fprintf(stderr, "Circular dependency: %s\n", cycle_path);
                return DEP_ERROR_CIRCULAR_DEPENDENCY;
            }
        }
    }

    return DEP_SUCCESS;
}

bool dependency_graph_has_cycle(
    DependencyGraph *graph,
    char *cycle_path,
    size_t path_size
) {
    BuildOrder order;

    /* Reset visited flags */
    for (size_t i = 0; i < graph->file_count; i++) {
        graph->files[i]->visited = false;
        graph->files[i]->in_stack = false;
    }

    order.file_count = 0;

    for (size_t i = 0; i < graph->file_count; i++) {
        if (!graph->files[i]->visited) {
            if (!topological_sort_dfs(graph, graph->files[i], &order,
                                     cycle_path, path_size)) {
                return true;
            }
        }
    }

    return false;
}

/* ==============================================================================
 * Dependency Analysis
 * ==============================================================================
 */

static void collect_dependencies_recursive(
    DependencyGraph *graph,
    SourceFile *file,
    SourceFile **deps,
    size_t max_deps,
    size_t *dep_count,
    bool *visited
) {
    size_t file_idx = 0;
    for (size_t i = 0; i < graph->file_count; i++) {
        if (graph->files[i] == file) {
            file_idx = i;
            break;
        }
    }

    if (visited[file_idx]) return;
    visited[file_idx] = true;

    for (size_t i = 0; i < file->include_count; i++) {
        SourceFile *dep = dependency_graph_find_file(graph, file->includes[i]);
        if (!dep) continue;

        if (*dep_count < max_deps) {
            deps[*dep_count] = dep;
            (*dep_count)++;
        }

        collect_dependencies_recursive(graph, dep, deps, max_deps, dep_count, visited);
    }
}

DependencyErrorCode dependency_graph_get_all_dependencies(
    DependencyGraph *graph,
    SourceFile *file,
    SourceFile **deps,
    size_t max_deps,
    size_t *dep_count
) {
    if (!graph || !file || !deps || !dep_count) {
        return DEP_ERROR_NULL_POINTER;
    }

    *dep_count = 0;

    bool visited[MAX_SOURCE_FILES] = {false};
    collect_dependencies_recursive(graph, file, deps, max_deps, dep_count, visited);

    return DEP_SUCCESS;
}

SourceFile *dependency_graph_find_main(const DependencyGraph *graph) {
    if (!graph) return NULL;

    for (size_t i = 0; i < graph->file_count; i++) {
        if (has_main_function(graph->files[i])) {
            return graph->files[i];
        }
    }

    return NULL;
}

DependencyErrorCode dependency_graph_find_libraries(
    DependencyGraph *graph,
    SourceFile **lib_files,
    size_t max_files,
    size_t *file_count
) {
    if (!graph || !lib_files || !file_count) {
        return DEP_ERROR_NULL_POINTER;
    }

    *file_count = 0;

    for (size_t i = 0; i < graph->file_count; i++) {
        SourceFile *file = graph->files[i];

        /* Library files are non-header sources without main */
        if (!file->is_header && !has_main_function(file)) {
            if (*file_count < max_files) {
                lib_files[*file_count] = file;
                (*file_count)++;
            }
        }
    }

    return DEP_SUCCESS;
}

/* ==============================================================================
 * Debug Printing
 * ==============================================================================
 */

void dependency_graph_print(const DependencyGraph *graph) {
    if (!graph) return;

    printf("Dependency Graph (%zu files):\n", graph->file_count);
    printf("Include Paths: ");
    for (size_t i = 0; i < graph->include_path_count; i++) {
        printf("%s ", graph->include_paths[i]);
    }
    printf("\n\n");

    for (size_t i = 0; i < graph->file_count; i++) {
        SourceFile *file = graph->files[i];
        printf("%s %s:\n", file->is_header ? "[H]" : "[S]", file->path);

        if (file->include_count > 0) {
            printf("  Includes:\n");
            for (size_t j = 0; j < file->include_count; j++) {
                printf("    - %s\n", file->includes[j]);
            }
        }
        printf("\n");
    }
}

void build_order_print(const BuildOrder *order) {
    if (!order) return;

    printf("Build Order (%zu files):\n", order->file_count);
    for (size_t i = 0; i < order->file_count; i++) {
        SourceFile *file = order->ordered_files[i];
        printf("%3zu. %s %s\n", i + 1,
               file->is_header ? "[H]" : "[S]",
               file->path);
    }
}