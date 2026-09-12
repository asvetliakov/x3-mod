// Emit camera/frame-routine evidence to a private local file.
// Raw output derives from the game and must not be committed.
// Usage: -postScript X3CameraState.java /tmp/x3-camera/out.txt <spec>...
//   dec:ADDR    decompile the function at/containing ADDR
//   ins:ADDR    full instruction listing of the function at/containing ADDR
//   data:ADDR   references to a data address, classified read/write, with context
//   sym:TEXT    symbols whose name contains TEXT (case-insensitive) and their references
//   disp:HEX    sweep "mov reg,[r+disp]" followed by CALL/JMP on that register
//   ptr:ADDR:N  dump N pointer-sized words at ADDR
//   load:HEX    every instruction with an operand ending in that displacement
//   range:ADDR:N  N instructions starting at ADDR
//   txt:STRING  every instruction whose text contains STRING
// @category X3
import java.io.PrintWriter;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class X3CameraState extends GhidraScript {
    private PrintWriter out;
    private DecompInterface decomp;

    private Function owner(Address a) {
        Function f = getFunctionAt(a);
        return f != null ? f : getFunctionContaining(a);
    }

    private void context(Address a, int before, int after) {
        Instruction ins = getInstructionAt(a);
        if (ins == null) { out.println("  (no instruction)"); return; }
        for (int i = 0; i < before && ins.getPrevious() != null; ++i) ins = ins.getPrevious();
        for (int i = 0; i < before + after + 1 && ins != null; ++i, ins = ins.getNext()) {
            out.println("  " + (ins.getAddress().equals(a) ? "*" : " ") + ins.getAddress() + "  " + ins);
        }
    }

    private void decompile(Address a) throws Exception {
        Function f = owner(a);
        if (f == null) { out.println("NO FUNCTION AT " + a); return; }
        out.println("=== DECOMPILE " + f.getEntryPoint() + " " + f.getName() + " ===");
        DecompileResults r = decomp.decompileFunction(f, 180, monitor);
        if (r != null && r.getDecompiledFunction() != null) out.println(r.getDecompiledFunction().getC());
        else out.println("(decompilation failed: " + (r == null ? "null" : r.getErrorMessage()) + ")");
    }

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("Private output path and specs required");
        decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(args[0])) {
            out = w;
            out.println("Program " + currentProgram.getName() + " base " + currentProgram.getImageBase());
            for (int n = 1; n < args.length; ++n) {
                String spec = args[n];
                int colon = spec.indexOf(':');
                String kind = colon < 0 ? "dec" : spec.substring(0, colon);
                String rest = colon < 0 ? spec : spec.substring(colon + 1);
                out.println();
                out.println("##### " + spec);
                switch (kind) {
                    case "dec": decompile(toAddr(rest)); break;
                    case "ins": {
                        Function f = owner(toAddr(rest));
                        if (f == null) { out.println("NO FUNCTION"); break; }
                        out.println("=== INSTRUCTIONS " + f.getEntryPoint() + " " + f.getName()
                            + " body " + f.getBody().getMinAddress() + "-" + f.getBody().getMaxAddress() + " ===");
                        var it = currentProgram.getListing().getInstructions(f.getBody(), true);
                        while (it.hasNext()) { Instruction i = it.next(); out.println(i.getAddress() + "  " + i); }
                        break;
                    }
                    case "data": {
                        Address a = toAddr(rest);
                        out.println("=== REFS TO " + a + " ===");
                        for (Reference ref : getReferencesTo(a)) {
                            Function f = getFunctionContaining(ref.getFromAddress());
                            out.println("REF " + ref.getFromAddress() + " " + ref.getReferenceType()
                                + (ref.getReferenceType().isWrite() ? " [WRITE]" : ref.getReferenceType().isRead() ? " [READ]" : "")
                                + " in=" + (f == null ? "?" : f.getEntryPoint() + "/" + f.getName()));
                            context(ref.getFromAddress(), 6, 6);
                        }
                        break;
                    }
                    case "sym": {
                        SymbolIterator si = currentProgram.getSymbolTable().getAllSymbols(true);
                        while (si.hasNext()) {
                            Symbol s = si.next();
                            if (!s.getName().toLowerCase().contains(rest.toLowerCase())) continue;
                            out.println("SYMBOL " + s.getAddress() + " " + s.getName() + " " + s.getSymbolType());
                            for (Reference ref : getReferencesTo(s.getAddress())) {
                                Function f = getFunctionContaining(ref.getFromAddress());
                                out.println("  REF " + ref.getFromAddress() + " " + ref.getReferenceType()
                                    + " in=" + (f == null ? "?" : f.getEntryPoint() + "/" + f.getName()));
                            }
                        }
                        break;
                    }
                    case "disp": {
                        long disp = Long.decode(rest);
                        var it = currentProgram.getListing().getInstructions(true);
                        while (it.hasNext()) {
                            Instruction i = it.next();
                            if (!i.getMnemonicString().equals("MOV") || i.getNumOperands() != 2) continue;
                            String op1 = i.getDefaultOperandRepresentation(1);
                            if (!op1.contains("0x" + Long.toHexString(disp) + "]")) continue;
                            String reg = i.getDefaultOperandRepresentation(0);
                            Instruction j = i.getNext();
                            for (int k = 0; k < 12 && j != null; ++k, j = j.getNext()) {
                                String m = j.getMnemonicString();
                                if ((m.equals("CALL") || m.equals("JMP"))
                                        && j.getDefaultOperandRepresentation(0).equals(reg)) {
                                    Function f = getFunctionContaining(j.getAddress());
                                    out.println("SITE load=" + i.getAddress() + " " + m + "=" + j.getAddress()
                                        + " reg=" + reg + " in=" + (f == null ? "?" : f.getEntryPoint() + "/" + f.getName()));
                                    break;
                                }
                                if (m.equals("MOV") && j.getDefaultOperandRepresentation(0).equals(reg)) break;
                            }
                        }
                        break;
                    }
                    case "load": {
                        long disp = Long.decode(rest);
                        String pat = "0x" + Long.toHexString(disp) + "]";
                        var it = currentProgram.getListing().getInstructions(true);
                        while (it.hasNext()) {
                            Instruction i = it.next();
                            boolean hit = false;
                            for (int k = 0; k < i.getNumOperands(); ++k)
                                if (i.getDefaultOperandRepresentation(k).endsWith(pat)) hit = true;
                            if (!hit) continue;
                            Function f = getFunctionContaining(i.getAddress());
                            out.println("LOAD " + i.getAddress() + "  " + i
                                + "  in=" + (f == null ? "?" : f.getEntryPoint() + "/" + f.getName()));
                        }
                        break;
                    }
                    case "txt": {
                        var it = currentProgram.getListing().getInstructions(true);
                        while (it.hasNext()) {
                            Instruction i = it.next();
                            if (!i.toString().contains(rest)) continue;
                            Function f = getFunctionContaining(i.getAddress());
                            out.println("TXT " + i.getAddress() + "  " + i
                                + "  in=" + (f == null ? "?" : f.getEntryPoint() + "/" + f.getName()));
                        }
                        break;
                    }
                    case "range": {
                        String[] p2 = rest.split(":");
                        Address a = toAddr(p2[0]);
                        int count = p2.length > 1 ? Integer.parseInt(p2[1]) : 40;
                        Instruction ins = getInstructionAt(a);
                        for (int i = 0; i < count && ins != null; ++i, ins = ins.getNext())
                            out.println("  " + ins.getAddress() + "  " + ins);
                        break;
                    }
                    case "ptr": {
                        String[] p = rest.split(":");
                        Address a = toAddr(p[0]);
                        int count = p.length > 1 ? Integer.parseInt(p[1]) : 8;
                        for (int i = 0; i < count; ++i) {
                            Address at = a.add(i * 4L);
                            int v = getInt(at);
                            out.println("  " + at + " = 0x" + Integer.toHexString(v));
                        }
                        break;
                    }
                    default: out.println("UNKNOWN SPEC " + spec);
                }
            }
        } finally {
            decomp.dispose();
        }
    }
}
