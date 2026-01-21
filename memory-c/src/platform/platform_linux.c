/*
 * Memory Service - Linux Platform Implementation
 */

#include "platform.h"

#include <sys/mman.h>
#include <string.h>

/*
 * Memory remapping using Linux mremap()
 */
void* platform_mremap(void* old_addr, size_t old_size, size_t new_size, int fd) {
    (void)fd;  /* Not needed on Linux - mremap handles it */
    return mremap(old_addr, old_size, new_size, MREMAP_MAYMOVE);
}

/*
 * ONNX Runtime provider - CPU on Linux (CUDA would need separate build)
 */
const char* platform_onnx_provider(void) {
    return "CPU";
}

bool platform_has_accelerator(void) {
    /* TODO: Check for CUDA availability */
    return false;
}

/*
 * Platform description
 */
const char* platform_description(void) {
    return PLATFORM_NAME "-" ARCH_NAME;
}
