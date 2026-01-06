.text
.globl _new_objc_msgSend
.p2align 2
.thumb_func

_new_objc_msgSend:
    .cfi_startproc

    push {r0-r3, lr}
    .cfi_def_cfa_offset 20
    .cfi_offset lr, -4

    mov r2, lr
    mov r3, sp
    bl _pre_objc_msgSend_callback
    mov r12, r0

    pop {r0-r3, lr}
    .cfi_def_cfa_offset 0

    blx r12

    push {r0-r3}
    .cfi_def_cfa_offset 16

    bl _post_objc_msgSend_callback
    mov lr, r0

    pop {r0-r3}
    .cfi_def_cfa_offset 0

    bx lr

    .cfi_endproc
