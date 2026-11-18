# Initialize matching x86 encoder candidates

Guest instruction samples repeatedly landed in the encoder's bytewise clear.
The encoder started by clearing a candidate for every catalogue row, even
when that row described a different mnemonic.

Check the mnemonic before matching the rest of the form. Clear the candidate
after the full match and before encoding into it. Keep catalogue order, the
shortest-encoding rule and the existing error path. This follows the same
initialization pattern as the decoder.

A checked i386 Linux replay of `cupidc_frontend.cc` fell from 248.034 to
104.240 seconds with the same allocator and compiler input. Both runs
produced the same 1,024,260-byte object and made 295,646 underlying heap
allocations. All 36 selected x86, CupidASM and CupidDis contracts passed,
including the full catalogue and rejected operands and forms.
