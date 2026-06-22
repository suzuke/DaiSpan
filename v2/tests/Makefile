# Host unit tests for the pure S21 codec (no ESP-IDF toolchain required).
#   make        build and run the tests
#   make clean  remove the test binary
CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -I../main
LDLIBS  := -lm

test: test_s21_codec
	./test_s21_codec

test_s21_codec: test_s21_codec.c ../main/s21/s21_codec.c ../main/s21/s21_codec.h ../main/common/types.h
	$(CC) $(CFLAGS) -o $@ test_s21_codec.c ../main/s21/s21_codec.c $(LDLIBS)

clean:
	rm -f test_s21_codec

.PHONY: test clean
