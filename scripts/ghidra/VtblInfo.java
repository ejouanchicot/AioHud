// Dump a C++ vtable: the RTTI class name (from the Complete Object Locator at
// vtbl-4) and each virtual method (address + signature + a hint of D3D/font/text
// imports it touches). Used to identify the Windower host interfaces a plugin
// receives at Init. Arg: hex vtable address (Ghidra, Hook.dll base 0x10000000).
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.*;

public class VtblInfo extends GhidraScript {
    @Override
    public void run() throws Exception {
        String arg = getScriptArgs().length > 0 ? getScriptArgs()[0] : "10E42A0C";
        long base = Long.parseLong(arg, 16);
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing l = currentProgram.getListing();
        SymbolTable st = currentProgram.getSymbolTable();
        long textMin = 0x10001000L, textMax = 0x10120000L;

        // RTTI class name via Complete Object Locator at vtbl-4
        try {
            long col = ((long) mem.getInt(toAddr(base - 4))) & 0xffffffffL;
            // COL+0xC -> TypeDescriptor ; TD+8 -> mangled name string
            long td = ((long) mem.getInt(toAddr(col + 0x0c))) & 0xffffffffL;
            StringBuilder nm = new StringBuilder();
            long p = td + 8;
            for (int k = 0; k < 80; k++) { int c = mem.getByte(toAddr(p + k)) & 0xff; if (c == 0) break; nm.append((char) c); }
            println("RTTI: " + nm);
        } catch (Exception e) { println("RTTI: (n/a) " + e.getMessage()); }

        println("=== vtable @ 0x" + Long.toHexString(base) + " ===");
        for (int i = 0; i < 40; i++) {
            Address slot = toAddr(base + (long) i * 4);
            long fa;
            try { fa = ((long) mem.getInt(slot)) & 0xffffffffL; } catch (Exception e) { break; }
            if (fa < textMin || fa >= textMax) { println("[" + i + "] 0x" + Long.toHexString(fa) + " -- end (" + i + " methods)"); break; }
            Function f = fm.getFunctionAt(toAddr(fa));
            if (f == null) f = createFunction(toAddr(fa), null);
            String sig = (f != null) ? f.getSignature().getPrototypeString() : "?";
            int purge = (f != null) ? f.getStackPurgeSize() : -1;
            // hint: imports / strings this method touches
            StringBuilder hint = new StringBuilder();
            if (f != null) {
                InstructionIterator it = l.getInstructions(f.getBody(), true);
                int g = 0;
                while (it.hasNext() && g++ < 1500) {
                    Instruction ins = it.next();
                    for (Reference r : ins.getReferencesFrom()) {
                        Function cf = fm.getFunctionAt(r.getToAddress());
                        if (cf != null) {
                            String nn = cf.getName();
                            String low = nn.toLowerCase();
                            if (low.contains("d3d")||low.contains("direct3d")||low.contains("font")||
                                low.contains("text")||low.contains("draw")||low.contains("present")||
                                low.contains("sprite")||low.contains("device"))
                                if (hint.indexOf(nn) < 0) hint.append(nn).append(" ");
                        }
                    }
                }
            }
            println(String.format("[%2d] 0x%08x purge=%d  %s%s", i, fa, purge, sig,
                    hint.length() > 0 ? "   <<< " + hint : ""));
        }
        println("=== end ===");
    }
}
