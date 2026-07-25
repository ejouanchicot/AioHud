// Decompiles a fixed list of functions (by address) and prints their C.
// Used to understand FFXIDB's IPlugin method overrides (metadata + command).
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;

public class DecompList extends GhidraScript {
    // Windower PrimitiveObject methods [0..9] -- label pos/size/color/visible/texture
    long[] addrs = {
        0x100756a0L, 0x100756f0L, 0x100757b0L, 0x100758a0L, 0x10075a50L,
        0x100759b0L, 0x10075750L, 0x10076110L, 0x10076170L, 0x10075350L
    };
    @Override
    public void run() throws Exception {
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        FunctionManager fm = currentProgram.getFunctionManager();
        for (int i = 0; i < addrs.length; i++) {
            Address a = toAddr(addrs[i]);
            Function f = fm.getFunctionAt(a);
            println(">>>>> slot " + i + "  " + a + "  " +
                    (f != null ? f.getSignature().getPrototypeString() : "(no func)"));
            if (f == null) continue;
            DecompileResults res = di.decompileFunction(f, 45, monitor);
            if (res != null && res.getDecompiledFunction() != null) {
                String[] lines = res.getDecompiledFunction().getC().split("\n");
                for (int k = 0; k < Math.min(lines.length, 26); k++) println("  | " + lines[k]);
            }
        }
        di.dispose();
        println("===== end =====");
    }
}
