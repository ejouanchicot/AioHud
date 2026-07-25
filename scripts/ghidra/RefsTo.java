// List all references to a given data address (arg0 hex) with the containing function.
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class RefsTo extends GhidraScript {
    @Override
    public void run() throws Exception {
        long tgt = Long.parseLong(getScriptArgs()[0], 16);
        Address a = toAddr(tgt);
        ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(a);
        FunctionManager fm = currentProgram.getFunctionManager();
        int n = 0;
        while (it.hasNext()) {
            Reference r = it.next();
            Address from = r.getFromAddress();
            Function f = fm.getFunctionContaining(from);
            Instruction ins = currentProgram.getListing().getInstructionAt(from);
            println(String.format("%s  %-14s  %-40s  fn=%s",
                from, r.getReferenceType(),
                ins != null ? ins.toString() : "?",
                f != null ? (f.getName()+"@"+f.getEntryPoint()) : "(none)"));
            n++;
        }
        println("===== end ("+n+") =====");
    }
}
