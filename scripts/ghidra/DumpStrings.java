// Dump all defined strings with address + first xref function.
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class DumpStrings extends GhidraScript {
    @Override
    public void run() throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            Object v = d.getValue();
            if (!(v instanceof String)) continue;
            String s = ((String) v).replace("\n","\n");
            if (s.length() > 120) s = s.substring(0,120);
            StringBuilder xr = new StringBuilder();
            ReferenceIterator ri = rm.getReferencesTo(d.getAddress());
            int n=0;
            while (ri.hasNext() && n<3) { Reference r = ri.next(); Function f = fm.getFunctionContaining(r.getFromAddress());
                if (f!=null){ xr.append(f.getEntryPoint()).append(" "); n++; } }
            println("S "+d.getAddress()+" ["+xr.toString().trim()+"] "+s);
        }
        println("=== end ===");
    }
}
