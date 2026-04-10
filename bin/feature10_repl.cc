// Feature 10 demo: TempleOS-style shell REPL
// Type CupidC directly at the normal shell prompt:
//   U32 x = 7;
//   x + 5;
//   ans;
//
// Multi-line blocks stay open until braces balance:
//   U32 Add(U32 a, U32 b) {
//     return a + b;
//   }
//
// The shell prompt reports the last result on the next prompt and
// failed snippets do not corrupt the persistent REPL session.

Print("Feature10 file-mode check via cc/cupidc\n");
