typedef unsigned int kernel_simd_u32;

void kernel_simd_copy64(void *destination, const void *source) {
  __asm__ volatile(
      "movdqu   (%1), %%xmm0\n\t"
      "movdqu 16(%1), %%xmm1\n\t"
      "movdqu 32(%1), %%xmm2\n\t"
      "movdqu 48(%1), %%xmm3\n\t"
      "movntdq %%xmm0,   (%0)\n\t"
      "movntdq %%xmm1, 16(%0)\n\t"
      "movntdq %%xmm2, 32(%0)\n\t"
      "movntdq %%xmm3, 48(%0)\n\t"
      :
      : "r"(destination), "r"(source)
      : "memory", "xmm0", "xmm1", "xmm2", "xmm3");
}

void kernel_simd_copy16(void *destination, const void *source) {
  __asm__ volatile(
      "movdqu (%1), %%xmm0\n\t"
      "movntdq %%xmm0, (%0)\n\t"
      :
      : "r"(destination), "r"(source)
      : "memory", "xmm0");
}

void kernel_simd_broadcast(kernel_simd_u32 color) {
  __asm__ volatile(
      "movd %0, %%xmm0\n\t"
      "pshufd $0x00, %%xmm0, %%xmm0\n\t"
      :
      : "r"(color)
      : "xmm0");
}

void kernel_simd_store16(void *destination) {
  __asm__ volatile(
      "movntdq %%xmm0, (%0)\n\t"
      :
      : "r"(destination)
      : "memory");
}

void kernel_simd_blend16(
    void *destination, const void *source,
    kernel_simd_u32 alpha, kernel_simd_u32 inverse_alpha) {
  __asm__ volatile(
      "movd %2, %%xmm5\n\t"
      "movd %3, %%xmm6\n\t"
      "movd %4, %%xmm7\n\t"
      "punpcklwd %%xmm5, %%xmm5\n\t"
      "punpcklwd %%xmm6, %%xmm6\n\t"
      "punpcklwd %%xmm7, %%xmm7\n\t"
      "pshufd $0x00, %%xmm5, %%xmm5\n\t"
      "pshufd $0x00, %%xmm6, %%xmm6\n\t"
      "pshufd $0x00, %%xmm7, %%xmm7\n\t"
      "pxor %%xmm4, %%xmm4\n\t"
      "movdqu (%1), %%xmm0\n\t"
      "movdqu (%0), %%xmm1\n\t"
      "movdqa %%xmm0, %%xmm2\n\t"
      "punpcklbw %%xmm4, %%xmm2\n\t"
      "movdqa %%xmm1, %%xmm3\n\t"
      "punpcklbw %%xmm4, %%xmm3\n\t"
      "pmullw %%xmm5, %%xmm2\n\t"
      "pmullw %%xmm6, %%xmm3\n\t"
      "paddw %%xmm3, %%xmm2\n\t"
      "paddw %%xmm7, %%xmm2\n\t"
      "psrlw $8, %%xmm2\n\t"
      "punpckhbw %%xmm4, %%xmm0\n\t"
      "punpckhbw %%xmm4, %%xmm1\n\t"
      "pmullw %%xmm5, %%xmm0\n\t"
      "pmullw %%xmm6, %%xmm1\n\t"
      "paddw %%xmm1, %%xmm0\n\t"
      "paddw %%xmm7, %%xmm0\n\t"
      "psrlw $8, %%xmm0\n\t"
      "packuswb %%xmm0, %%xmm2\n\t"
      "movdqu %%xmm2, (%0)\n\t"
      :
      : "r"(destination), "r"(source), "r"(alpha),
        "r"(inverse_alpha), "r"(128u)
      : "memory", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6",
        "xmm7");
}

void kernel_simd_add16(void *destination, const void *source) {
  __asm__ volatile(
      "movdqu (%1), %%xmm0\n\t"
      "movdqu (%0), %%xmm1\n\t"
      "paddusb %%xmm0, %%xmm1\n\t"
      "movdqu %%xmm1, (%0)\n\t"
      :
      : "r"(destination), "r"(source)
      : "memory", "xmm0", "xmm1");
}
