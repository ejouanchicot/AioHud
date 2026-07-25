// List the functions that reference/call a given address (arg0 hex). Used to walk
// up from a known handler (e.g. the //unload handler) to the console dispatcher.
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class FindCallers extends GhidraScript {
    @Override
    public void run() throws Exception {
        long addr = Long.parseLong(getScriptArgs()[0], 16);
        Address target = toAddr(addr);
        ReferenceManager rm = currentProgram.getReferenceManager();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceIterator it = rm.getReferencesTo(target);
        java.util.HashSet<String> seen = new java.util.HashSet<>();
        while (it.hasNext()) {
            Reference r = it.next();
            Function f = fm.getFunctionContaining(r.getFromAddress());
            String fn = (f != null) ? (f.getName() + " @" + f.getEntryPoint()) : ("? @" + r.getFromAddress());
            if (seen.add(fn)) println("CALLER: " + fn + "  (" + r.getReferenceType() + " from " + r.getFromAddress() + ")");
        }
        println("===== end =====");
    }
}
