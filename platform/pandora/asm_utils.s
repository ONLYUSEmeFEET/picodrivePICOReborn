@ vim:filetype=armasm


.global clut_line @ void *dest, void *src, unsigned short *pal, int pixels_mask

clut_line:
    stmfd   sp!, {r4-r11,lr}

    and     lr, r3, #0xff0000
    mov     lr, lr, lsr #15        @ mask
    mov     r3, r3, lsr #3
    and     r3, r3, #0xff          @ counter
    add     r11,r0, #800*2

clut_line_loop:
    ldmia   r1!, {r10,r12}

    and     r4, lr, r10, lsl #1
    ldrh    r4, [r2, r4]
    and     r5, lr, r10, lsr #7
    ldrh    r5, [r2, r5]
    orr     r4, r4, r4,  lsl #16

    and     r6, lr, r10, lsr #15
    ldrh    r6, [r2, r6]
    orr     r5, r5, r5,  lsl #16

    and     r7, lr, r10, lsr #23
    ldrh    r7, [r2, r7]
    orr     r6, r6, r6,  lsl #16

    and     r8, lr, r12, lsl #1
    ldrh    r8, [r2, r8]
    orr     r7, r7, r7,  lsl #16

    and     r9, lr, r12, lsr #7
    ldrh    r9, [r2, r9]
    orr     r8, r8, r8,  lsl #16

    and     r10,lr, r12, lsr #15
    ldrh    r10,[r2, r10]
    orr     r9, r9, r9,  lsl #16

    and     r12,lr, r12, lsr #23
    ldrh    r12,[r2, r12]
    orr     r10,r10,r10, lsl #16

    subs    r3, r3, #1
    orr     r12,r12,r12, lsl #16

    stmia   r0!, {r4-r10,r12}
    stmia   r11!,{r4-r10,r12}
    bne     clut_line_loop

    ldmfd   sp!, {r4-r11,pc}

