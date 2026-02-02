.text
.globl _new_objc_msgSend
.p2align 2
.code 16
.thumb_func

_new_objc_msgSend:
    .cfi_startproc

    push {r0-r3, lr}
    .cfi_def_cfa_offset 20
    .cfi_offset lr, -4

    mov r2, lr
    mov r3, sp
    bl _pre_objc_msgSend_callback
    
    cmp r0, #0
    beq L_fast_path

    pop {r0-r3, lr}
    .cfi_def_cfa_offset 0

    ldr r12, L_offset_slow
L_base_slow:
    add r12, pc
    ldr r12, [r12]
    ldr r12, [r12]

    blx r12

    push {r0-r3}
    .cfi_def_cfa_offset 16

    bl _post_objc_msgSend_callback
    mov lr, r0

    pop {r0-r3}
    .cfi_def_cfa_offset 0

    bx lr

L_fast_path:
    pop {r0-r3, lr}
    .cfi_def_cfa_offset 0

    ldr r12, L_offset_fast
L_base_fast:
    add r12, pc
    ldr r12, [r12]
    ldr r12, [r12]
    
    bx r12

    .align 2
L_offset_slow:
    .long L_data_ptr - (L_base_slow + 4)
L_offset_fast:
    .long L_data_ptr - (L_base_fast + 4)

    .cfi_endproc


.section __DATA,__data
.align 2
L_data_ptr:
    .long _g_original_objc_msgSend
