# Nerdle solver build
CXX = g++
CXXFLAGS = -O3 -std=c++17
SRCDIR = src
BINDIR = .

.PHONY: all clean micro_policy mini_policy

all: generate generate_maxi solve solve_adaptive solve_binerdle solve_quadnerdle bench_nerdle nerdle binerdle bench_binerdle quadnerdle bench_quadnerdle optimal_expected optimal_subgame midi_exact compare_subgame_entropy compare_full_restricted_subgame explore_first_guess first_guess_tiered_sim first_guess_recursive_ev first_guess_staged_sample trace_target compare_bellman

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

bench_nerdle: $(SRCDIR)/bench_nerdle.cpp $(SRCDIR)/bench_solve.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp $(SRCDIR)/optimal_policy_build.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/bench_nerdle.cpp

compare_bellman: $(SRCDIR)/compare_bellman.cpp $(SRCDIR)/bench_solve.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp $(SRCDIR)/optimal_policy_build.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/compare_bellman.cpp

nerdle: $(SRCDIR)/nerdle.cpp $(SRCDIR)/bench_solve.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp $(SRCDIR)/optimal_policy_build.hpp
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

optimal_subgame: $(SRCDIR)/optimal_subgame.cpp $(SRCDIR)/subgame_optimal.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp
	$(CXX) $(CXXFLAGS) -o $(BINDIR)/$@ $(SRCDIR)/optimal_subgame.cpp

midi_exact: $(SRCDIR)/midi_exact.cpp $(SRCDIR)/subgame_optimal.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp
	$(CXX) $(CXXFLAGS) -o $(BINDIR)/$@ $(SRCDIR)/midi_exact.cpp

compare_subgame_entropy: $(SRCDIR)/compare_subgame_entropy.cpp $(SRCDIR)/subgame_optimal.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/compare_subgame_entropy.cpp

compare_full_restricted_subgame: $(SRCDIR)/compare_full_restricted_subgame.cpp $(SRCDIR)/subgame_optimal.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/compare_full_restricted_subgame.cpp

explore_first_guess: $(SRCDIR)/explore_first_guess.cpp $(SRCDIR)/subgame_optimal.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/explore_first_guess.cpp

first_guess_tiered_sim: $(SRCDIR)/first_guess_tiered_sim.cpp $(SRCDIR)/bench_solve.hpp $(SRCDIR)/subgame_optimal.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/first_guess_tiered_sim.cpp

first_guess_recursive_ev: $(SRCDIR)/first_guess_recursive_ev.cpp $(SRCDIR)/subgame_optimal.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp
	$(CXX) $(CXXFLAGS) -o $(BINDIR)/$@ $(SRCDIR)/first_guess_recursive_ev.cpp

first_guess_staged_sample: $(SRCDIR)/first_guess_staged_sample.cpp $(SRCDIR)/bench_solve.hpp $(SRCDIR)/subgame_optimal.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/first_guess_staged_sample.cpp

# Regenerate after changing equations or Bellman tie-break in optimal_expected.cpp
micro_policy: optimal_expected data/equations_5.txt
	./optimal_expected data/equations_5.txt --write-policy data/optimal_policy_5.bin --quiet

mini_policy: optimal_expected data/equations_6.txt
	./optimal_expected data/equations_6.txt --write-policy data/optimal_policy_6.bin --quiet

trace_target: $(SRCDIR)/trace_target.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/trace_target.cpp

clean:
	rm -f $(BINDIR)/generate $(BINDIR)/generate_maxi $(BINDIR)/solve \
	      $(BINDIR)/solve_adaptive $(BINDIR)/solve_binerdle $(BINDIR)/solve_quadnerdle \
	      $(BINDIR)/bench_nerdle $(BINDIR)/nerdle $(BINDIR)/binerdle \
	      $(BINDIR)/bench_binerdle $(BINDIR)/quadnerdle $(BINDIR)/bench_quadnerdle \
	      $(BINDIR)/optimal_expected $(BINDIR)/optimal_subgame $(BINDIR)/midi_exact $(BINDIR)/compare_subgame_entropy $(BINDIR)/compare_full_restricted_subgame $(BINDIR)/explore_first_guess $(BINDIR)/first_guess_tiered_sim $(BINDIR)/first_guess_recursive_ev $(BINDIR)/first_guess_staged_sample $(BINDIR)/trace_target $(BINDIR)/compare_bellman
