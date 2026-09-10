// Emit targeted instruction/xref evidence to a private local file.
// Raw output derives from the game and must not be committed.
// Usage: -postScript X3ObjectContext.java /tmp/x3-context.txt 004c0150 00608a44 ...
// @category X3
import java.io.PrintWriter;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;

public class X3ObjectContext extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("Private output and addresses required");
        try (PrintWriter out = new PrintWriter(args[0])) {
            out.println("Program " + currentProgram.getName() + " image base " + currentProgram.getImageBase());
            for (int n = 1; n < args.length; ++n) {
                Address address = toAddr(args[n]);
                out.println("TARGET " + address);
                for (Reference ref : getReferencesTo(address)) {
                    Function caller = getFunctionContaining(ref.getFromAddress());
                    out.println("XREF " + ref.getFromAddress() + " " + ref.getReferenceType() + " function=" + (caller == null ? "?" : caller.getEntryPoint()));
                    Instruction ins = getInstructionAt(ref.getFromAddress());
                    if (ins != null) {
                        for (int i=0; i<7 && ins.getPrevious()!=null; ++i) ins=ins.getPrevious();
                        for (int i=0; i<15 && ins!=null; ++i,ins=ins.getNext()) out.println(ins.getAddress()+" "+ins);
                    }
                }
                Function function = getFunctionAt(address);
                if (function != null) {
                    out.println("FUNCTION " + function.getEntryPoint()+" "+function.getSignature());
                    var instructions=currentProgram.getListing().getInstructions(function.getBody(),true);
                    while(instructions.hasNext()) { Instruction ins=instructions.next(); out.println(ins.getAddress()+" "+ins); }
                }
            }
        }
    }
}
