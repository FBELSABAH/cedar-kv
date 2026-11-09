CC = cc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror

all: cedar-kv
cedar-kv: server.c
	$(CC) $(CFLAGS) server.c -o $@
test: cedar-kv
	python3 test.py
sanitize:
	$(CC) -std=c11 -g -O1 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer server.c -o cedar-kv-sanitize
	KV_BINARY=./cedar-kv-sanitize python3 test.py
benchmark: cedar-kv
	python3 benchmark.py
clean:
	rm -f cedar-kv cedar-kv-sanitize
.PHONY: all test sanitize benchmark clean
