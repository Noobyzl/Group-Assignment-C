CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 -I. -IModule_2 -IModule_4 -IModule_5
LDFLAGS = -lm
TARGET  = solar_planner

SRC = Module_1/loader.c Module_1/loader_helper.c Module_1/panels.c \
      Module_2/Yield.c Module_2/Fault.c Module_3/Logic_Decision.c \
      Module_4/impact.c Module_4/sweep.c Module_4/tnb.c \
      Module_5/main.c Module_5/report.c Module_5/search.c
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET) $(TARGET).exe

.PHONY: all clean run