// Print bytes at addr. args: ghidraAddrHex countDec. @category Windower
import ghidra.app.script.GhidraScript; import ghidra.program.model.mem.*;
public class Bytes extends GhidraScript {
  @Override public void run() throws Exception {
    String[] a=getScriptArgs(); long base=Long.parseLong(a[0],16); int n=Integer.parseInt(a[1]);
    Memory mem=currentProgram.getMemory(); StringBuilder sb=new StringBuilder();
    for(int i=0;i<n;i++){ int v=mem.getByte(toAddr(base+i))&0xFF; sb.append(String.format("%02x ",v));
      if((i&15)==15){ println(String.format("%08x: %s",base+i-15,sb.toString())); sb.setLength(0);} }
    if(sb.length()>0) println(sb.toString());
    println("===== end =====");
  }
}
