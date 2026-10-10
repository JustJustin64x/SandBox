CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude
SRC = $(wildcard src/*.c)

# Betriebssystem erkennen
ifeq ($(OS),Windows_NT)
    # Windows (MinGW / w64devkit)
    TARGET = worldbox.exe
    LDFLAGS = -lraylib -lopengl32 -lgdi32 -lwinmm
    RM = del /Q /F
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        # macOS (Homebrew)
        TARGET = worldbox
        CFLAGS += -I/opt/homebrew/include
        LDFLAGS = -L/opt/homebrew/lib -lraylib -lm -lpthread
        RM = rm -f
    else
        # Linux
        TARGET = worldbox
        LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
        RM = rm -f
    endif
endif

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

run: all
	./$(TARGET)

clean:
	$(RM) $(TARGET)