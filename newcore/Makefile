CC = gcc
CFLAGS = -Wall -Wextra -Iinclude -g
LDFLAGS = 
SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
TARGET = psxemu

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(TARGET) 