# Nerdle solver build
CXX = g++
CXXFLAGS = -O3 -std=c++17
SRCDIR = src
BINDIR = .

.PHONY: all clean

all: generate generate_maxi solve solve_adaptive solve_binerdle solve_quadnerdle bench_nerdle nerdle binerdle bench_binerdle quadnerdle bench_quadnerdle

generate: $(SRCDIR)/generate.cpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $<

generate_maxi: $(SRCDIR)/generate_maxi.cpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $<

solve: $(SRCDIR)/solve.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/solve.cpp

solve_adaptive: $(SRCDIR)/solve_adaptive.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/solve_adaptive.cpp

solve_binerdle: $(SRCDIR)/solve_binerdle.cpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $<

solve_quadnerdle: $(SRCDIR)/solve_quadnerdle.cpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $<

bench_nerdle: $(SRCDIR)/bench_nerdle.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/bench_nerdle.cpp

nerdle: $(SRCDIR)/nerdle.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -o $(BINDIR)/$@ $(SRCDIR)/nerdle.cpp

binerdle: $(SRCDIR)/binerdle.cpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $<

bench_binerdle: $(SRCDIR)/bench_binerdle.cpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $<

quadnerdle: $(SRCDIR)/quadnerdle.cpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $<

bench_quadnerdle: $(SRCDIR)/bench_quadnerdle.cpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $<

clean:
	rm -f $(BINDIR)/generate $(BINDIR)/generate_maxi $(BINDIR)/solve \
	      $(BINDIR)/solve_adaptive $(BINDIR)/solve_binerdle $(BINDIR)/solve_quadnerdle \
	      $(BINDIR)/bench_nerdle $(BINDIR)/nerdle $(BINDIR)/binerdle \
	      $(BINDIR)/bench_binerdle $(BINDIR)/quadnerdle $(BINDIR)/bench_quadnerdle
