// Fully decompile one function (FFXIDB Init / slot 2 = FUN_10072450) to see how
// the plugin uses the Windower host object it receives at init (which host vtable
// method yields the IDirect3DDevice8 or the text/primitive drawing API).
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;

public class DecompOne extends GhidraScript {
    @Override
    public void run() throws Exception {
        String arg = getScriptArgs().length > 0 ? getScriptArgs()[0] : "10072450";
        long addr = Long.parseLong(arg, 16);
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        Function f = currentProgram.getFunctionManager().getFunctionAt(toAddr(addr));
        println(">>>>> " + toAddr(addr) + "  " +
                (f != null ? f.getSignature().getPrototypeString() : "(no func)"));
        if (f != null) {
            DecompileResults res = di.decompileFunction(f, 90, monitor);
            if (res != null && res.getDecompiledFunction() != null)
                for (String ln : res.getDecompiledFunction().getC().split("\n")) println(ln);
        }
        di.dispose();
        println("===== end =====");
    }
}
