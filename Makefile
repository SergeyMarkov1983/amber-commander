CC := clang++
CXXFLAGS := -std=c++20 -O2 -municode -mwindows -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0A00 -Isrc -Isrc/ui -Isrc/core
LDLIBS := -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 -luuid -ld2d1 -ldwrite -lwindowscodecs -ladvapi32 -ldwmapi

SRC := $(wildcard src/*.cpp src/core/*.cpp src/ui/*.cpp)
RES := res/amber_res.o

amber.exe: $(SRC) $(RES)
	$(CC) $(CXXFLAGS) $(SRC) $(RES) $(LDLIBS) -o $@

res/amber_res.o: res/amber.rc res/app.manifest res/amber.ico
	windres res/amber.rc res/amber_res.o

clean:
	del /q amber.exe res\amber_res.o

.PHONY: clean
