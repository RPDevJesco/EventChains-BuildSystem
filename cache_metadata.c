/**
 * ==============================================================================
 * EventChains Build System - Persistent Cache Implementation
 * ==============================================================================
 */

#include "cache_metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
    #include <direct.h>
    #include <io.h>
    #define mkdir(path, mode) _mkdir(path)
    #define access _access
    #define F_OK 0
#else
    #include <unistd.h>
#endif

/* ==============================================================================
 * Internal Constants
 * ==============================================================================
 */

#define CACHE_FILENAME "cache.dat"
#define CACHE_TEMP_FILENAME "cache.dat.tmp"
#define FNV_OFFSET_BASIS 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

/* ==============================================================================
 * Utility Functions Implementation
 * ==============================================================================
 */

uint64_t hash_file_content(const char *path) {
    if (!path) return 0;
    
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    
    uint64_t hash = FNV_OFFSET_BASIS;
    unsigned char buffer[8192];
    size_t bytes;
    
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        for (size_t i = 0; i < bytes; i++) {
            hash ^= buffer[i];
            hash *= FNV_PRIME;
        }
    }
    
    fclose(fp);
    return hash;
}

time_t get_file_mtime(const char *path) {
    if (!path) return 0;
    
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    
    return st.st_mtime;
}

bool file_exists_cache(const char *path) {
    if (!path) return false;
    return access(path, F_OK) == 0;
}

/* ==============================================================================
 * Cache Management Implementation
 * ==============================================================================
 */

BuildCache *build_cache_create(const char *project_dir) {
    if (!project_dir) return NULL;
    
    BuildCache *cache = (BuildCache *)calloc(1, sizeof(BuildCache));
    if (!cache) return NULL;
    
    /* Initialize cache */
    cache->version = CACHE_VERSION;
    cache->entry_count = 0;
    cache->hits = 0;
    cache->misses = 0;
    cache->invalidations = 0;
    
    /* Store project directory */
    strncpy(cache->project_dir, project_dir, MAX_PATH_LENGTH - 1);
    cache->project_dir[MAX_PATH_LENGTH - 1] = '\0';
    
    /* Create .eventchains directory */
    snprintf(cache->cache_dir, MAX_PATH_LENGTH, "%s/.eventchains", project_dir);
    mkdir(cache->cache_dir, 0755);
    
    /* Try to load existing cache */
    char cache_file[MAX_PATH_LENGTH];
    snprintf(cache_file, MAX_PATH_LENGTH, "%s/%s", cache->cache_dir, CACHE_FILENAME);
    
    FILE *fp = fopen(cache_file, "rb");
    if (fp) {
        /* Read version */
        uint32_t version;
        if (fread(&version, sizeof(uint32_t), 1, fp) != 1) {
            fclose(fp);
            printf("Warning: Failed to read cache version\n");
            return cache;
        }
        
        /* Check version compatibility */
        if (version != CACHE_VERSION) {
            fclose(fp);
            printf("Warning: Cache version mismatch (expected %d, got %d) - rebuilding cache\n",
                   CACHE_VERSION, version);
            return cache;
        }
        
        /* Read entry count */
        if (fread(&cache->entry_count, sizeof(size_t), 1, fp) != 1) {
            fclose(fp);
            printf("Warning: Failed to read cache entry count\n");
            cache->entry_count = 0;
            return cache;
        }
        
        /* Validate entry count */
        if (cache->entry_count > MAX_CACHE_ENTRIES) {
            fclose(fp);
            printf("Warning: Invalid cache entry count (%zu) - rebuilding cache\n",
                   cache->entry_count);
            cache->entry_count = 0;
            return cache;
        }
        
        /* Read entries */
        if (fread(cache->entries, sizeof(CacheEntry), cache->entry_count, fp) != cache->entry_count) {
            fclose(fp);
            printf("Warning: Failed to read cache entries - rebuilding cache\n");
            cache->entry_count = 0;
            return cache;
        }
        
        fclose(fp);
        printf("Loaded cache: %zu entries from %s\n", cache->entry_count, cache_file);
    }
    
    return cache;
}

bool build_cache_save(const BuildCache *cache) {
    if (!cache) return false;
    
    /* Write to temporary file first (atomic write) */
    char temp_file[MAX_PATH_LENGTH];
    snprintf(temp_file, MAX_PATH_LENGTH, "%s/%s", cache->cache_dir, CACHE_TEMP_FILENAME);
    
    FILE *fp = fopen(temp_file, "wb");
    if (!fp) {
        printf("Warning: Failed to open cache file for writing: %s\n", temp_file);
        return false;
    }
    
    /* Write version */
    if (fwrite(&cache->version, sizeof(uint32_t), 1, fp) != 1) {
        fclose(fp);
        remove(temp_file);
        return false;
    }
    
    /* Write entry count */
    if (fwrite(&cache->entry_count, sizeof(size_t), 1, fp) != 1) {
        fclose(fp);
        remove(temp_file);
        return false;
    }
    
    /* Write entries */
    if (fwrite(cache->entries, sizeof(CacheEntry), cache->entry_count, fp) != cache->entry_count) {
        fclose(fp);
        remove(temp_file);
        return false;
    }
    
    fclose(fp);
    
    /* Atomic rename */
    char cache_file[MAX_PATH_LENGTH];
    snprintf(cache_file, MAX_PATH_LENGTH, "%s/%s", cache->cache_dir, CACHE_FILENAME);
    
#ifdef _WIN32
    /* Windows requires removing the target file first */
    remove(cache_file);
#endif
    
    if (rename(temp_file, cache_file) != 0) {
        printf("Warning: Failed to rename cache file\n");
        remove(temp_file);
        return false;
    }
    
    return true;
}

void build_cache_destroy(BuildCache *cache) {
    if (!cache) return;
    free(cache);
}

void build_cache_clear(BuildCache *cache) {
    if (!cache) return;
    
    cache->entry_count = 0;
    cache->hits = 0;
    cache->misses = 0;
    cache->invalidations = 0;
    
    memset(cache->entries, 0, sizeof(cache->entries));
}

/* ==============================================================================
 * Cache Query Implementation
 * ==============================================================================
 */

CacheEntry *build_cache_find(BuildCache *cache, const char *source_path) {
    if (!cache || !source_path) return NULL;
    
    for (size_t i = 0; i < cache->entry_count; i++) {
        if (strcmp(cache->entries[i].source_path, source_path) == 0) {
            return &cache->entries[i];
        }
    }
    
    return NULL;
}

bool build_cache_needs_recompilation(
    BuildCache *cache,
    const SourceFile *source,
    const char *object_path
) {
    if (!cache || !source) {
        return true;
    }
    
    /* Find cache entry */
    CacheEntry *entry = build_cache_find(cache, source->path);
    if (!entry || !entry->valid) {
        cache->misses++;
        return true; /* No cache entry = must compile */
    }
    
    /* Check if source content changed */
    uint64_t current_hash = hash_file_content(source->path);
    if (current_hash == 0) {
        cache->misses++;
        return true; /* Can't read source file */
    }
    
    if (current_hash != entry->source_hash) {
        cache->misses++;
        return true; /* Source content changed */
    }
    
    /* Check if any dependency changed */
    for (size_t i = 0; i < entry->dependency_count; i++) {
        uint64_t dep_hash = hash_file_content(entry->dependencies[i]);
        if (dep_hash == 0) {
            /* Dependency file missing - might be OK if it's a system header */
            continue;
        }
        
        if (dep_hash != entry->dependency_hashes[i]) {
            cache->misses++;
            return true; /* Dependency changed */
        }
    }
    
    /* Cache hit!
     * Note: We DON'T check if object file exists. If source and dependencies
     * are unchanged, we trust the cache even if .o file was deleted.
     * The compilation system will handle regenerating it if needed.
     */
    cache->hits++;
    return false;
}

void build_cache_update(
    BuildCache *cache,
    const char *source_path,
    const char *object_path,
    const DependencyGraph *graph
) {
    if (!cache || !source_path || !object_path) return;

    /* Find existing entry or create new one */
    CacheEntry *entry = build_cache_find(cache, source_path);
    if (!entry) {
        if (cache->entry_count >= MAX_CACHE_ENTRIES) {
            printf("Warning: Cache full (%d entries), cannot add more\n", MAX_CACHE_ENTRIES);
            return;
        }
        entry = &cache->entries[cache->entry_count++];
    }

    /* Update basic info */
    strncpy(entry->source_path, source_path, MAX_PATH_LENGTH - 1);
    entry->source_path[MAX_PATH_LENGTH - 1] = '\0';

    strncpy(entry->object_path, object_path, MAX_PATH_LENGTH - 1);
    entry->object_path[MAX_PATH_LENGTH - 1] = '\0';

    /* Hash source content */
    entry->source_hash = hash_file_content(source_path);
    entry->source_mtime = get_file_mtime(source_path);
    entry->last_compiled = time(NULL);

    /* Store dependencies from dependency graph */
    entry->dependency_count = 0;

    if (graph) {
        SourceFile *source = dependency_graph_find_file(graph, source_path);
        if (source) {
            size_t max_deps = source->include_count;
            if (max_deps > MAX_DEPENDENCIES_PER_FILE) {
                max_deps = MAX_DEPENDENCIES_PER_FILE;
            }

            for (size_t i = 0; i < max_deps; i++) {
                strncpy(entry->dependencies[entry->dependency_count],
                       source->includes[i],
                       MAX_PATH_LENGTH - 1);
                entry->dependencies[entry->dependency_count][MAX_PATH_LENGTH - 1] = '\0';

                entry->dependency_hashes[entry->dependency_count] =
                    hash_file_content(source->includes[i]);

                entry->dependency_count++;
            }
        }
    }

    entry->valid = true;
}

void build_cache_invalidate(BuildCache *cache, const char *source_path) {
    if (!cache || !source_path) return;

    CacheEntry *entry = build_cache_find(cache, source_path);
    if (entry) {
        entry->valid = false;
        cache->invalidations++;
    }
}

void build_cache_invalidate_dependents(
    BuildCache *cache,
    const char *changed_file,
    const DependencyGraph *graph
) {
    if (!cache || !changed_file || !graph) return;

    /* Find all cache entries that depend on changed_file */
    for (size_t i = 0; i < cache->entry_count; i++) {
        CacheEntry *entry = &cache->entries[i];
        if (!entry->valid) continue;

        /* Check if this entry depends on changed_file */
        for (size_t j = 0; j < entry->dependency_count; j++) {
            if (strcmp(entry->dependencies[j], changed_file) == 0) {
                /* Found a dependent - invalidate it */
                entry->valid = false;
                cache->invalidations++;
                break;
            }
        }
    }
}

/* ==============================================================================
 * Cache Statistics Implementation
 * ==============================================================================
 */

void build_cache_print_stats(const BuildCache *cache) {
    if (!cache) return;

    printf("\nCache Statistics:\n");
    printf("  Total Entries: %zu\n", cache->entry_count);
    printf("  Cache Hits:    %zu\n", cache->hits);
    printf("  Cache Misses:  %zu\n", cache->misses);
    printf("  Invalidations: %zu\n", cache->invalidations);

    if (cache->hits + cache->misses > 0) {
        double hit_rate = (double)cache->hits / (cache->hits + cache->misses) * 100.0;
        printf("  Hit Rate:      %.1f%%\n", hit_rate);
    }

    printf("  Cache Size:    %zu KB\n", build_cache_size_bytes(cache) / 1024);
}

double build_cache_hit_rate(const BuildCache *cache) {
    if (!cache) return 0.0;

    size_t total = cache->hits + cache->misses;
    if (total == 0) return 0.0;

    return (double)cache->hits / total;
}

size_t build_cache_size_bytes(const BuildCache *cache) {
    if (!cache) return 0;

    return sizeof(BuildCache) +
           (cache->entry_count * sizeof(CacheEntry));
}