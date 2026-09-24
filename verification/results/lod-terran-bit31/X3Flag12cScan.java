// Read-only: list every instruction that addresses [reg + 0x12c..0x12f] (any base, incl. SIB),
// and for register loads from that field a forward fall-through trace of up to 14 instructions
// (stops at a redefinition of the loaded register, RET or unconditional JMP).
// Output TSV: kind fn addr insn | trace. Usage: -postScript X3Flag12cScan.java out.tsv
// @category X3
import java.io.PrintWriter;
import java.util.regex.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
public class X3Flag12cScan extends GhidraScript {
  static final Pattern MEM = Pattern.compile("\\[([A-Z]+)(?: \\+ [A-Z]+(?:\\*\\d)?)? \\+ 0x12([c-f])\\]");
  @Override public void run() throws Exception {
    String out = getScriptArgs()[0];
    Listing l = currentProgram.getListing();
    try (PrintWriter o = new PrintWriter(out)) {
      for (Instruction ins : l.getInstructions(true)) {
        String s = ins.toString();
        Matcher m = MEM.matcher(s);
        if (!m.find()) continue;
        Function f = getFunctionContaining(ins.getAddress());
        String fn = f == null ? "?" : f.getEntryPoint().toString();
        String mn = ins.getMnemonicString();
        int comma = s.indexOf(',');
        boolean memFirst = comma < 0 ? true : s.indexOf('[') < comma;
        String kind;
        String loadReg = null;
        if (mn.equals("TEST") || mn.equals("CMP") || mn.equals("BT")) kind = "read";
        else if (mn.equals("PUSH")) kind = "push";
        else if (memFirst && !mn.equals("LEA")) kind = "write";
        else if (mn.equals("LEA")) kind = "lea";
        else { kind = "load"; loadReg = s.substring(s.indexOf(' ') + 1, comma).trim(); }
        StringBuilder tr = new StringBuilder();
        if (loadReg != null) {
          Instruction n = ins.getNext();
          for (int i = 0; i < 14 && n != null; ++i) {
            String ns = n.toString();
            tr.append(" ; ").append(n.getAddress()).append(" ").append(ns);
            String nm = n.getMnemonicString();
            int nc = ns.indexOf(',');
            String dst = nc > 0 ? ns.substring(ns.indexOf(' ') + 1, nc).trim() : "";
            if (nm.equals("RET") || nm.equals("JMP")) break;
            if ((nm.equals("MOV") || nm.equals("MOVZX") || nm.equals("LEA") || nm.equals("POP") || nm.equals("MOVSX")) && dst.equals(loadReg)) break;
            if (nm.equals("POP") && ns.endsWith(loadReg)) break;
            if (nm.equals("CALL")) { tr.append(" [call]"); }
            n = n.getNext();
          }
        }
        o.println(kind + "\t" + fn + "\t" + ins.getAddress() + "\t" + s + "\t" + tr);
      }
    }
  }
}
