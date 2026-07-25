// Find symbols (functions) whose name and/or parent namespace contains the given
// substrings, and decompile the first match. Used to locate MMFTextHandler::Update
// (the actual text renderer). Args: <nameSubstr> [namespaceSubstr]
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class FindSym extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String nameSub = a.length > 0 ? a[0] : "Update";
        String nsSub = a.length > 1 ? a[1] : "Text";
        SymbolTable st = currentProgram.getSymbolTable();
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        Function first = null;
        int count = 0;
        for (Symbol s : st.getAllSymbols(false)) {
            if (!s.getName().contains(nameSub)) continue;
            Namespace ns = s.getParentNamespace();
            String nsn = ns != null ? ns.getName() : "";
            if (!nsn.contains(nsSub)) continue;
            Function f = fm.getFunctionAt(s.getAddress());
            println("MATCH: " + nsn + "::" + s.getName() + " @ " + s.getAddress() + (f != null ? "  [func]" : ""));
            if (f != null && first == null) first = f;
            if (++count > 40) break;
        }
        if (first != null) {
            println("\n===== decompile " + first.getName() + " @ " + first.getEntryPoint() + " =====");
            DecompileResults res = di.decompileFunction(first, 90, monitor);
            if (res != null && res.getDecompiledFunction() != null)
                for (String ln : res.getDecompiledFunction().getC().split("\n")) println(ln);
        }
        di.dispose();
        println("\n=== end ===");
    }
}
