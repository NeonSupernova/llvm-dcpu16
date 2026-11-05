; Freestanding octet16 integer division helpers. See LICENSE.TXT.
; No division instructions or host-library dependencies are used here.

define internal i32 @__dcpu_udivmod32(i32 %a, i32 %b, ptr %remainder) noinline {
entry:
  %zero = icmp eq i32 %b, 0
  br i1 %zero, label %divide_by_zero, label %loop
divide_by_zero:
  store i32 0, ptr %remainder, align 2
  ret i32 0
loop:
  %count = phi i16 [ 32, %entry ], [ %count_next, %loop ]
  %numerator = phi i32 [ %a, %entry ], [ %numerator_next, %loop ]
  %quotient = phi i32 [ 0, %entry ], [ %quotient_next, %loop ]
  %rem = phi i32 [ 0, %entry ], [ %rem_next, %loop ]
  %input_bit = lshr i32 %numerator, 31
  %numerator_next = shl i32 %numerator, 1
  %overflow = icmp slt i32 %rem, 0
  %rem_shifted = shl i32 %rem, 1
  %candidate = or i32 %rem_shifted, %input_bit
  %large = icmp uge i32 %candidate, %b
  %take = or i1 %overflow, %large
  %difference = sub i32 %candidate, %b
  %rem_next = select i1 %take, i32 %difference, i32 %candidate
  %quotient_shifted = shl i32 %quotient, 1
  %quotient_bit = zext i1 %take to i32
  %quotient_next = or i32 %quotient_shifted, %quotient_bit
  %count_next = sub i16 %count, 1
  %done = icmp eq i16 %count_next, 0
  br i1 %done, label %exit, label %loop
exit:
  store i32 %rem_next, ptr %remainder, align 2
  ret i32 %quotient_next
}

define i32 @__udivsi3(i32 %a, i32 %b) noinline {
  %rem = alloca i32, align 2
  %q = call i32 @__dcpu_udivmod32(i32 %a, i32 %b, ptr %rem)
  ret i32 %q
}

define i32 @__umodsi3(i32 %a, i32 %b) noinline {
  %rem = alloca i32, align 2
  %q = call i32 @__dcpu_udivmod32(i32 %a, i32 %b, ptr %rem)
  %r = load i32, ptr %rem, align 2
  ret i32 %r
}

define i32 @__divsi3(i32 %a, i32 %b) noinline {
  %aneg = icmp slt i32 %a, 0
  %bneg = icmp slt i32 %b, 0
  %aminus = sub i32 0, %a
  %bminus = sub i32 0, %b
  %amag = select i1 %aneg, i32 %aminus, i32 %a
  %bmag = select i1 %bneg, i32 %bminus, i32 %b
  %qmag = call i32 @__udivsi3(i32 %amag, i32 %bmag)
  %negative = xor i1 %aneg, %bneg
  %minus = sub i32 0, %qmag
  %result = select i1 %negative, i32 %minus, i32 %qmag
  ret i32 %result
}

define i32 @__modsi3(i32 %a, i32 %b) noinline {
  %aneg = icmp slt i32 %a, 0
  %bneg = icmp slt i32 %b, 0
  %aminus = sub i32 0, %a
  %bminus = sub i32 0, %b
  %amag = select i1 %aneg, i32 %aminus, i32 %a
  %bmag = select i1 %bneg, i32 %bminus, i32 %b
  %rmag = call i32 @__umodsi3(i32 %amag, i32 %bmag)
  %negative = or i1 %aneg, false
  %minus = sub i32 0, %rmag
  %result = select i1 %negative, i32 %minus, i32 %rmag
  ret i32 %result
}

define internal i64 @__dcpu_udivmod64(i64 %a, i64 %b, ptr %remainder) noinline {
entry:
  %zero = icmp eq i64 %b, 0
  br i1 %zero, label %divide_by_zero, label %loop
divide_by_zero:
  store i64 0, ptr %remainder, align 2
  ret i64 0
loop:
  %count = phi i16 [ 64, %entry ], [ %count_next, %loop ]
  %numerator = phi i64 [ %a, %entry ], [ %numerator_next, %loop ]
  %quotient = phi i64 [ 0, %entry ], [ %quotient_next, %loop ]
  %rem = phi i64 [ 0, %entry ], [ %rem_next, %loop ]
  %input_bit = lshr i64 %numerator, 63
  %numerator_next = shl i64 %numerator, 1
  %overflow = icmp slt i64 %rem, 0
  %rem_shifted = shl i64 %rem, 1
  %candidate = or i64 %rem_shifted, %input_bit
  %large = icmp uge i64 %candidate, %b
  %take = or i1 %overflow, %large
  %difference = sub i64 %candidate, %b
  %rem_next = select i1 %take, i64 %difference, i64 %candidate
  %quotient_shifted = shl i64 %quotient, 1
  %quotient_bit = zext i1 %take to i64
  %quotient_next = or i64 %quotient_shifted, %quotient_bit
  %count_next = sub i16 %count, 1
  %done = icmp eq i16 %count_next, 0
  br i1 %done, label %exit, label %loop
exit:
  store i64 %rem_next, ptr %remainder, align 2
  ret i64 %quotient_next
}

define i64 @__udivdi3(i64 %a, i64 %b) noinline {
  %rem = alloca i64, align 2
  %q = call i64 @__dcpu_udivmod64(i64 %a, i64 %b, ptr %rem)
  ret i64 %q
}

define i64 @__umoddi3(i64 %a, i64 %b) noinline {
  %rem = alloca i64, align 2
  %q = call i64 @__dcpu_udivmod64(i64 %a, i64 %b, ptr %rem)
  %r = load i64, ptr %rem, align 2
  ret i64 %r
}

define i64 @__divdi3(i64 %a, i64 %b) noinline {
  %aneg = icmp slt i64 %a, 0
  %bneg = icmp slt i64 %b, 0
  %aminus = sub i64 0, %a
  %bminus = sub i64 0, %b
  %amag = select i1 %aneg, i64 %aminus, i64 %a
  %bmag = select i1 %bneg, i64 %bminus, i64 %b
  %qmag = call i64 @__udivdi3(i64 %amag, i64 %bmag)
  %negative = xor i1 %aneg, %bneg
  %minus = sub i64 0, %qmag
  %result = select i1 %negative, i64 %minus, i64 %qmag
  ret i64 %result
}

define i64 @__moddi3(i64 %a, i64 %b) noinline {
  %aneg = icmp slt i64 %a, 0
  %bneg = icmp slt i64 %b, 0
  %aminus = sub i64 0, %a
  %bminus = sub i64 0, %b
  %amag = select i1 %aneg, i64 %aminus, i64 %a
  %bmag = select i1 %bneg, i64 %bminus, i64 %b
  %rmag = call i64 @__umoddi3(i64 %amag, i64 %bmag)
  %negative = or i1 %aneg, false
  %minus = sub i64 0, %rmag
  %result = select i1 %negative, i64 %minus, i64 %rmag
  ret i64 %result
}
