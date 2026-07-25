// Find a byte pattern (arg0 = hex string, ?? = wildcard) and decompile the containing function.
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
public class FindAOB extends GhidraScript {
    @Override public void run() throws Exception {
        String hex = getScriptArgs()[0].replaceAll("\s","");
        int n = hex.length()/2;
        byte[] b = new byte[n]; byte[] m = new byte[n];
        for(int i=0;i<n;i++){ String s=hex.substring(i*2,i*2+2);
            if(s.equals("??")){b[i]=0;m[i]=0;} else {b[i]=(byte)Integer.parseInt(s,16);m[i]=(byte)0xFF;} }
        Memory mem = currentProgram.getMemory();
        Address a = mem.findBytes(currentProgram.getMinAddress(), b, m, true, monitor);
        println("IMAGEBASE "+currentProgram.getImageBase());
        if(a==null){ println("NOT FOUND"); return; }
        println("FOUND @ "+a+"  (RVA 0x"+Long.toHexString(a.getOffset()-currentProgram.getImageBase().getOffset())+")");
        Function f = currentProgram.getFunctionManager().getFunctionContaining(a);
        if(f!=null){ println("in "+f.getName()+" @"+f.getEntryPoint());
            DecompInterface di=new DecompInterface(); di.openProgram(currentProgram);
            DecompileResults r=di.decompileFunction(f,120,monitor);
            if(r!=null&&r.getDecompiledFunction()!=null) for(String ln:r.getDecompiledFunction().getC().split("\n")) println(ln);
            di.dispose();
        } else println("(no containing function)");
        println("=== end ===");
    }
}
