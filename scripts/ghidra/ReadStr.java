// Print the null-terminated ASCII string at each hex address arg.
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
public class ReadStr extends GhidraScript {
    @Override public void run() throws Exception {
        for (String a: getScriptArgs()) { if(a==null||a.isEmpty())continue;
            Address ad = toAddr(Long.parseLong(a.trim(),16));
            StringBuilder sb = new StringBuilder();
            for (int i=0;i<64;i++){ byte b=getByte(ad.add(i)); if(b==0)break; sb.append((char)b); }
            println(a.trim()+" : \""+sb+"\"");
        }
        println("=== end ===");
    }
}
