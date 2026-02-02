.text
.globl _new_objc_msgSend
.p2align 4

_new_objc_msgSend:
    .cfi_startproc

    sub sp, sp, #208
    .cfi_def_cfa_offset 208

    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp q0, q1, [sp, #80]
    stp q2, q3, [sp, #144]

    stp x29, x30, [sp, #192]
    add x29, sp, #192

    mov x2, x30
    mov x3, sp
    bl _pre_objc_msgSend_callback

    // pre_objc_msgSend_callback() decides whether to trace or not.
    // If this call shouldn't be traced, restore registers and jump to original objc_msgSend
    // without calling post_objc_msgSend_callback()
    cmp x0, #0
    b.ne L_trace

L_fast_path:
    ldp q2, q3, [sp, #144]
    ldp q0, q1, [sp, #80]
    ldp x8, x9, [sp, #64]
    ldp x6, x7, [sp, #48]
    ldp x4, x5, [sp, #32]
    ldp x2, x3, [sp, #16]
    ldp x0, x1, [sp, #0]

    ldp x29, x30, [sp, #192]

    add sp, sp, #208
    .cfi_def_cfa_offset 0

    adrp x16, _g_original_objc_msgSend@PAGE
    add x16, x16, _g_original_objc_msgSend@PAGEOFF
    ldr x16, [x16]

    br x16

L_trace:
    .cfi_def_cfa_offset 208

    ldp q2, q3, [sp, #144]
    ldp q0, q1, [sp, #80]
    ldp x8, x9, [sp, #64]
    ldp x6, x7, [sp, #48]
    ldp x4, x5, [sp, #32]
    ldp x2, x3, [sp, #16]
    ldp x0, x1, [sp, #0]

    ldp x29, x30, [sp, #192]

    add sp, sp, #208
    .cfi_def_cfa_offset 0

    adrp x16, _g_original_objc_msgSend@PAGE
    add x16, x16, _g_original_objc_msgSend@PAGEOFF
    ldr x16, [x16]
    blr x16

    sub sp, sp, #144
    .cfi_def_cfa_offset 144

    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp q0, q1, [sp, #32]
    stp q2, q3, [sp, #96]

    bl _post_objc_msgSend_callback
    mov x30, x0

    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp q0, q1, [sp, #32]
    ldp q2, q3, [sp, #96]

    add sp, sp, #144
    .cfi_def_cfa_offset 0

    ret

    .cfi_endproc
