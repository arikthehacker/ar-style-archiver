# Ariella Marchuk
# amarchuk@pdx.edu
# cs333 Lab 2 UNIX File I/O
# Builds arvik-md4 and also links to md library

CC = gcc
CFLAGS = -g -Wall -Wextra -Wshadow -Wunreachable-code -Wredundant-decls \
         -Wmissing-declarations -Wold-style-definition -Wmissing-prototypes \
         -Wdeclaration-after-statement -Wno-return-local-addr \
         -Wunsafe-loop-optimizations -Wuninitialized -Werror
LDFLAGS = -lmd

TARGET = arvik-md4
OBJS = arvik-md4.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS) *~ \#*


run: $(TARGET)
	@echo "one" > one.txt; echo "two" > two.txt
	./$(TARGET) -cvf demo.arvik one.txt two.txt
	./$(TARGET) -tvf demo.arvik
	@rm -f one.txt two.txt demo.arvik

test:
	./test.sh

.PHONY: all clean run test
