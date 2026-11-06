; Known failing optimizer audit case, not a passing native-codegen test.
; On a patched LLVM core, run the SROA pass on this function.
; The load must return 0x1122 (4386): it addresses the second native word.
; Current SROA instead creates an i8 alloca/store of 0x33 followed by i16 load.
; See docs/WORD16.md. Do not enable --byte-size=16 with this unresolved.
target datalayout = "e-B16-p:16:16-i8:16-i16:16-i32:16-i64:16-a:16-n16-S16"
define i16 @extract() {
  %p = alloca i32, align 1
  store i32 287454020, ptr %p, align 1
  %q = getelementptr i16, ptr %p, i16 1
  %v = load i16, ptr %q, align 1
  ret i16 %v
}
