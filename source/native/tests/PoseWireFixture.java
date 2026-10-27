import com.bioshockport.PoseWire;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;

public class PoseWireFixture {
    public static void main(String[] args) throws Exception {
        float[] axes = new float[53];
        axes[6] = axes[18] = axes[43] = axes[47] = 1;
        axes[7] = .34567892f;
        axes[20] = -.8765432f;
        axes[9] = -.45678912f;
        axes[21] = .56789123f;
        axes[27] = .12345679f; axes[28] = -.23456791f; axes[29] = .03456789f;
        axes[30] = (float)Math.sqrt(1-axes[27]*axes[27]-axes[28]*axes[28]-axes[29]*axes[29]);
        axes[31] = .012345678f; axes[32] = 1.654321f; axes[33] = -.32109876f;
        axes[34] = .06345679f; axes[35] = axes[36] = 100;
        axes[37] = 156; axes[39] = 1.654321f;
        boolean[] buttons = new boolean[19]; buttons[0] = buttons[9] = buttons[18] = true;
        ByteBuffer packet = ByteBuffer.allocate(PoseWire.BYTES);
        if (!PoseWire.encode(packet, axes, buttons)) throw new AssertionError("encode");
        Files.write(Path.of(args[0]), packet.array());
        ByteBuffer exact=ByteBuffer.allocate(PoseWire.EXACT_BYTES);
        if(!PoseWire.encodeExact(exact,axes,buttons,0x123456780000abcdL))throw new AssertionError("exact encode");
        Files.write(Path.of(args[0]+".v2"),exact.array());
        if (PoseWire.encode(packet, new float[47], buttons)) throw new AssertionError("short axes accepted");
        System.out.println("Java: full precision pose fixture, 160 bytes");
    }
}
