// Read-only loading-orchestration exploration for the documented X3AP.exe hash.
// Batches several query kinds in one headless run so the project is opened once.
// Output is derived game implementation detail: write it under /tmp only, never
// into the repository.
//
// Usage:
//   -postScript X3LoadingOrchestration.java /tmp/out.txt \
//        xrefs 0055f994 0054d428 -- callers 004bb470 3 -- decompile 004bb470 \
//        -- listing 004e8780 120 -- callees 004e9210
//
// Commands (separated by the literal token "--"):
//   xrefs ADDR...          references to each address with the containing function
//   callers ADDR DEPTH     recursive caller tree up to DEPTH levels
//   callees ADDR           direct call targets from a function
//   decompile ADDR...      C decompilation of each function
//   listing ADDR COUNT     COUNT instructions of raw disassembly from ADDR
//   funcs ADDR...          entry/size/parameter summary
//   strings ADDR...        printable constants referenced inside each function
//   search HEXBYTES        byte-pattern search over initialised memory
// @category X3
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.RefType;

public class X3LoadingOrchestration extends GhidraScript {
    private PrintWriter out;
    private DecompInterface decompiler;

    private String label(Function f) {
        return f == null ? "unknown" : (f.getName() + "@" + f.getEntryPoint());
    }

    private void xrefs(Address address) {
        out.println("XREFS TO " + address);
        int n = 0;
        for (Reference ref : getReferencesTo(address)) {
            Function owner = getFunctionContaining(ref.getFromAddress());
            out.println("  " + ref.getFromAddress() + " " + ref.getReferenceType()
                + " in " + label(owner));
            if (++n > 400) { out.println("  ... truncated"); break; }
        }
        if (n == 0) out.println("  (none)");
    }

    private void callers(Address address, int depth, int level, Set<Address> seen) {
        if (level > depth) return;
        Function target = getFunctionContaining(address);
        String pad = "  ".repeat(level);
        if (target == null) { out.println(pad + "no function at " + address); return; }
        if (!seen.add(target.getEntryPoint())) {
            out.println(pad + label(target) + " (already expanded)");
            return;
        }
        out.println(pad + label(target) + " size=" + target.getBody().getNumAddresses());
        Set<Address> parents = new HashSet<>();
        for (Reference ref : getReferencesTo(target.getEntryPoint())) {
            if (!ref.getReferenceType().isCall() && !ref.getReferenceType().isData()
                && !ref.getReferenceType().isJump()) continue;
            Function owner = getFunctionContaining(ref.getFromAddress());
            if (owner == null) { out.println(pad + "  <- data/unknown " + ref.getFromAddress()
                + " " + ref.getReferenceType()); continue; }
            if (owner.getEntryPoint().equals(target.getEntryPoint())) continue;
            if (!parents.add(owner.getEntryPoint())) continue;
            out.println(pad + "  <- " + ref.getFromAddress() + " " + ref.getReferenceType());
            callers(owner.getEntryPoint(), depth, level + 2, seen);
        }
        if (parents.isEmpty()) out.println(pad + "  <- (no callers found)");
    }

    private void callees(Address address) {
        Function f = getFunctionContaining(address);
        if (f == null) { out.println("no function at " + address); return; }
        out.println("CALLEES OF " + label(f));
        Instruction inst = getInstructionAt(f.getEntryPoint());
        Set<String> seen = new HashSet<>();
        while (inst != null && f.getBody().contains(inst.getAddress())) {
            for (Reference ref : inst.getReferencesFrom()) {
                if (!ref.getReferenceType().isCall()) continue;
                Function callee = getFunctionAt(ref.getToAddress());
                String text = inst.getAddress() + " -> " + ref.getToAddress() + " "
                    + (callee == null ? "?" : callee.getName());
                if (seen.add(text)) out.println("  " + text);
            }
            inst = inst.getNext();
        }
    }

    private void decompile(Address address) throws Exception {
        Function f = getFunctionContaining(address);
        if (f == null) { out.println("no function at " + address); return; }
        out.println("// FUNCTION " + label(f));
        DecompileResults r = decompiler.decompileFunction(f, 60, monitor);
        out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : r.getErrorMessage());
    }

    private void listing(Address address, int count) {
        out.println("LISTING " + address + " x" + count);
        Instruction inst = getInstructionAt(address);
        if (inst == null) inst = getInstructionAfter(address);
        for (int i = 0; i < count && inst != null; ++i) {
            StringBuilder sb = new StringBuilder();
            sb.append(inst.getAddress()).append("  ").append(inst.toString());
            for (Reference ref : inst.getReferencesFrom()) {
                if (ref.getReferenceType() == RefType.FALL_THROUGH) continue;
                Object o = getDataAt(ref.getToAddress());
                sb.append("   ; ").append(ref.getReferenceType()).append(" ").append(ref.getToAddress());
                if (o != null) sb.append(" = ").append(o.toString());
            }
            out.println("  " + sb);
            inst = inst.getNext();
        }
    }

    private void strConsts(Address address) {
        Function f = getFunctionContaining(address);
        if (f == null) { out.println("no function at " + address); return; }
        out.println("STRING CONSTANTS IN " + label(f));
        Instruction inst = getInstructionAt(f.getEntryPoint());
        Set<String> seen = new HashSet<>();
        while (inst != null && f.getBody().contains(inst.getAddress())) {
            for (Reference ref : inst.getReferencesFrom()) {
                if (ref.getReferenceType().isCall()) continue;
                Address to = ref.getToAddress();
                StringBuilder sb = new StringBuilder();
                for (int i = 0; i < 96; ++i) {
                    int b;
                    try { b = getByte(to.add(i)) & 0xff; } catch (Exception e) { break; }
                    if (b == 0) break;
                    if (b < 0x20 || b > 0x7e) { sb.setLength(0); break; }
                    sb.append((char) b);
                }
                if (sb.length() >= 3) {
                    String text = to + " \"" + sb + "\"";
                    if (seen.add(text)) out.println("  " + inst.getAddress() + " " + text);
                }
            }
            inst = inst.getNext();
        }
    }

    private void funcs(Address address) {
        Function f = getFunctionContaining(address);
        if (f == null) { out.println("no function at " + address); return; }
        out.println("FUNC " + label(f) + " size=" + f.getBody().getNumAddresses()
            + " params=" + f.getParameterCount() + " conv=" + f.getCallingConventionName()
            + " callers=" + f.getCallingFunctions(monitor).size());
    }

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("output path and commands required");
        decompiler = new DecompInterface();
        out = new PrintWriter(args[0]);
        try {
            decompiler.openProgram(currentProgram);
            List<String> cmd = new ArrayList<>();
            for (int i = 1; i <= args.length; ++i) {
                if (i == args.length || args[i].equals("--")) {
                    if (!cmd.isEmpty()) dispatch(cmd);
                    cmd = new ArrayList<>();
                } else cmd.add(args[i]);
            }
        } finally { out.close(); decompiler.dispose(); }
    }

    private void dispatch(List<String> cmd) throws Exception {
        String kind = cmd.get(0);
        out.println("================ " + String.join(" ", cmd));
        switch (kind) {
            case "xrefs":
                for (int i = 1; i < cmd.size(); ++i) xrefs(toAddr(cmd.get(i)));
                break;
            case "callers":
                callers(toAddr(cmd.get(1)), Integer.parseInt(cmd.get(2)), 0, new HashSet<>());
                break;
            case "callees":
                for (int i = 1; i < cmd.size(); ++i) callees(toAddr(cmd.get(i)));
                break;
            case "decompile":
                for (int i = 1; i < cmd.size(); ++i) decompile(toAddr(cmd.get(i)));
                break;
            case "listing":
                listing(toAddr(cmd.get(1)), Integer.parseInt(cmd.get(2)));
                break;
            case "strings":
                for (int i = 1; i < cmd.size(); ++i) strConsts(toAddr(cmd.get(i)));
                break;
            case "funcs":
                for (int i = 1; i < cmd.size(); ++i) funcs(toAddr(cmd.get(i)));
                break;
            case "search": {
                byte[] pattern = new byte[cmd.get(1).length() / 2];
                for (int i = 0; i < pattern.length; ++i)
                    pattern[i] = (byte) Integer.parseInt(cmd.get(1).substring(2 * i, 2 * i + 2), 16);
                Address at = currentProgram.getMinAddress();
                for (int n = 0; n < 50 && at != null; ++n) {
                    at = find(at, pattern);
                    if (at == null) break;
                    out.println("  hit " + at);
                    at = at.add(1);
                }
                break;
            }
            default: out.println("unknown command " + kind);
        }
        out.println();
    }
}
