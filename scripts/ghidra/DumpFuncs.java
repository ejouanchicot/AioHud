// Decompiles the PluginManager per-slot forwarder functions and, for each,
// reports its callers and whether any caller's body touches Direct3D / input /
// packet APIs -- so we can label which IPlugin slot is render / init / command.
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class DumpFuncs extends GhidraScript {
    // forwarders for IPlugin slots 17..32 (from FindVtableCalls)
    long[] addrs = {
        0x10060410L, 0x10060460L, 0x100604b0L, 0x10060500L, 0x10060550L,
        0x100605a0L, 0x100605f0L, 0x10060640L, 0x10060690L, 0x100606e0L,
        0x10060730L, 0x10060780L, 0x100607d0L, 0x10060820L, 0x10060870L,
        0x10060020L
    };
    int[] slotOf = {17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};

    String hintFor(Function f) {
        if (f == null) return "";
        StringBuilder s = new StringBuilder();
        Listing l = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        InstructionIterator it = l.getInstructions(f.getBody(), true);
        int g = 0;
        while (it.hasNext() && g++ < 3000) {
            Instruction ins = it.next();
            if (!ins.getFlowType().isCall()) continue;
            for (Reference r : ins.getReferencesFrom()) {
                Function cf = fm.getFunctionAt(r.getToAddress());
                if (cf == null) continue;
                String n = cf.getName().toLowerCase();
                if (n.contains("d3d") || n.contains("direct3d") || n.contains("present")
                    || n.contains("endscene") || n.contains("beginscene") || n.contains("reset")
                    || n.contains("drawprim") || n.contains("settexture") || n.contains("input")
                    || n.contains("keyboard") || n.contains("mouse") || n.contains("getdevicestate"))
                    if (s.indexOf(cf.getName()) < 0) s.append(cf.getName()).append(" ");
            }
        }
        return s.toString();
    }

    @Override
    public void run() throws Exception {
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        FunctionManager fm = currentProgram.getFunctionManager();

        for (int i = 0; i < addrs.length; i++) {
            Address a = toAddr(addrs[i]);
            Function f = fm.getFunctionAt(a);
            println("\n##### slot " + slotOf[i] + "  forwarder " + a + "  " +
                    (f != null ? f.getSignature().getPrototypeString() : "(no func)"));
            if (f == null) continue;

            // callers + their D3D/input hints
            Set<Function> callers = f.getCallingFunctions(monitor);
            for (Function c : callers) {
                println("   caller " + c.getEntryPoint() + " " + c.getName() + "   hint:[" + hintFor(c) + "]");
            }

            // decompiled body (trimmed)
            DecompileResults res = di.decompileFunction(f, 45, monitor);
            if (res != null && res.getDecompiledFunction() != null) {
                String c = res.getDecompiledFunction().getC();
                String[] lines = c.split("\n");
                for (int k = 0; k < Math.min(lines.length, 22); k++) println("   | " + lines[k]);
            }
        }
        di.dispose();
        println("\n=== end ===");
    }
}
