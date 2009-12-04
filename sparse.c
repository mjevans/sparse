#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
// for SIZE_MAX
#include <stdint.h>

// #define DEBUG 1

/*

// Unit Tests

gcc -o sparse sparse.c && { \
echo Testing against a file without holes; \
./sparse -o512i$((1024*1024))p sparse sparse.o ; diff sparse sparse.o ; echo $? one-to-one; \
./sparse -o512i64pl 512,768,4096 sparse sparse.1 sparse.2 sparse.3 > sparse.4 ; cat sparse.[1-9] | diff sparse - ; echo $? stdout ; \
./sparse -o512i64pl 512,768,4096 sparse sparse.1 sparse.2 sparse.3 sparse.4 sparse.5 sparse.6 sparse.7 sparse.8 sparse.9 ; cat sparse.[1-9] | diff sparse - ; echo $? itr1; \
./sparse -o512i65pl 512,768,4096 sparse sparse.1 sparse.2 sparse.3 sparse.4 sparse.5 sparse.6 sparse.7 sparse.8 sparse.9 ; cat sparse.[1-9] | diff sparse - ; echo $? itr2; \
./sparse -o513i64pl 512,768,4096 sparse sparse.1 sparse.2 sparse.3 sparse.4 sparse.5 sparse.6 sparse.7 sparse.8 sparse.9 ; cat sparse.[1-9] | diff sparse - ; echo $? itr3; \
ls -sl ; rm sparse.o sparse.[1-9] ; \
echo Generating a temp file with holes using truncate; \
dd if=/dev/urandom of=sparse.test bs=1b count=7 seek=4 ; \
dd if=/dev/urandom of=sparse.test bs=1b count=3 seek=400 ; \
dd if=/dev/urandom of=sparse.test bs=1b count=18 seek=8000 ; \
dd if=/dev/urandom of=sparse.test bs=1b count=1 seek=16380 ; \
truncate -s8M sparse.test ; \
echo Running on the temp-file. ; \
./sparse -o512i$((1024*1024))p sparse.test sparse.o ; diff sparse.test sparse.o ; echo $? one-to-one; \
./sparse -o512i64pl 512,768,$((1024*1024*2)) sparse.test sparse.1 sparse.2 sparse.3 sparse.4 sparse.5 sparse.6 sparse.7 sparse.8 sparse.9 ; cat sparse.[1-9] | diff sparse.test - ; echo $? itr1; \
./sparse -o512i65pl 512,768,$((1024*1024*2)) sparse.test sparse.1 sparse.2 sparse.3 sparse.4 sparse.5 sparse.6 sparse.7 sparse.8 sparse.9 ; cat sparse.[1-9] | diff sparse.test - ; echo $? itr2; \
./sparse -o513i64pl 512,768,$((1024*1024*2)) sparse.test sparse.1 sparse.2 sparse.3 sparse.4 sparse.5 sparse.6 sparse.7 sparse.8 sparse.9 ; cat sparse.[1-9] | diff sparse.test - ; echo $? itr3; }
ls -sl
rm sparse.o sparse.[1-9] sparse.test

// Tuning Tests

for blkin in 512 4096 $((1024*256)) $((1024*512)) $((1024*1024)) $((1024*1024*4)) ; do \
  for blkout in 512 4096 $((1024*256)) $((1024*512)) $((1024*1024)) $((1024*1024*4)) ; do \
    echo "-i${blkin}-o${blkout}" | tee -a log;\
    md5sum -b src | sed 's/src/dst/' > dst.md5 ;\
    time ./sparse -i${blkin}o${blkout}p src dst 2>&1 | tee -a log ;\
    md5sum -c dst.md5  2>&1 | tee -a log ; rm dst;\
  done ;\
done ; rm dst.md5

General observations:
Operating at filesystem block sizes for input and output is fastest (on my system).
Operating at hardware block sizes for output is only -slightly- slower.
Given the need to scan over each chunk operating at or beneath a single page of memory seems to work better on my system.
Given the two choices, 4096:4096 and 4096:512, the first seems a better default.
It's slightly faster and when operating on actual block devices, instead of files, would better work for -most- filesystem images.
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

void
my_nextListFile(FILE **f_out, char ***argv, char **sList, size_t *smax) {
	if (*f_out) fclose(*f_out);
#ifdef DEBUG
	fprintf(stderr, "Opening output: %s", **argv);
#endif
	if (**argv == NULL) {
		*smax = SIZE_MAX;
		*f_out = stdout;
	} else {
	*f_out = my_fopen(**argv, "w");
		if (*f_out == NULL) {
			*smax = SIZE_MAX;
			*f_out = stdout;
		} else {
			if (*sList != NULL && **sList != '\0')
				*smax = strtoll(*sList, sList, 0);
			if (*sList != NULL && **sList != '\0')
				(*sList)++;
#ifdef DEBUG
			fprintf(stderr, " with max %lu", *smax);
#endif
			(*argv)++;
		}
	}
#ifdef DEBUG
	fprintf(stderr, "\n");
#endif
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
	size_t sz_input = 4096, sz_output = 4096, ii_count_i = 0, ii_count_o = 0,
		ii_seek_i = 0, ii_seek_o = 0, ii_seek_max = SIZE_MAX,
		ii_buf_pos, ii_tmp, ii_write_len;
	FILE *f_in, *f_out, *f_err;
	void *buf;

	f_in = stdin;
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
				sz_input = 4096;
				if (arg[1] >= '0' && arg[1] <= '9') {
					sz_input = (size_t) strtoll(&(arg[1]), &arg, 0);
					arg--;
				}
				break;
			case 'o': // Max output buffer (1==special default to block)
				sz_output = 4096;
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
			case 'h': // help
				fprintf(f_err, "Usage: %s [OPTION]... [FILE]...\n"
					"Sparsely copy input to one or more output files.\n\n"
					"\t-p\tcoPy out of the first file instead of stdin.\n"
					"\t-i\tInput block max size\n"
					"\t-o\tOutput block max size (this is how large a string of zeros skip writing)\n"
					"\t-t\tTruncate trailing zero pad (if any, for last file only)\n"
					"\t-l\ttake the very first option after the flag set as a comma seperated List of filesizes, the last size being reused for all subsiquent files (except stdout)\n"
					, *(argv - 1));
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

	f_out = NULL;
	my_nextListFile(&f_out, &argv, &arg_sizeList, &ii_seek_max);
	
	while (!feof(f_in)) {
		// Step 1/2: Read Buffer
		ii_count_i = fread(buf, sz_input, 1, f_in) * sz_input;
		if (ii_count_i == 0) // Didn't read all records, figure out the byte size the hard way.
			ii_count_i = ftell(f_in) - ii_seek_i;
		ii_seek_i += ii_count_i;

		// Step 2/2: Loop writing Output buffer units (skipping if all 0s)

		for (ii_buf_pos = 0 ; ii_buf_pos < ii_count_i; ii_buf_pos += ii_count_o) {
			md_copy=0;	// Reuse md_copy as a state variable
			for (ii_count_o = 0; ii_count_o < ii_count_i && ii_count_o < sz_output; ii_count_o++) {
				if ( ((char *)buf)[ii_buf_pos + ii_count_o] != '\0' ) {
					md_copy=1;
					break;
				}
			}
			ii_write_len = ii_count_i - ii_buf_pos;
			if (ii_write_len > sz_output) ii_write_len = sz_output;
#ifdef DEBUG
	fprintf(f_err, "Write %hu: %lx > %lx bpos:%lu len:%lu\n", md_copy, ii_seek_i, ii_seek_o, ii_buf_pos, ii_write_len);
#endif // DEBUG
			if (!md_copy) {
				if (ii_seek_o + ii_write_len < ii_seek_max) {
					ii_seek_o += ii_write_len;
				} else {
					my_seekOrDie(f_out, ii_seek_max - 1, SEEK_SET);
					ii_count_o = fwrite(buf, 1, 1, f_out);
					if (!ii_count_o)
					  fprintf(stderr, "Write error for last 0-byte section of %s.", *(argv - 1));
					ii_seek_o = ii_seek_o + ii_write_len - ii_seek_max;
					my_nextListFile(&f_out, &argv, &arg_sizeList, &ii_seek_max);
				}
				ii_count_o = ii_write_len;
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
					my_nextListFile(&f_out, &argv, &arg_sizeList, &ii_seek_max);
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
