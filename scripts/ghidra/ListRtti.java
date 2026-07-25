// List all MSVC RTTI class names (".?AV...@@" / ".?AU...@@") found in Hook.dll,
// to enumerate Windower's interface classes and spot the graphics/text/font/
// primitive one used for on-screen drawing.
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.*;
import java.util.*;

public class ListRtti extends GhidraScript {
    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        TreeSet<String> names = new TreeSet<>();
        for (String pat : new String[]{".?AV", ".?AU"}) {
            Address start = currentProgram.getMinAddress();
            while (start != null) {
                Address hit = findBytes(start, java.util.regex.Pattern.quote(pat));
                if (hit == null) break;
                StringBuilder sb = new StringBuilder();
                for (int k = 0; k < 120; k++) {
                    int c;
                    try { c = mem.getByte(hit.add(k)) & 0xff; } catch (Exception e) { break; }
                    if (c == 0) break;
                    sb.append((char) c);
                }
                String s = sb.toString();
                if (s.endsWith("@@")) names.add(s);
                start = hit.add(1);
            }
        }
        println("=== " + names.size() + " RTTI classes in Hook.dll ===");
        for (String n : names) println(n);
        println("=== end ===");
    }
}
