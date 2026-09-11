// Bounded callsite ownership/xrefs/decompilation for local callback research.
// Invoke headless with -readOnly -noanalysis and an existing analyzed program.
// Usage: -postScript X3CallbackResearch.java /tmp/private-output.c 004d3620 ...
// Generated decompilation is copyrighted program material: keep it untracked.
// @category X3
import java.io.PrintWriter;
import java.util.HashSet;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
public class X3CallbackResearch extends GhidraScript {
 public void run() throws Exception {
  String[] a=getScriptArgs();
  if(a.length<2)throw new IllegalArgumentException("Output path and at least one callsite required");
  DecompInterface d=new DecompInterface(); d.openProgram(currentProgram);
  HashSet<String> seen=new HashSet<>();
  try(PrintWriter o=new PrintWriter(a[0])) {
   for(int i=1;i<a.length;i++) { Function f=getFunctionContaining(toAddr(a[i]));
    o.println("SITE "+a[i]+" OWNER "+(f==null?"none":f.getEntryPoint()));
    if(f==null||!seen.add(f.getEntryPoint().toString()))continue;
    for(Reference r:getReferencesTo(f.getEntryPoint()))o.println("REF "+r.getFromAddress()+" "+r.getReferenceType());
    var result=d.decompileFunction(f,30,monitor); if(result.decompileCompleted())o.println(result.getDecompiledFunction().getC());
   }
  } finally{d.dispose();}
 }
}
