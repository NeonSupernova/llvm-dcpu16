; octet16 ABI startup. Code labels and SP count hardware words.
; This program stops at word address 8 with A = 42.
SET SP, 0x7f00
SET A, 20
SET B, 22
JSR add
halt:
SET PC, halt
