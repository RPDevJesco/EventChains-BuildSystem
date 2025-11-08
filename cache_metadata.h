/**
 * ==============================================================================
 * EventChains Build System - Persistent Cache Metadata
 * ==============================================================================
 * 
 * Provides persistent caching across build runs using content hashing and
 * transitive dependency tracking.
 * 
 * Features:
 * - Content-addressable caching (hash-based)
 * - Transitive dependency tracking
 * - Survives build directory deletion
 * - Fast incremental builds
 * 
 * Copyright (c) 2024 EventChains Project
 * Licensed under the MIT License
 * ==============================================================================
 */

#ifndef CACHE_METADATA_H
#define CACHE_METADATA_H

#include "dependency_resolver.h"
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==============================================================================
 * Configuration Constants
 * ==============================================================================
 */

#define MAX_CACHE_ENTRIES 2048
#define MAX_DEPENDENCIES_PER_FILE 128
#define CACHE_VERSION 1

/* ==============================================================================
 * Cache Entry Structure
 * ==============================================================================
 */

/**
 * CacheEntry - Metadata for a single compiled source file
 * 
 * This stores everything needed to determine if recompilation is necessary:
 * - Source file hash (content-addressable)
 * - Object file location
 * - All dependencies and their hashes
 * - Timestamps for fallback
 */
typedef struct CacheEntry {
    char source_path[MAX_PATH_LENGTH];      /* Full path to source file */
    char object_path[MAX_PATH_LENGTH];      /* Full path to object file */
    
    uint64_t source_hash;                   /* Hash of source content (FNV-1a) */
    time_t source_mtime;                    /* Last modification time (fallback) */
    time_t last_compiled;                   /* When we compiled it */
    
    /* Transitive dependencies */
    char dependencies[MAX_DEPENDENCIES_PER_FILE][MAX_PATH_LENGTH];
    uint64_t dependency_hashes[MAX_DEPENDENCIES_PER_FILE];
    size_t dependency_count;
    
    bool valid;                             /* Is this entry valid? */
} CacheEntry;

/**
 * BuildCache - Complete persistent cache
 * 
 * Stores all compilation metadata for the project.
 * Persisted to disk as .eventchains/cache.dat
 */
typedef struct BuildCache {
    uint32_t version;                       /* Cache format version */
    size_t entry_count;                     /* Number of entries */
    CacheEntry entries[MAX_CACHE_ENTRIES];  /* Cache entries */
    
    char cache_dir[MAX_PATH_LENGTH];        /* .eventchains directory */
    char project_dir[MAX_PATH_LENGTH];      /* Project root directory */
    
    /* Statistics */
    size_t hits;                            /* Cache hits */
    size_t misses;                          /* Cache misses */
    size_t invalidations;                   /* Entries invalidated */
} BuildCache;

/* ==============================================================================
 * Cache Management API
 * ==============================================================================
 */

/**
 * Create or load build cache from project directory
 * 
 * This will:
 * 1. Create .eventchains/ directory if it doesn't exist
 * 2. Load cache.dat if it exists
 * 3. Validate cache version
 * 
 * @param project_dir  Project root directory
 * @return             Pointer to BuildCache, or NULL on error
 */
BuildCache *build_cache_create(const char *project_dir);

/**
 * Save cache to disk
 * 
 * Writes cache.dat to .eventchains/ directory.
 * Uses atomic write (write to temp file, then rename).
 * 
 * @param cache  Pointer to BuildCache
 * @return       true on success, false on error
 */
bool build_cache_save(const BuildCache *cache);

/**
 * Destroy cache and free resources
 * 
 * @param cache  Pointer to BuildCache
 */
void build_cache_destroy(BuildCache *cache);

/**
 * Clear all cache entries
 * 
 * @param cache  Pointer to BuildCache
 */
void build_cache_clear(BuildCache *cache);

/* ==============================================================================
 * Cache Query API
 * ==============================================================================
 */

/**
 * Find cache entry for a source file
 * 
 * @param cache       Pointer to BuildCache
 * @param source_path Full path to source file
 * @return            Pointer to CacheEntry, or NULL if not found
 */
CacheEntry *build_cache_find(BuildCache *cache, const char *source_path);

/**
 * Check if source needs recompilation using cache metadata
 * 
 * This is the core caching logic. Returns true if:
 * - No cache entry exists
 * - Source content changed (hash mismatch)
 * - Any dependency changed (hash mismatch)
 * - Object file doesn't exist (optional check)
 * 
 * @param cache       Pointer to BuildCache
 * @param source      Source file to check
 * @param object_path Path to object file
 * @return            true if recompilation needed, false if cached
 */
bool build_cache_needs_recompilation(
    BuildCache *cache,
    const SourceFile *source,
    const char *object_path
);

/**
 * Update cache entry after compilation
 * 
 * This should be called after successful compilation to update:
 * - Source hash
 * - Object path
 * - Dependency hashes
 * - Timestamps
 * 
 * @param cache       Pointer to BuildCache
 * @param source_path Path to source file
 * @param object_path Path to object file
 * @param graph       Dependency graph (for transitive deps)
 */
void build_cache_update(
    BuildCache *cache,
    const char *source_path,
    const char *object_path,
    const DependencyGraph *graph
);

/**
 * Invalidate cache entry for a source file
 * 
 * Marks the entry as invalid, forcing recompilation.
 * 
 * @param cache       Pointer to BuildCache
 * @param source_path Path to source file
 */
void build_cache_invalidate(BuildCache *cache, const char *source_path);

/**
 * Invalidate all entries that depend on a given file
 * 
 * This is used when a header changes - all sources that include it
 * need to be recompiled.
 * 
 * @param cache       Pointer to BuildCache
 * @param changed_file Path to changed file (usually a header)
 * @param graph       Dependency graph
 */
void build_cache_invalidate_dependents(
    BuildCache *cache,
    const char *changed_file,
    const DependencyGraph *graph
);

/* ==============================================================================
 * Cache Statistics & Reporting
 * ==============================================================================
 */

/**
 * Print cache statistics
 * 
 * @param cache  Pointer to BuildCache
 */
void build_cache_print_stats(const BuildCache *cache);

/**
 * Get cache hit rate
 * 
 * @param cache  Pointer to BuildCache
 * @return       Hit rate (0.0 to 1.0)
 */
double build_cache_hit_rate(const BuildCache *cache);

/**
 * Get cache size in bytes
 * 
 * @param cache  Pointer to BuildCache
 * @return       Size in bytes
 */
size_t build_cache_size_bytes(const BuildCache *cache);

/* ==============================================================================
 * Utility Functions
 * ==============================================================================
 */

/**
 * Hash file contents using FNV-1a
 * 
 * Fast, non-cryptographic hash suitable for cache validation.
 * 
 * @param path  Path to file
 * @return      64-bit hash, or 0 on error
 */
uint64_t hash_file_content(const char *path);

/**
 * Get file modification time
 * 
 * @param path  Path to file
 * @return      Modification time, or 0 on error
 */
time_t get_file_mtime(const char *path);

/**
 * Check if file exists
 * 
 * @param path  Path to file
 * @return      true if file exists, false otherwise
 */
bool file_exists_cache(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* CACHE_METADATA_H */
