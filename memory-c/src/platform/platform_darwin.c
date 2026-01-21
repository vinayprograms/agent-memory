/*
 * Memory Service - macOS/Darwin Platform Implementation
 */

#include "platform.h"

#include <sys/mman.h>
#include <string.h>

/*
 * Memory remapping on macOS (no mremap, use munmap + mmap)
 *
 * Unlike Linux mremap(), macOS requires us to:
 * 1. Create a new mapping at the larger size
 * 2. Unmap the old region
 *
 * The file descriptor is required to create the new mapping.
 *
 * IMPORTANT: POINTER INVALIDATION
 * After calling this function, ALL pointers into the old memory region become
 * INVALID and must not be dereferenced. The caller is responsible for updating
 * any cached pointers to use the new base address returned by this function.
 *
 * Unlike Linux mremap() which may return the same address if resizing in-place
 * is possible, this implementation ALWAYS returns a different address.
 *
 * Thread Safety: This function is NOT thread-safe. Concurrent access to the
 * memory region during remapping results in undefined behavior.
 *
 * Returns: New address on success, MAP_FAILED on failure. On failure, the
 * original mapping at old_addr remains valid and unchanged.
 */
void* platform_mremap(void* old_addr, size_t old_size, size_t new_size, int fd) {
    /* Create new mapping at the new size */
    void* new_addr = mmap(NULL, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (new_addr == MAP_FAILED) {
        return MAP_FAILED;
    }

    /*
     * Unmap the old region - this invalidates all pointers into old_addr.
     * The file-backed data is preserved since we created the new mapping first.
     */
    munmap(old_addr, old_size);

    return new_addr;
}

/*
 * ONNX Runtime provider - CoreML on Apple Silicon
 */
const char* platform_onnx_provider(void) {
#if defined(ARCH_ARM64)
    return "CoreML";
#else
    return "CPU";
#endif
}

bool platform_has_accelerator(void) {
#if defined(ARCH_ARM64)
    /* Apple Silicon has Neural Engine */
    return true;
#else
    return false;
#endif
}

/*
 * Platform description
 */
const char* platform_description(void) {
    return PLATFORM_NAME "-" ARCH_NAME;
}
