// For each PluginManager per-slot forwarder (slots 17..32), find its callers and
// report whether a caller touches Direct3D (d3d8 imports, device vtable calls, or
// present/endscene/beginscene strings). The render slot's forwarder is called from
// the D3D EndScene/Present detour.
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class FindRender extends GhidraScript {
    long[] fwd = {
        0x10060410L,0x10060460L,0x100604b0L,0x10060500L,0x10060550L,0x100605a0L,
        0x100605f0L,0x10060640L,0x10060690L,0x100606e0L,0x10060730L,0x10060780L,
        0x100607d0L,0x10060820L,0x10060870L,0x10060020L
    };
    int[] slot = {17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};

    String d3dHint(Function f, FunctionManager fm, Listing l, ReferenceManager rm) {
        if (f == null) return "";
        StringBuilder s = new StringBuilder();
        InstructionIterator it = l.getInstructions(f.getBody(), true);
        int g = 0;
        while (it.hasNext() && g++ < 6000) {
            Instruction ins = it.next();
            // imported call names
            for (Reference r : ins.getReferencesFrom()) {
                Function cf = fm.getFunctionAt(r.getToAddress());
                if (cf != null) {
                    String n = cf.getName().toLowerCase();
                    if (n.contains("d3d")||n.contains("direct3d")||n.contains("present")
                       ||n.contains("endscene")||n.contains("beginscene")||n.contains("reset")
                       ||n.contains("createdevice")) {
                        if (s.indexOf(cf.getName())<0) s.append(cf.getName()).append(" ");
                    }
                }
            }
        }
        return s.toString();
    }

    @Override
    public void run() throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        Listing l = currentProgram.getListing();
        for (int i = 0; i < fwd.length; i++) {
            Address a = toAddr(fwd[i]);
            Function f = fm.getFunctionAt(a);
            println("\nslot " + slot[i] + "  forwarder " + a + "  " +
                    (f != null ? f.getSignature().getPrototypeString() : "(no func)"));
            // callers
            for (Reference r : rm.getReferencesTo(a)) {
                if (!r.getReferenceType().isCall()) continue;
                Function c = fm.getFunctionContaining(r.getFromAddress());
                String hint = d3dHint(c, fm, l, rm);
                println("   called from " + r.getFromAddress() + " in " +
                        (c != null ? c.getName()+"@"+c.getEntryPoint() : "?") +
                        (hint.isEmpty() ? "" : "   <<< D3D: " + hint));
            }
        }
        println("\n=== end ===");
    }
}
