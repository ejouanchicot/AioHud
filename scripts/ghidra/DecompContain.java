// Decompile the function CONTAINING arg0 (hex). @category Windower
import ghidra.app.script.GhidraScript; import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
public class DecompContain extends GhidraScript {
  @Override public void run() throws Exception {
    long addr=Long.parseLong(getScriptArgs()[0],16);
    Function f=currentProgram.getFunctionManager().getFunctionContaining(toAddr(addr));
    if(f==null){ println("(no containing func) @"+toAddr(addr)); println("===== end ====="); return; }
    println(">>>>> containing "+f.getEntryPoint()+"  "+f.getSignature().getPrototypeString());
    DecompInterface di=new DecompInterface(); di.openProgram(currentProgram);
    DecompileResults r=di.decompileFunction(f,90,monitor);
    if(r!=null&&r.getDecompiledFunction()!=null) for(String ln:r.getDecompiledFunction().getC().split("\n")) println(ln);
    di.dispose(); println("===== end =====");
  }
}
