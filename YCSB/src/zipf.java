import java.io.File;
import java.io.FileNotFoundException;
import java.io.PrintWriter;
import java.util.Arrays;

import com.yahoo.ycsb.generator.*;

public class zipf {
	static int NUM_SERVERS = 7;
	static int NUM_CLIENTS = 51;
	public static void main(String args[]) throws FileNotFoundException {
		int Freq[] = new int[NUM_SERVERS];
		
		PrintWriter out[] = new PrintWriter[NUM_CLIENTS];
		for (int i = 0; i < NUM_CLIENTS; i++) {
			String filename = "/dev/zipf/data" + i + ".dat";
			out[i] = new PrintWriter(new File(filename));	
		}

		ScrambledZipfianGenerator gen = new ScrambledZipfianGenerator(0,
			Long.MAX_VALUE, .99);

		int N = 1 * 1024 * 1024;
		for (int cn = 0; cn < NUM_CLIENTS; cn++) {
			for (int i = 0; i < N; i++) {
				long num = gen.nextLong();
				long mask = 0xffL;
				if((num & mask) == 0) {		// Do not allow last byte = 0
					i --;
					continue;
				}
				Freq[(int)((num >> 40) % (NUM_SERVERS - 1)) + 1] ++;
				out[cn].println(num);
			}
			System.out.println("Done for client " + cn);
		}

		Arrays.sort(Freq);
		System.out.println(Arrays.toString(Freq));
		for (int cn = 0; cn < NUM_CLIENTS; cn++) {
			out[cn].close();
		}	
	}
}
