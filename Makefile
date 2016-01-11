CXX=g++ -std=c++11 -O3 -march=native -mtune=native -Wall -Wextra
CXXFLAGS=-I./integer_encoding_library/include
LIBFLAGS= -I../tng/include -fPIC -DHRTC_VERSION=$(shell git log | head -n1 | cut -f2 -d' ')
BINFLAGS=-lboost_program_options -fwhole-program
LDFLAGS=$(wildcard ./integer_encoding_library/src/*.o) \
	$(wildcard ./integer_encoding_library/src/compress/*.o) \
	$(wildcard ./integer_encoding_library/src/compress/table/*.o) \
	$(wildcard ./integer_encoding_library/src/io/*.o)
SHELL=/bin/bash

all: bin lib

.PHONY: bin
bin: hrtc

.PHONY: lib
lib: hrtc_wrapper.o

.PHONY: clean
clean:
	-rm hrtc *~ test/*{~,.{compr,loop,ident,line_count}} *.o

%: %.cpp $(wildcard *.hpp)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(BINFLAGS) $< -o $@

hrtc_wrapper.o: hrtc_wrapper.cpp $(wildcard *.hpp)
	$(CXX) -c $(CXXFLAGS) $(LIBFLAGS) $< -o $@


### TESTS

IDENT_TESTS := one_frame three_frames alternate manycol longtrans longrand
LINECOUNT_TESTS := rand

TEST_BOUND := 100
TEST_ERROR := 0.1

.PHONY: test
test: hrtc \
	$(patsubst %,test/%.ident,$(IDENT_TESTS)) \
	$(patsubst %,test/%.line_count,$(LINECOUNT_TESTS))

pass = (echo -e "\033[42m\033[37m\033[1m PASS \033[0m $@")
fail = (echo -e "\033[41m\033[37m\033[1m FAIL \033[0m $@" && false)
col_count = $(shell head -n1 $1 | tr "\t" "\n" | wc -l)

test/test_num: test_num
	time ./$<
	touch $@

.PRECIOUS: test/%.compr
test/%.compr: test/% hrtc
	@echo -e "Compress\t$<"
	./hrtc --src $< --numtraj $(call col_count,$<) --bound $(TEST_BOUND) --error $(TEST_ERROR) --format=tsvfloat --compress >$@~
	mv $@~ $@
	echo foo

.PRECIOUS: test/%.loop
test/%.loop: test/%.compr hrtc
	@echo -e "Decompress\t$<"
	./hrtc --src $< --numtraj $(call col_count,$(patsubst %.compr,%,$<)) --bound $(TEST_BOUND) --error $(TEST_ERROR) --format=tsvfloat --decompress >$@~
	mv $@~ $@

test/%.ident: test/% test/%.loop
	@[ "$$(md5sum <$<)" == "$$(md5sum <$<.loop)" ] || $(fail)
	@$(pass)

test/%.line_count: test/% test/%.loop
	@[ "$$(wc <$<)" == "$$(wc <$<.loop)" ] || $(fail)
	@$(pass)


### benchmarks

.PRECIOUS: bench/%.time_size
bench/%.time_size: bench/% hrtc
	set -o pipefail; \
	for encoding in $$(seq 0 7; seq 9 17); do \
	  (time (echo -en "code\t$$encoding\nsize\t"; ./hrtc --numtraj 429 --compress --bound 2 --error 0.04 --integer-encoding $$encoding <$< | wc -c)) 2>&1 \
	    | grep -e ^user -e ^size -e ^code | cut -f2 | sed 's/m/*60 + /' | sed 's/s//' | bc | tr '\n' '\t'; \
	  echo; \
	done > $@~
	mv $@~ $@

bench/%.time_size.pdf: bench/%.time_size
	(echo "set terminal pdf;"  \
	      "set output '$@~';" \
              "set xlabel 'size [B]';" \
              "set ylabel 'time [s]';" \
	      "set title 'size and time requirements of different integer encoders';" \
	      "plot '$<' using (175718400/"'$$'"2):3:1 with labels point offset 1 title ''") | gnuplot
	mv $@~ $@
