#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
// for SIZE_MAX
#include <stdint.h>

// #define DEBUG 1

/*
gcc -o sparse sparse.c
./sparse -o512i$((1024*1024))p sparse sparse.o ; diff sparse sparse.o ; echo $? ;
./sparse -o512i64pl 512,768,4096 sparse sparse.1 sparse.2 sparse.3 sparse.4 sparse.5 sparse.6 sparse.7 sparse.8 sparse.9 ; cat sparse.[1-9] | diff sparse - ; echo $?
./sparse -o512i65pl 512,768,4096 sparse sparse.1 sparse.2 sparse.3 sparse.4 sparse.5 sparse.6 sparse.7 sparse.8 sparse.9 ; cat sparse.[1-9] | diff sparse - ; echo $?
./sparse -o513i64pl 512,768,4096 sparse sparse.1 sparse.2 sparse.3 sparse.4 sparse.5 sparse.6 sparse.7 sparse.8 sparse.9 ; cat sparse.[1-9] | diff sparse - ; echo $?
ls -l
rm sparse.o sparse.[1-9]
*/

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
my_seekOrDie(FILE *f, size_t p, int w) {
	int pos;
	if ( pos = fseek(f, p, w) ) {
		pos = errno;
		fprintf(stderr, "Could not seek to %ld: %s\n",
			p, strerror(errno));
		exit(pos);
	} else {
		return pos;
	}
}

int
main(int argc, char **argv) {
	char *arg, *arg_sizeList = 0;
	short int md_copy = 0, md_skipPad = 0;
	size_t sz_input = 0, sz_output = 0, ii_count_i = 0, ii_count_o = 0,
		ii_seek_i = 0, ii_seek_o = 0, ii_seek_max = SIZE_MAX,
		ii_buf_pos, ii_tmp, ii_write_len;
	FILE *f_in, *f_out, *f_err;
	void *buf;

	f_in = stdin;
	f_out = stdout;
	f_err = stderr;

	// If 'split' -l is assumed.
	if (strncmp(&((*argv)[(strlen(*argv) - 5)]),"split", 5) == 0) arg_sizeList = (char *) -1;
#ifdef DEBUG
	fprintf(stderr, "Arg: %s (%s)\n", *argv, &((*argv)[(strlen(*argv) - 5)]));
#endif

	for (argv++; argv[0][0] == '-'; argv++) {
		arg = argv[0];
		for (arg++; arg[0] != '\0'; arg++) {
			switch (arg[0]) {
			case 't': // truncate trailing zeros (don't write last sparse pad)
				md_skipPad = 1;
				break;
			case 'i': // Max input buffer (default 512 if set)
				sz_input = 512;
				if (arg[1] >= '0' && arg[1] <= '9') {
					sz_input = (size_t) strtoll(&(arg[1]), &arg, 0);
					arg--;
				}
				break;
			case 'o': // Max output buffer (1==special default to block)
				sz_output = 1;
				if (arg[1] >= '0' && arg[1] <= '9') {
					sz_output = (size_t) strtoll(&(arg[1]), &arg, 0);
					arg--;
				}
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
	if (sz_output == 1) sz_output = sz_input;

	if (arg_sizeList == (char *) -1) {
		arg_sizeList = *argv;
		argv++;
	}

	// Arg processing done, open the files
#ifdef DEBUG
	fprintf(f_err, "Options: b=%lx z=%lx copy=%d list=%s\n", sz_input, sz_output, md_copy, arg_sizeList);
#endif // DEBUG

	if (md_copy == 1) {
		f_in = my_fopen(*argv, "r");
		argv++;
	} else {
		f_in = stdin;
	}

	if (sz_input) {
		buf = malloc(sz_input);
		if (buf == NULL) {
			fprintf(f_err, "Could not alloc buffer size %lx\n", sz_input);
			exit(errno);
		}
	} else {
		// Rather than creating a special case, I'll abuse an alloced but no longer used variable as a char.
		sz_input = 1;
		buf = &arg;
	}

#ifdef DEBUG
	fprintf(f_err, "Opening output: %s\n", *argv);
#endif
	f_out = my_fopen(*argv, "w");
	argv++;
	if (arg_sizeList != NULL && *arg_sizeList != '\0') // AKA != 0
		ii_seek_max = (size_t) strtoll(arg_sizeList, &arg_sizeList, 0);
	if (arg_sizeList != NULL && *arg_sizeList != '\0') // AKA != 0
		arg_sizeList++;
	
	while (!feof(f_in)) {
		// Step 1/2: Read Buffer
		ii_count_i = fread(buf, sz_input, 1, f_in) * sz_input;
		if (ii_count_i == 0) // Didn't read all records, figure out the byte size the hard way.
			ii_count_i = ftell(f_in) - ii_seek_i;
		ii_seek_i += ii_count_i;

		// Step 2/2: Loop writing Output buffer units (skipping if all 0s)

		for (ii_buf_pos = 0 ; ii_buf_pos < ii_count_i; ii_buf_pos += ii_count_o) {
			md_copy=0;	// Reuse md_copy as a state variable
			for (ii_count_o = 0; ii_count_o < sz_output; ii_count_o++) {
				if ( ((char *)buf)[ii_count_o] != '\0' ) {
					md_copy=1;
					break;
				}
			}
			ii_write_len = ii_count_i - ii_buf_pos;
			if (ii_write_len > sz_output) ii_write_len = sz_output;
			if (!md_copy) {
				if (ii_seek_o + ii_write_len < ii_seek_max) {
					ii_seek_o += ii_count_i;
				} else {
					my_seekOrDie(f_out, ii_seek_max - 1, SEEK_SET);
					ii_count_o = fwrite(buf, 1, 1, f_out);
					if (!ii_count_o)
					  fprintf(stderr, "Write error for last 0-byte section of %s.", *(argv - 1));
					ii_seek_o = ii_seek_o + ii_write_len - ii_seek_max;
					fclose(f_out);
#ifdef DEBUG
	fprintf(f_err, "Opening output: %s\n", *argv);
#endif
					f_out = my_fopen(*argv, "w");
					argv++;
					if (arg_sizeList != NULL && *arg_sizeList != '\0') // AKA != 0
						ii_seek_max = (size_t) strtoll(arg_sizeList, &arg_sizeList, 0);
					if (arg_sizeList != NULL && *arg_sizeList != '\0') // AKA != 0
						arg_sizeList++;
				}
				ii_count_o = ii_count_i;
			}
			while (md_copy) {
				if (ii_seek_o + ii_write_len <= ii_seek_max) {
					if ( ftell(f_out) != ii_seek_o )
					  my_seekOrDie(f_out, ii_seek_o, SEEK_SET);
					if ( ! ii_write_len % sz_input ) {
						ii_count_o = fwrite(&(((char *)buf)[ii_buf_pos]), ii_write_len, 1, f_out) * sz_input;
					} else {
						ii_count_o = fwrite(&(((char *)buf)[ii_buf_pos]), 1, ii_write_len, f_out);
					}
					ii_seek_o += ii_count_o;
				} else {
					// Finish writing current file, shift the buffer allow the next loop to catch up.
					ii_count_o = fwrite(&(((char *)buf)[ii_buf_pos]), 1, ii_seek_max - ii_seek_o, f_out);
					ii_seek_o += ii_count_o;
				}
				if (ferror(f_out)) {
					// investigate and report error
					if (!feof(f_out)) perror(*argv);
					fprintf(stderr, "Could not write at in %ld out %ld: errno: %d = %s\n", ii_seek_i, ii_seek_o, errno, strerror(errno));
					// exit(1);
				}
				if (ii_seek_o == ii_seek_max) {
					fclose(f_out);
#ifdef DEBUG
	fprintf(f_err, "Opening output: %s\n", *argv);
#endif
					f_out = my_fopen(*argv, "w");
					argv++;
					if (arg_sizeList != NULL && *arg_sizeList != '\0') // AKA != 0
						ii_seek_max = (size_t) strtoll(arg_sizeList, &arg_sizeList, 0);
					if (arg_sizeList != NULL && *arg_sizeList != '\0') // AKA != 0
						arg_sizeList++;
					ii_seek_o = 0;
					md_copy = 0;
				} else if (ii_count_o == ii_write_len) md_copy = 0;
			}
		}
#ifdef DEBUG
		if ( arg_sizeList == NULL && ii_seek_o != ii_seek_i) {
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
