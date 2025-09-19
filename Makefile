# Compiler and flags.
CC = gcc
CFLAGS = -Wall -Wextra -std=c11

# Target executable.
TARGET = mysh

# Source files.
SRC = mysh.c

# Default target: build the executable.
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

# Clean target: remove the executable.
clean:
	rm -f $(TARGET)
