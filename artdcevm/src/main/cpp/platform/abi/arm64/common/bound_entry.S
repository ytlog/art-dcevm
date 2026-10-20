/*
 * Copyright (C) 2014 The Android Open Source Project
 * Copyright (C) 2026 art-dcevm contributors
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// Fixed arm64 ExecuteSwitchImplAsm native ABI, Android 11--17. Android 16:
// runtime/arch/arm64/native_entrypoints_arm64.S and interpreter/cfi_asm_support.h.
// The implementation is supplied by ART; only its small native wrapper is
// reproduced here, preserving x19, LR, CFA and ART's DEX1 unwind expression.
.text
.p2align 2
.hidden artdcevmBoundSwitchEntry
.global artdcevmBoundSwitchEntry
.type artdcevmBoundSwitchEntry, %function
artdcevmBoundSwitchEntry:
.cfi_startproc
    bti c
    adrp x9, artdcevm_bound_entry_method
    ldr x9, [x9, :lo12:artdcevm_bound_entry_method]
    ldr x10, [x0, #16] // SwitchContextPrefix::frame, asserted in C++.
    ldr x10, [x10, #8] // ShadowFramePrefix::method, asserted in C++.
    cmp x9, x10
    b.eq .Lbound
.cfi_remember_state
    stp x19, x30, [sp, #-16]!
.cfi_def_cfa_offset 16
.cfi_offset 19, -16
.cfi_offset 30, -8
    mov x19, x2
.cfi_escape 0x16, 0, 0x09, 0x0c, 0x44, 0x45, 0x58, 0x31, 0x13, 0x92, 19, 0
    blr x1
    ldp x19, x30, [sp], #16
.cfi_def_cfa_offset 0
.cfi_restore 19
.cfi_restore 30
    ret
.Lbound:
.cfi_restore_state
    b artdcevmBoundInterpreterEntry
.cfi_endproc
.size artdcevmBoundSwitchEntry, .-artdcevmBoundSwitchEntry
.section .note.GNU-stack,"",%progbits
