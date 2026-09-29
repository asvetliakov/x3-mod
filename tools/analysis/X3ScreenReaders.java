// Read-only helper: list instructions that read [0x00606f38] and, within the next 10 instructions,
// perform word accesses at +0x4/+0x6 (screen width/height) or dword +0x28..+0x34.
// @category X3
import java.io.PrintWriter;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
public class X3ScreenReaders extends GhidraScript {
  @Override public void run() throws Exception {
    String[] a = getScriptArgs();
    try (PrintWriter o = new PrintWriter(a[0])) {
      Listing l = currentProgram.getListing();
      for (Instruction ins : l.getInstructions(true)) {
        String s = ins.toString();
        if (!s.contains("[0x00606f38]")) continue;
        Function f = getFunctionContaining(ins.getAddress());
        StringBuilder hit = new StringBuilder();
        Instruction n = ins;
        for (int i = 0; i < 12 && n != null; ++i) {
          n = n.getNext(); if (n == null) break;
          String t = n.toString();
          if (t.matches(".*word ptr \\[E[A-Z]{2} \\+ 0x[46]\\].*") || t.matches(".*dword ptr \\[E[A-Z]{2} \\+ 0x(28|2c|30|34)\\].*"))
            hit.append(" | ").append(n.getAddress()).append(" ").append(t);
        }
        o.println((f==null?"?":f.getEntryPoint().toString()) + " " + ins.getAddress() + " " + s + hit);
      }
    }
  }
}
