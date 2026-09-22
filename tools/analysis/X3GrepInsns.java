// Read-only helper: grep instruction text in given functions (or all if none). Usage: out regex [fnaddr...]
// @category X3
import java.io.PrintWriter;
import java.util.regex.Pattern;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
public class X3GrepInsns extends GhidraScript {
  @Override public void run() throws Exception {
    String[] a = getScriptArgs();
    Pattern p = Pattern.compile(a[1]);
    try (PrintWriter o = new PrintWriter(a[0])) {
      Listing l = currentProgram.getListing();
      if (a.length > 2) {
        for (int i = 2; i < a.length; ++i) {
          Function f = getFunctionAt(toAddr(a[i]));
          if (f == null) { o.println("nofn " + a[i]); continue; }
          for (Instruction ins : l.getInstructions(f.getBody(), true)) {
            String s = ins.toString();
            if (p.matcher(s).find()) o.println(a[i] + " " + ins.getAddress() + " " + s);
          }
        }
      } else {
        for (Instruction ins : l.getInstructions(true)) {
          String s = ins.toString();
          if (p.matcher(s).find()) { Function f = getFunctionContaining(ins.getAddress());
            o.println((f==null?"?":f.getEntryPoint().toString()) + " " + ins.getAddress() + " " + s); }
        }
      }
    }
  }
}
