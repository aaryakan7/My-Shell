# Compiler and flags.
CC = gcc
CFLAGS = -Wall -Wextra -std=c11

# Target executable.
TARGET = mysh

# Source files.
SRC = mysh.c

# Default target: build the executable.
all: $(TARGET)

test: $(TARGET)
	@set -e; \
	tmpdir=$$(mktemp -d); \
	trap 'rm -rf "$$tmpdir"' 0; \
	cp test.sh "$$tmpdir/test.sh"; \
	cd "$$tmpdir"; \
	"$(CURDIR)/$(TARGET)" test.sh > .mysh-test-output 2> .mysh-test-errors; \
	for marker in CHECK_INITIAL_CONDITIONAL CHECK_INLINE_COMMENT CHECK_PWD_REDIRECT CHECK_WHICH_REDIRECT CHECK_PWD_PIPE CHECK_WHICH_PIPE CHECK_PIPE_LAST_SUCCESS CHECK_PIPE_LAST_FAILURE CHECK_AND CHECK_OR CHECK_PIPE_SYNTAX CHECK_REDIRECT_SYNTAX CHECK_EXTRA_PIPE_SYNTAX CHECK_WILDCARD CHECK_REDIRECT_FAILURE; do \
		if ! grep -q "$$marker" .mysh-test-output; then \
			cat .mysh-test-output; cat .mysh-test-errors; echo "Missing test result: $$marker"; exit 1; \
		fi; \
	done; \
	if grep -q 'BAD_INITIAL_CONDITIONAL\|should-not-run' .mysh-test-output; then echo "an invalid command ran"; exit 1; fi; \
	{ printf 'echo'; i=0; while [ "$$i" -lt 100 ]; do printf ' x'; i=$$((i + 1)); done; printf '\nor echo CHECK_TOKEN_LIMIT\n'; } > too-many-tokens.sh; \
	"$(CURDIR)/$(TARGET)" too-many-tokens.sh > .mysh-token-output 2> .mysh-token-errors; \
	grep -q CHECK_TOKEN_LIMIT .mysh-token-output

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

# Clean target: remove the executable.
clean:
	rm -f $(TARGET)

.PHONY: all test clean
