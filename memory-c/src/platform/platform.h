/*
 * Memory Service - Platform Abstraction Layer
 *
 * This header declares platform-specific functions that have different
 * implementations on Linux, macOS, etc.
 *
 * Implementations are in:
 *   - platform_linux.c  (Linux)
 *   - platform_darwin.c (macOS/iOS)
 */

#ifndef MEMORY_SERVICE_PLATFORM_H
#define MEMORY_SERVICE_PLATFORM_H

#include <stddef.h>
#include <stdbool.h>

/* Platform detection */
#if defined(__linux__)
    #define PLATFORM_LINUX 1
    #define PLATFORM_NAME "linux"
#elif defined(__APPLE__)
    #define PLATFORM_DARWIN 1
    #define PLATFORM_NAME "darwin"
#elif defined(_WIN32)
    #define PLATFORM_WINDOWS 1
    #define PLATFORM_NAME "windows"
#else
    #define PLATFORM_UNKNOWN 1
    #define PLATFORM_NAME "unknown"
#endif

/* Architecture detection */
#if defined(__x86_64__) || defined(_M_X64)
    #define ARCH_X86_64 1
    #define ARCH_NAME "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define ARCH_ARM64 1
    #define ARCH_NAME "arm64"
#else
    #define ARCH_UNKNOWN 1
    #define ARCH_NAME "unknown"
#endif

/*
 * Memory-mapped file operations
 */

/* Remap a memory-mapped region to a new size.
 * On Linux, uses mremap(). On macOS, uses munmap()+mmap().
 *
 * Parameters:
 *   old_addr  - Current mapped address
 *   old_size  - Current mapped size
 *   new_size  - Desired new size
 *   fd        - File descriptor (needed for macOS fallback)
 *
 * Returns:
 *   New mapped address on success, MAP_FAILED on error
 */
void* platform_mremap(void* old_addr, size_t old_size, size_t new_size, int fd);

/*
 * ONNX Runtime execution provider
 */

/* Get the name of the preferred ONNX execution provider for this platform.
 * Returns: "CoreML" on macOS with Apple Silicon, "CPU" otherwise
 */
const char* platform_onnx_provider(void);

/* Check if hardware acceleration is available for ONNX inference.
 * Returns: true if GPU/NPU acceleration available
 */
bool platform_has_accelerator(void);

/*
 * Platform info
 */

/* Get platform description string (e.g., "darwin-arm64") */
const char* platform_description(void);

#endif /* MEMORY_SERVICE_PLATFORM_H */
