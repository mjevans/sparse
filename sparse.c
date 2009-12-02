#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
// for SIZE_MAX
#include <stdint.h>

#define DEBUG 1

FILE*
my_fopen(const char *n, const char *m) {
	FILE *f;
	int err;
	f = fopen(n, m);
	if (f == NULL) {
		err = errno;
		perror(n);
		exit(err);
	}
	return f;
}

int
main(int argc, char **argv) {
	char *arg, *arg_sizeList = 0;
	short int md_copy = 0, md_block = 0, md_skipPad = 0;
	size_t md_zeroSize = 0, ii_count_i = 0, ii_count_o = 0, ii_seek_i = 0, ii_seek_o = 0, ii_seek_max = SIZE_MAX;
	FILE *f_in, *f_out, *f_err;
	void *buf;

	f_in = stdin;
	f_out = stdout;
	f_err = stderr;

	// If 'split' -l is assumed.
	if ( strncmp(&(argv[(strlen(*argv) - 6)]), "split", 5) ) arg_sizeList = (char *) -1;

	for (argv++; argv[0][0] == '-'; argv++) {
		arg = argv[0];
#ifdef DEBUG
		fprintf(stderr, "Arg: %s (%s)\n", arg, &(argv[(strlen(*argv) - 6)]));
#endif
		for (arg++; arg[0] != '\0'; arg++) {
			switch (arg[0]) {
			case 't': // don't seek in the file to create any pads.
				md_skipPad = 1;
				break;
			case 'b': // Block on boundry (default 512 if set)
				md_block = 512;
				if (arg[1] >= '0' && arg[1] <= '9')
					md_block = (int) strtol(&(arg[1]), &arg, 0);
				break;
			case 'z': // Zero size (1==special default to block)
				md_zeroSize = 1;
				if (arg[1] >= '0' && arg[1] <= '9')
					md_zeroSize = (size_t) strtoll(&(arg[1]), &arg, 0);
				break;
			case 'p': // coPy
				md_copy = 1;
				break;
			case 'l': // list (of sizes for each file argument in order)
				arg_sizeList = (char *) -1; // Use first 'file' slot for this argument.
				break;
			case '-':
				if (arg[1] == '\0') goto end_args; // break 2
				// Long Opts
				while (arg[1] != '\0') arg++; // end condition
				break;
			default:
				fprintf(f_err, "Unknown option %c\n", arg[0]);
				break;
			}
			if (arg[0] == '\0') break;
		}
	}
end_args:

	if (arg_sizeList == (char *) -1) {
		arg_sizeList = *argv;
		argv++;
	}

	// Arg processing done, open the files
#ifdef DEBUG
	fprintf(f_err, "Options: b=%x z=%lx copy=%d list=%s\n", md_block, md_zeroSize, md_copy, arg_sizeList);
#endif // DEBUG

	if (md_copy == 1) {
		f_in = my_fopen(*argv, "r");
		argv++;
	} else {
		f_in = stdin;
	}

	if (md_block) {
		buf = malloc(md_block);
		if (buf == NULL) {
			fprintf(f_err, "Could not alloc buffer size %x\n", md_block);
			exit(errno);
		}
	} else {
		// Rather than creating a special case, I'll abuse an alloced but no longer used variable as a char.
		md_block = 1;
		buf = &arg;
	}

	f_out = my_fopen(*argv, "w");
	argv++;
	if (arg_sizeList != NULL) // AKA != 0
		ii_output_max = (size_t) strtoll(&(arg_sizeList), &arg_sizeList, 0);
	if (arg_sizeList != NULL) // AKA != 0
		arg_sizeList++;
	
	while (!feof(f_in)) {
		ii_count_i = fread(buf, md_block, 1, f_in) * md_block;
		if (ii_count_i == 0) {
			// If reading a full block fails, read up to a full block of bytes.
			ii_count_i = fread(buf, 1, md_block, f_in);
		}
		ii_seek_i += ii_count_i;
		md_copy=0;	// Reuse md_copy as a state variable
		for (ii_count_o = 0; ii_count_o < ii_count_i; ii_count_o++) {
			if ( ((char *)buf)[ii_count_o] != '\0' ) {
				md_copy=1;
				break;
			}
		}
		if (!md_copy) ii_seek_o += ii_count_i;
		while (md_copy) {
			if (ii_seek_o + ii_count_i <= ii_seek_max) {
				if ( ftell(f_out) != ii_seek_o && ( ii_count_o = fseek(f_out, ii_seek_o, SEEK_SET) ) ) {
					ii_seek_i = errno;
					fprintf(stderr, "Could not seek to %ld: %s: %ld\n", ii_seek_o, strerror(errno), ii_count_o);
					exit(ii_seek_i);
				}
				if ( ! ii_count_i % md_block ) {
					ii_count_o = fwrite(buf, md_block, 1, f_out) * md_block;
				} else {
					ii_count_o = fwrite(buf, 1, md_block, f_out);
				}
				ii_seek_o += ii_count_o;
			} else {
				// Finish writing current file, shift the buffer allow the next loop to catch up.
				ii_count_o = fwrite(buf, 1, ii_seek_max - ii_seek_o, f_out);
				ii_seek_o += ii_count_o;
			}
			if (ferror(f_out)) {
				// investigate and report error
				if (!feof(f_out)) perror(*argv);
				fprintf(stderr, "Could not write at in %ld out %ld: errno: %d = %s\n", ii_seek_i, ii_seek_o, errno, strerror(errno));
				// exit(1);
			}
			if (ii_seek_o == ii_seek_max) {
				ii_count_i -= ii_count_o;
				memmove(buf, (const void *) &( ((char *)buf) [ii_count_o]), ii_count_i);
				fclose(f_out);
				f_out = my_fopen(*argv, "w");
				argv++;
				if (arg_sizeList != NULL) // AKA != 0
					ii_output_max = (size_t) strtoll(&(arg_sizeList), &arg_sizeList, 0);
				if (arg_sizeList != NULL) // AKA != 0
					arg_sizeList++;
				ii_seek_o = 0;
				ii_count_o = 0;
				md_copy = ii_count_i > 0;
			}
			if (ii_count_o = ii_count_i) md_copy = 0;
		}
#ifdef DEBUG
		if (ii_seek_o != ii_seek_i) {
			fprintf(stderr, "Desynced %ld vs %ld\n", ii_seek_i, ii_seek_o);
			exit(1);
		}
#endif
	}
	if (!md_skipPad && ftell(f_out) < ii_seek_o) {
		if ( ii_count_o = fseek(f_out, ii_seek_o - 1, SEEK_SET) ) {
			ii_seek_i = errno;
			fprintf(stderr, "Could not seek to %ld: %s: %ld\n", ii_seek_o, strerror(errno), ii_count_o);
			exit(ii_seek_i);
		}
		((char *)buf)[0] = '\0';
		ii_count_o = fwrite(buf, 1, 1, f_out); // Write '\0' as the very last byte.
		if (!ii_count_o) fprintf(f_err, "Warning: Couldn't write final zero byte to pad the end of file.\n");
	}
#ifdef DEBUG
	fprintf(stderr, "Final %ld\t: %ld vs\n      %ld\t: %ld\n", ii_seek_i, ftell(f_in), ii_seek_o, ftell(f_out));
#endif
	fclose(f_out);
	fclose(f_in);
//	fclose(f_err);
}
