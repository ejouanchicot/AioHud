// Dump the vtable of a Windower class by name: finds the "<Class>::vftable"
// symbol, then lists each virtual method (addr, purge=stackargs, signature,
// and string/import hints) to reverse the TextHandler / TextObject draw API.
// Args: one or more class names, e.g.  TextHandler TextObject
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.*;
import java.util.*;

public class DumpClass extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] classes = getScriptArgs();
        if (classes.length == 0) classes = new String[]{"TextHandler", "TextObject"};
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing l = currentProgram.getListing();
        SymbolTable st = currentProgram.getSymbolTable();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        long textMin = 0x10001000L, textMax = 0x10120000L;

        for (String cls : classes) {
            // vftable is a symbol named "vftable" whose parent namespace is the class
            Address vt = null;
            for (Symbol s : st.getSymbols("vftable")) {
                Namespace ns = s.getParentNamespace();
                if (ns != null && ns.getName().equals(cls)) { vt = s.getAddress(); break; }
            }
            if (vt == null) { // also try a label named exactly Class::vftable
                for (Symbol s : st.getAllSymbols(false))
                    if (s.getName(true).equals(cls + "::vftable")) { vt = s.getAddress(); break; }
            }
            println("\n##### " + cls + "  vftable=" + vt + " #####");
            if (vt == null) { println("  (symbol not found)"); continue; }
            long base = vt.getOffset();
            for (int i = 0; i < 30; i++) {
                long fa;
                try { fa = ((long) mem.getInt(toAddr(base + i * 4L))) & 0xffffffffL; } catch (Exception e) { break; }
                if (fa < textMin || fa >= textMax) { println("  [" + i + "] end (" + i + " methods)"); break; }
                Function f = fm.getFunctionAt(toAddr(fa));
                if (f == null) f = createFunction(toAddr(fa), null);
                int purge = (f != null) ? f.getStackPurgeSize() : -1;
                String sig = (f != null) ? f.getSignature().getPrototypeString() : "?";
                // string/import hint
                StringBuilder hint = new StringBuilder();
                if (f != null) {
                    DecompileResults dr = di.decompileFunction(f, 25, monitor);
                    // (decompile kept cheap; just note callees of interest)
                    InstructionIterator ii = l.getInstructions(f.getBody(), true);
                    int g = 0;
                    while (ii.hasNext() && g++ < 1200) {
                        Instruction ins = ii.next();
                        for (Reference r : ins.getReferencesFrom()) {
                            Function cf = fm.getFunctionAt(r.getToAddress());
                            if (cf != null) {
                                String nn = cf.getName().toLowerCase();
                                if (nn.contains("draw")||nn.contains("text")||nn.contains("font")||
                                    nn.contains("d3d")||nn.contains("sprite")||nn.contains("rect")||
                                    nn.contains("color")||nn.contains("create")||nn.contains("add"))
                                    if (hint.indexOf(cf.getName())<0) hint.append(cf.getName()).append(" ");
                            }
                        }
                    }
                }
                println(String.format("  [%2d] 0x%08x purge=%d args=%d  %s%s",
                        i, fa, purge, purge>=0?purge/4:-1, sig, hint.length()>0?"  << "+hint:""));
            }
        }
        di.dispose();
        println("\n=== end ===");
    }
}
