// Code to find some Zipf properties
import java.io.File;
import java.io.FileNotFoundException;
import java.io.PrintWriter;
import java.util.Arrays;
import java.util.Collections;

import com.yahoo.ycsb.generator.*;

public class zipf_properties {
	public static void main(String args[]) throws FileNotFoundException {
		int count = Integer.parseInt(args[0]);	// Number of samples

		int buckets = 64;
		int Freq[] = new int[buckets + 1];

		// Initialize a generator for numbers between 0 and @buckets with
		// skeness .99
		ScrambledZipfianGenerator gen = new ScrambledZipfianGenerator(0,
			Long.MAX_VALUE, .99);

		for (int i = 0; i < count; i++) {
			long num = gen.nextLong();
			Freq[(int) (num % buckets)]++;
		}

		// Sort the array - this is dumb..
		Arrays.sort(Freq);
		for (int i = 0; i < Freq.length / 2; i++) {
			int temp = Freq[i];
			Freq[i] = Freq[Freq.length - 1 - i];
			Freq[Freq.length - 1 - i] = temp;
		}

		// Print percentiles
		for (int percentile = 5; percentile <= 100; percentile += 5) {
			int tot = 0;
			int i;
			for(i = 0; i < buckets; i++) {
				tot += Freq[i];
				if(tot >= ((double) percentile * count) / 100) {
					i++;
					break;
				}
			}

			System.out.println(i + " buckets of " + buckets + " comprise " +
				percentile + " percent of samples");
		}
	}
}
