ifeq ($(OS), Windows_NT)
	EXE ?= Starzix

	ENDS_WITH := exe
	SUFFIX :=
	ifneq ($(patsubst %$(ENDS_WITH),,$(lastword $(EXE))),)
		SUFFIX := .exe
	endif

	command := clang++ -O3 -std=c++20 -march=native -Wl,/STACK:16777216 -Wunused -Wall -Wextra -DNDEBUG src/main.cpp -o $(EXE)$(SUFFIX)
else
	EXE ?= starzix
	command := clang++ -O3 -std=c++20 -lstdc++ -lm -march=native -Wunused -Wall -Wextra -DNDEBUG src/main.cpp -o $(EXE)
endif

# Build rule
all:
	$(command)
