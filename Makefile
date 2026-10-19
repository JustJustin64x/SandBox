CXX = g++
CXXFLAGS = -Wall -Wextra -Wno-missing-field-initializers -std=c++17 \
           -Iinclude \
           -Iinclude/core \
           -Iinclude/gfx \
           -Iinclude/world \
           -Iinclude/entities \
           -Iinclude/buildings \
           -Iinclude/ai \
           -Iinclude/social \
           -Iinclude/progression \
           -Iinclude/ui \
           -Iinclude/civ \
           -O2

# Alle Quelldateien rekursiv erfassen
SRC = $(wildcard src/*.cpp) $(wildcard src/*/*.cpp)

# Betriebssystem erkennen
ifeq ($(OS),Windows_NT)
    TARGET = worldbox.exe
    LDFLAGS = -lraylib -lopengl32 -lgdi32 -lwinmm
    RM = del /Q /F
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        TARGET = worldbox
        CXXFLAGS += -I/opt/homebrew/include
        LDFLAGS = -L/opt/homebrew/lib -lraylib -lm -lpthread
        RM = rm -f
    else
        TARGET = worldbox
        LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
        RM = rm -f
    endif
endif

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

run: all
	./$(TARGET)

clean:
	$(RM) $(TARGET)