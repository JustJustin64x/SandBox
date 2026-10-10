CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib -lraylib -lm -lpthread

TARGET = worldbox
SRC = $(wildcard src/*.c)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe