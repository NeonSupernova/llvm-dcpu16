; Compile with dcpu16-llc, then combine with start.s before assembling.
define i16 @add(i16 %a, i16 %b) {
  %result = add i16 %a, %b
  ret i16 %result
}
