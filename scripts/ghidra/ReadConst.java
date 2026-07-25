// Print float+int32 value at each hex address arg.
// @category Windower
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
public class ReadConst extends GhidraScript {
    @Override public void run() throws Exception {
        for (String a: getScriptArgs()) { if(a==null||a.isEmpty())continue;
            Address ad = toAddr(Long.parseLong(a.trim(),16));
            int i = getInt(ad);
            println(a.trim()+" : int="+i+" (0x"+Integer.toHexString(i)+")  float="+Float.intBitsToFloat(i));
        }
        println("=== end ===");
    }
}
