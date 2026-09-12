// Read-only structural survey for the script/XML load stall: resolves containing
// functions for sampled addresses, lists callers/callees (with imported-thunk
// names), and reports which CRT/Win32 calls a function makes and how often.
// Usage: -postScript X3ScriptLoadStudy.java <addr> [<addr> ...]
// @category X3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.RefType;
import java.util.LinkedHashMap;
import java.util.Map;

public class X3ScriptLoadStudy extends GhidraScript {

    private String describe(Function f) {
        if (f == null) return "unknown";
        String extra = f.isThunk() ? " [thunk->" + f.getThunkedFunction(true).getName() + "]" : "";
        return f.getEntryPoint() + " " + f.getName() + " size=" + f.getBody().getNumAddresses() + extra;
    }

    private String targetName(Address to) {
        Function f = getFunctionAt(to);
        if (f != null) {
            if (f.isThunk()) return f.getThunkedFunction(true).getName();
            return f.getName();
        }
        ghidra.program.model.symbol.Symbol s = getSymbolAt(to);
        return s == null ? to.toString() : s.getName();
    }

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        for (String a : args) {
            Address addr;
            if (a.startsWith("dis:")) {
                String[] parts = a.substring(4).split(":");
                Address start = toAddr(parts[0]);
                Address end = toAddr(parts[1]);
                println("==== DISASSEMBLY " + start + " .. " + end);
                Instruction ins = getInstructionAt(start);
                while (ins != null && ins.getAddress().compareTo(end) <= 0) {
                    println("  " + ins.getAddress() + "  " + ins.toString());
                    ins = ins.getNext();
                }
                continue;
            }
            if (a.startsWith("fdis:")) {
                Function fn = getFunctionContaining(toAddr(a.substring(5)));
                println("==== FUNCTION DISASSEMBLY " + (fn == null ? "none" : fn.getEntryPoint().toString()));
                if (fn == null) continue;
                InstructionIterator fit = currentProgram.getListing().getInstructions(fn.getBody(), true);
                while (fit.hasNext()) { Instruction i2 = fit.next(); println("  " + i2.getAddress() + "  " + i2.toString()); }
                continue;
            }
            if (a.startsWith("name:")) {
                String want = a.substring(5);
                Address found = null;
                for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
                    if (fn.getName().equals(want)) { println("SYMBOL " + want + " = " + fn.getEntryPoint()); found = fn.getEntryPoint(); }
                }
                if (found == null) { println("SYMBOL " + want + " = not found"); continue; }
                addr = found;
            } else {
                addr = toAddr(a);
            }
            Function f = getFunctionContaining(addr);
            println("==== ADDRESS " + a + " -> " + describe(f));
            if (f == null) continue;
            println("  CALLERS:");
            for (Reference r : getReferencesTo(f.getEntryPoint())) {
                if (!r.getReferenceType().isCall() && !r.getReferenceType().isData()) continue;
                println("    " + r.getFromAddress() + " " + r.getReferenceType() + " in " +
                    describe(getFunctionContaining(r.getFromAddress())));
            }
            println("  CALLEES (site -> target, in order):");
            Map<String, Integer> counts = new LinkedHashMap<>();
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                if (!ins.getFlowType().isCall()) continue;
                Address[] flows = ins.getFlows();
                String name = flows.length == 0 ? "indirect(" + ins.toString() + ")" : targetName(flows[0]);
                println("    " + ins.getAddress() + " -> " + name);
                counts.merge(name, 1, Integer::sum);
            }
            println("  CALLEE COUNTS:");
            for (Map.Entry<String, Integer> e : counts.entrySet()) println("    " + e.getValue() + "x " + e.getKey());
        }
    }
}
