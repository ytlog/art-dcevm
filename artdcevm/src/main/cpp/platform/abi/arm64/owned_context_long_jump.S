/*
 * Copyright (C) 2014 The Android Open Source Project
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Adapted from fixed Android 16/17 ART art_quick_do_long_jump. Thread field
 * offsets are provider-calibrated parameters, not object offsets recovered
 * from code. Slot32 is a private consumer refresh payload, never an ART value.
 */
.text
.p2align 2
.macro OWNED_LONG_JUMP name, reserved
.global \name
.hidden \name
.type \name,%function
\name:
.cfi_startproc
  // ABI input: x0 owned native Context, w1 calibrated refresh payload.
  sub sp,sp,#544
  .cfi_def_cfa_offset 544
  str x30,[sp,#528]
  .cfi_offset x30,-16
  mov w3,w1
  mov x1,sp
  add x2,sp,#272
  bl ArtDcevmCopyContextForLongJump
  add x0,sp,#272
  ldp d0,d1,[x0,#0]
  ldp d2,d3,[x0,#16]
  ldp d4,d5,[x0,#32]
  ldp d6,d7,[x0,#48]
  ldp d8,d9,[x0,#64]
  ldp d10,d11,[x0,#80]
  ldp d12,d13,[x0,#96]
  ldp d14,d15,[x0,#112]
  ldp d16,d17,[x0,#128]
  ldp d18,d19,[x0,#144]
  ldp d20,d21,[x0,#160]
  ldp d22,d23,[x0,#176]
  ldp d24,d25,[x0,#192]
  ldp d26,d27,[x0,#208]
  ldp d28,d29,[x0,#224]
  ldp d30,d31,[x0,#240]
  ldp x0,x1,[sp,#0]
  ldp x2,x3,[sp,#16]
  ldp x4,x5,[sp,#32]
  ldp x6,x7,[sp,#48]
  ldp x8,x9,[sp,#64]
  ldp x10,x11,[sp,#80]
  ldp x12,x13,[sp,#96]
  ldp x14,x15,[sp,#112]
  // x16/x17 are IP scratches. x18 platform register remains untouched.
  ldr x19,[sp,#152]
.if \reserved
  ldr w20,[sp,#256]       // own protocol: low16=marking, high16=suspend.
  lsr w21,w20,#16
  and w20,w20,#0xffff
.else
  ldr x20,[sp,#160]       // ordinary callee-save when MR is not reserved.
  ldr w21,[sp,#256]
  lsr w21,w21,#16
.endif
  ldp x22,x23,[sp,#176]
  ldp x24,x25,[sp,#192]
  ldp x26,x27,[sp,#208]
  ldp x28,x29,[sp,#224]
  ldp x30,x16,[sp,#240]   // LR/SP.
  ldr x17,[sp,#264]       // PC.
  mov sp,x16
  .cfi_def_cfa_offset 0
  .cfi_register x30,x17
  // Exact source ordering: refresh Thread state after changing SP, before br.
.if \reserved
  mov w16,w20
  ldr w20,[x19,x16]
.endif
  ldr x21,[x19,x21]
  br x17
.cfi_endproc
.size \name,.-\name
.endm
OWNED_LONG_JUMP ArtDcevmArm64LongJumpReserved,1
OWNED_LONG_JUMP ArtDcevmArm64LongJumpUnreserved,0
.section .note.GNU-stack,"",%progbits
