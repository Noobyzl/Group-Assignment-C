# Original (unrefactored) sources. Run `make` in WSL (or Linux): it builds solar_planner.exe,
# a static Windows program. That one file runs on Windows and, through Windows interop,
# inside WSL too, so it is the file to send with the source code.
#
#   make          build solar_planner.exe  (needs: sudo apt install mingw-w64)
#   make linux    build a native Linux binary, solar_planner (only runs in WSL/Linux)
#   make run      build the .exe and run it
#   make clean
#
# On plain Windows (mingw32-make) `make` builds the same solar_planner.exe with gcc.

TARGET  = solar_planner
CFLAGS  = -Wall -Wextra -Wpedantic -std=c99 -I. -IModule_2 -IModule_4 -IModule_5
LDFLAGS = -lm

ifeq ($(OS),Windows_NT)
  WINCC  = gcc
  RM     = cmd /C "del /Q /F $(TARGET).exe $(TARGET) 2>nul"
else
  WINCC  = x86_64-w64-mingw32-gcc
  RM     = rm -f $(TARGET).exe $(TARGET)
endif

SRC = Module_1/loader.c Module_1/loader_helper.c Module_1/panels.c \
      Module_2/Yield.c Module_2/Fault.c Module_3/Logic_Decision.c \
      Module_4/impact.c Module_4/sweep.c Module_4/tnb.c \
      Module_5/main.c Module_5/report.c Module_5/search.c

all: $(TARGET).exe

# -static: no MinGW DLLs needed on the machine that runs it.
$(TARGET).exe: $(SRC) $(wildcard Module_*/*.h) common.h
	$(WINCC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS) -static

linux: $(TARGET)

$(TARGET): $(SRC) $(wildcard Module_*/*.h) common.h
	gcc $(CFLAGS) $(SRC) -o $@ $(LDFLAGS)

run: $(TARGET).exe
	./$(TARGET).exe

clean:
	-$(RM)

.PHONY: all linux run clean
