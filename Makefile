# Compiler configuration with portable fallback
CXX ?= $(shell \
	if command -v clang++ >/dev/null 2>&1 && echo "int main(){}" | clang++ -std=c++20 -x c++ - -o /dev/null 2>/dev/null; then echo clang++; \
	elif command -v g++ >/dev/null 2>&1 && echo "int main(){}" | g++ -std=c++20 -x c++ - -o /dev/null 2>/dev/null; then echo g++; \
	else echo c++; fi)

CXXFLAGS ?= -O3 -march=native -std=c++20
INCLUDES := -Ithird_party/cpp-TimSort/include -Iinclude

SRCS := $(wildcard bench_*.cpp)
BINS := $(patsubst %.cpp,bin/%,$(SRCS))

.PHONY: all clean smoke test help

all: bin $(BINS)
	@echo "All benchmark binaries successfully built in bin/."

bin:
	mkdir -p bin

bin/%: %.cpp | bin
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

smoke: all
	@echo "Running smoke tests..."
	./run/smoke_test.sh

clean:
	rm -f bin/bench_*
	@echo "Cleaned build artifacts."

help:
	@echo "LadderSort Build System"
	@echo "  make          - Build all benchmark executables into bin/"
	@echo "  make smoke    - Run quick ablation smoke tests"
	@echo "  make clean    - Remove built executables"
