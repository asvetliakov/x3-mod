// Decompile explicitly requested functions to a private local research file.
// Never commit output: it is derived game implementation, not original mod code.
// Usage: -postScript X3DecompileFunctions.java /tmp/output.txt 004bae10 ...
// @category X3
import java.io.PrintWriter;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;

public class X3DecompileFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("Output path and function addresses required");
        DecompInterface decompiler = new DecompInterface();
        try (PrintWriter output = new PrintWriter(args[0])) {
            decompiler.openProgram(currentProgram);
            for (int i = 1; i < args.length; ++i) {
                Function function = getFunctionAt(toAddr(args[i]));
                if (function == null) { output.println("No function at " + args[i]); continue; }
                DecompileResults results = decompiler.decompileFunction(function, 30, monitor);
                output.println("// Function " + args[i]);
                if (results.decompileCompleted()) output.println(results.getDecompiledFunction().getC());
                else output.println(results.getErrorMessage());
            }
        } finally { decompiler.dispose(); }
    }
}
