# Nerdle solver build
CXX = g++
CXXFLAGS = -O3 -std=c++17
SRCDIR = src
BINDIR = .

.PHONY: all clean micro_policy

all: generate generate_maxi solve solve_adaptive solve_binerdle solve_quadnerdle bench_nerdle nerdle binerdle bench_binerdle quadnerdle bench_quadnerdle optimal_expected trace_target

generate: $(SRCDIR)/generate.cpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $<

generate_maxi: $(SRCDIR)/generate_maxi.cpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $<

solve: $(SRCDIR)/solve.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/solve.cpp

solve_adaptive: $(SRCDIR)/solve_adaptive.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/solve_adaptive.cpp

solve_binerdle: $(SRCDIR)/solve_binerdle.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/solve_binerdle.cpp

solve_quadnerdle: $(SRCDIR)/solve_quadnerdle.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/solve_quadnerdle.cpp

bench_nerdle: $(SRCDIR)/bench_nerdle.cpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/bench_nerdle.cpp

nerdle: $(SRCDIR)/nerdle.cpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp
	$(CXX) $(CXXFLAGS) -o $(BINDIR)/$@ $(SRCDIR)/nerdle.cpp

binerdle: $(SRCDIR)/binerdle.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/binerdle.cpp

bench_binerdle: $(SRCDIR)/bench_binerdle.cpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $<

quadnerdle: $(SRCDIR)/quadnerdle.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/quadnerdle.cpp

bench_quadnerdle: $(SRCDIR)/bench_quadnerdle.cpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $<

optimal_expected: $(SRCDIR)/optimal_expected.cpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp
	$(CXX) $(CXXFLAGS) -o $(BINDIR)/$@ $(SRCDIR)/optimal_expected.cpp

# Regenerate after changing equations or Bellman tie-break in optimal_expected.cpp
micro_policy: optimal_expected data/equations_5.txt
	./optimal_expected data/equations_5.txt --write-policy data/optimal_policy_5.bin --quiet

trace_target: $(SRCDIR)/trace_target.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/trace_target.cpp

clean:
	rm -f $(BINDIR)/generate $(BINDIR)/generate_maxi $(BINDIR)/solve \
	      $(BINDIR)/solve_adaptive $(BINDIR)/solve_binerdle $(BINDIR)/solve_quadnerdle \
	      $(BINDIR)/bench_nerdle $(BINDIR)/nerdle $(BINDIR)/binerdle \
	      $(BINDIR)/bench_binerdle $(BINDIR)/quadnerdle $(BINDIR)/bench_quadnerdle \
	      $(BINDIR)/optimal_expected $(BINDIR)/trace_target
