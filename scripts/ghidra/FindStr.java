// Find defined strings containing a substring (arg0, case-insensitive) and list
// the functions that reference them. Used to locate the console-command dispatcher
// by its error/help strings. Optionally decompile matches with arg1 = "d".
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class FindStr extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String sub = (a.length > 0 ? a[0] : "command").toLowerCase();
        boolean decomp = (a.length > 1 && a[1].equalsIgnoreCase("d"));

        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        DecompInterface di = new DecompInterface();
        if (decomp) di.openProgram(currentProgram);

        java.util.HashSet<String> seenFns = new java.util.HashSet<>();
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            Object v = d.getValue();
            if (!(v instanceof String)) continue;
            String s = (String) v;
            if (!s.toLowerCase().contains(sub)) continue;
            println("STR @" + d.getAddress() + " : " + s.replace("\n", "\\n"));
            ReferenceIterator ri = rm.getReferencesTo(d.getAddress());
            while (ri.hasNext()) {
                Reference r = ri.next();
                Function f = fm.getFunctionContaining(r.getFromAddress());
                String fn = (f != null) ? (f.getName() + " @" + f.getEntryPoint()) : ("? @" + r.getFromAddress());
                println("    <- " + fn);
                if (decomp && f != null && seenFns.add(f.getName())) {
                    DecompileResults res = di.decompileFunction(f, 60, monitor);
                    if (res != null && res.getDecompiledFunction() != null)
                        for (String ln : res.getDecompiledFunction().getC().split("\n")) println("        " + ln);
                }
            }
        }
        if (decomp) di.dispose();
        println("===== end =====");
    }
}
