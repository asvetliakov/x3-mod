// Resolve sampled preferred addresses to containing functions and local callers.
// Decompiler output is private local research and must not be committed.
// @category X3
import java.io.PrintWriter;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
public class X3SampleFunctions extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        if(args.length<2)throw new IllegalArgumentException("local output path, preferred addresses required");
        DecompInterface decompiler=new DecompInterface();
        try(PrintWriter output=new PrintWriter(args[0])){
            decompiler.openProgram(currentProgram);
            for(int i=1;i<args.length;++i){
                Address address=toAddr(args[i]);Function f=getFunctionContaining(address);
                output.println("SAMPLE "+address+" function="+(f==null?"unknown":f.getEntryPoint()+" "+f.getName()));
                if(f==null)continue;
                for(Reference ref:getReferencesTo(f.getEntryPoint())){
                    Function caller=getFunctionContaining(ref.getFromAddress());
                    output.println("CALLER "+ref.getFromAddress()+" "+ref.getReferenceType()+" function="+(caller==null?"unknown":caller.getEntryPoint()+" "+caller.getName()));
                }
                DecompileResults result=decompiler.decompileFunction(f,15,monitor);
                if(result.decompileCompleted())output.println(result.getDecompiledFunction().getC());
            }
        }finally{decompiler.dispose();}
    }
}
