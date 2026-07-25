// Decompile each function-address arg (hex, no 0x).
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
public class DecompMany extends GhidraScript {
    @Override public void run() throws Exception {
        DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
        FunctionManager fm = currentProgram.getFunctionManager();
        for (String a : getScriptArgs()) { if(a==null||a.isEmpty())continue;
            long addr = Long.parseLong(a.trim(),16);
            Function f = fm.getFunctionAt(toAddr(addr));
            println("\n>>>>> "+toAddr(addr)+"  "+(f!=null?f.getSignature().getPrototypeString():"(no func)"));
            if (f==null) continue;
            DecompileResults res = di.decompileFunction(f, 90, monitor);
            if (res!=null && res.getDecompiledFunction()!=null)
                for (String ln : res.getDecompiledFunction().getC().split("\n")) println(ln);
        }
        di.dispose(); println("\n===== end =====");
    }
}
