//help: DANGER: deliberately panics the kernel with SIMD #XF.
//help: Unmasks divide-by-zero in MXCSR, then divides 1.0/0.0. Kernel reboots required.
//help: Usage: fp_drill
//help: Use this to verify the #XF handler dumps MXCSR correctly.

void main() {
    println("fp_drill: about to unmask #XF(DE) and divide by zero...");
    println("fp_drill: this will panic the kernel. Reboot required afterward.");

    int mx;
    asm {
        stmxcsr [mx]
    }
    /* Clear bit 9 (DE mask) so DIV-by-zero raises #XF */
    mx = mx & 0xFFFFFDFF;
    asm {
        ldmxcsr [mx]
    }

    float a = 1.0;
    float b = 0.0;
    float c = 0.0;
    asm {
        movss xmm0, [a]
        movss xmm1, [b]
        divss xmm0, xmm1
        movss [c], xmm0
    }

    /* If we got here, #XF didn't fire — unexpected */
    println("fp_drill: UNEXPECTED — #XF did not fire despite unmasked DE");
}
