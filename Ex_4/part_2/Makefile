# Define variables
CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lnsl
SRC = part_2.c
OBJ = part_2.o
EXEC = part_2

## Define the directory path
#DIR = /home/neriya/CLionProjects/CN_ex4/Ex_4/part_2_1

# Targets
all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJ) $(LDFLAGS)

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c $(SRC) -o $(OBJ)

clean:
	rm -f $(OBJ) $(EXEC)

.PHONY: all clean
