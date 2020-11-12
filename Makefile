#override CC := gcc $(CC)
override CFLAGS := -Wall -Werror -std=gnu11 -pedantic -O0 -g $(CFLAGS) # or whatever your build needs
override LDLIBS := -lpthread $(LDLIBS)

tls.o: tls.c tls.h
	$(CC) $(CFLAGS) $(LDLIBS) -c tls.c -o tls.o

test/%: test/%.o tls.o
	$(CC) $(LDFLAGS) $+ $(LOADLIBES) $(LDLIBS) -o $@

# Discover all test files
test_files=$(shell find test -type f -name '*.c')

# check, checkprogs, and clean aren't actual output files of the build
.PHONY: check checkprogs clean

# The intermediate test .o files shouldn't be auto-deleted in test runs; they
# may be useful for incremental builds while fixing tls.c bugs.
.SECONDARY: $(test_files:.c=.o)

# Compile and link the test files
checkprogs: $(test_files:.c=) tls.o

# Execute all tests
check: checkprogs
	test/run_tests.sh $(test_files:.c=)

clean:
	rm -f $(test_files:.c=) $(test_files:.c=.o) *.o
