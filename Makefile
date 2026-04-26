# Nerdle solver build
CXX = g++
CXXFLAGS = -O3 -std=c++17
SRCDIR = src
BINDIR = .

.PHONY: all clean micro_policy mini_policy canonical_tables report_partition_8 report_partition_maxi compare_first_8 list_partition_failures_8 maxi_unique_partition_sample maxi_first_partition_sweep maxi_opening_partition_stats

all: generate generate_maxi solve solve_adaptive solve_binerdle solve_quadnerdle bench_nerdle bench_partition_aggregate bench_entropy_aggregate nerdle nerdle_micro binerdle bench_binerdle quadnerdle bench_quadnerdle optimal_expected optimal_subgame midi_exact compare_subgame_entropy compare_full_restricted_subgame explore_first_guess first_guess_tiered_sim first_guess_recursive_ev first_guess_staged_sample trace_target compare_bellman solver_json compare_first_8 list_partition_failures_8 maxi_unique_partition_sample maxi_first_partition_sweep partition_report

generate: $(SRCDIR)/generate.cpp $(SRCDIR)/equation_canonical.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -I$(SRCDIR) -o $(BINDIR)/$@ $(SRCDIR)/generate.cpp

generate_maxi: $(SRCDIR)/generate_maxi.cpp $(SRCDIR)/equation_canonical.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -I$(SRCDIR) -o $(BINDIR)/$@ $(SRCDIR)/generate_maxi.cpp

solve: $(SRCDIR)/solve.cpp $(SRCDIR)/equation_canonical.hpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/solve.cpp

solve_adaptive: $(SRCDIR)/solve_adaptive.cpp $(SRCDIR)/equation_canonical.hpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/solve_adaptive.cpp

solve_binerdle: $(SRCDIR)/solve_binerdle.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/solve_binerdle.cpp

solve_quadnerdle: $(SRCDIR)/solve_quadnerdle.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/solve_quadnerdle.cpp

bench_nerdle: $(SRCDIR)/bench_nerdle.cpp $(SRCDIR)/bench_solve.hpp $(SRCDIR)/equation_canonical.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp $(SRCDIR)/optimal_policy_build.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/bench_nerdle.cpp

bench_partition_aggregate: $(SRCDIR)/partition_report.cpp $(SRCDIR)/equation_canonical.hpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -I$(SRCDIR) -o $(BINDIR)/$@ $(SRCDIR)/partition_report.cpp

bench_entropy_aggregate: $(SRCDIR)/bench_entropy_aggregate.cpp $(SRCDIR)/equation_canonical.hpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -Isrc -o $(BINDIR)/$@ $(SRCDIR)/bench_entropy_aggregate.cpp

compare_bellman: $(SRCDIR)/compare_bellman.cpp $(SRCDIR)/bench_solve.hpp $(SRCDIR)/equation_canonical.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp $(SRCDIR)/optimal_policy_build.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/compare_bellman.cpp

nerdle: $(SRCDIR)/nerdle.cpp $(SRCDIR)/nerdle_interactive.cpp $(SRCDIR)/nerdle_interactive.hpp $(SRCDIR)/bench_solve.hpp $(SRCDIR)/equation_canonical.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp $(SRCDIR)/optimal_policy_build.hpp
	$(CXX) $(CXXFLAGS) -o $(BINDIR)/$@ $(SRCDIR)/nerdle.cpp $(SRCDIR)/nerdle_interactive.cpp

nerdle_micro: $(SRCDIR)/nerdle_micro.cpp $(SRCDIR)/nerdle_interactive.cpp $(SRCDIR)/nerdle_interactive.hpp $(SRCDIR)/bench_solve.hpp $(SRCDIR)/equation_canonical.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp $(SRCDIR)/optimal_policy_build.hpp
	$(CXX) $(CXXFLAGS) -o $(BINDIR)/$@ $(SRCDIR)/nerdle_micro.cpp $(SRCDIR)/nerdle_interactive.cpp

solver_json: $(SRCDIR)/solver_json.cpp $(SRCDIR)/bench_solve.hpp $(SRCDIR)/equation_canonical.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/solver_json.cpp

partition_report: $(SRCDIR)/partition_report.cpp $(SRCDIR)/equation_canonical.hpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -I$(SRCDIR) -o $(BINDIR)/$@ $(SRCDIR)/partition_report.cpp

# 8-tile: same partition+report engine; increase --tie-depth stepwise (0,1,2,…) to measure cost
report_partition_8: partition_report
	./partition_report --pool data/equations_8.txt --tie-depth 0

# Maxi: full pool is ~1.8M; use --max for a prefix (canonical order) so the report n×n table + policy eval fit RAM/time
report_partition_maxi: partition_report
	./partition_report --pool data/equations_10.txt --tie-depth 0 --max 15000

compare_first_8: $(SRCDIR)/compare_first_8.cpp $(SRCDIR)/equation_canonical.hpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -I$(SRCDIR) -o $(BINDIR)/$@ $(SRCDIR)/compare_first_8.cpp

list_partition_failures_8: $(SRCDIR)/list_partition_failures_8.cpp $(SRCDIR)/equation_canonical.hpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -I$(SRCDIR) -o $(BINDIR)/$@ $(SRCDIR)/list_partition_failures_8.cpp

maxi_unique_partition_sample: $(SRCDIR)/maxi_unique_partition_sample.cpp $(SRCDIR)/equation_canonical.hpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -I$(SRCDIR) -o $(BINDIR)/$@ $(SRCDIR)/maxi_unique_partition_sample.cpp

maxi_first_partition_sweep: $(SRCDIR)/maxi_first_partition_sweep.cpp $(SRCDIR)/equation_canonical.hpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -I$(SRCDIR) -o $(BINDIR)/$@ $(SRCDIR)/maxi_first_partition_sweep.cpp

maxi_opening_partition_stats: $(SRCDIR)/maxi_opening_partition_stats.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -o $(BINDIR)/$@ $(SRCDIR)/maxi_opening_partition_stats.cpp

binerdle: $(SRCDIR)/binerdle.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/binerdle.cpp

bench_binerdle: $(SRCDIR)/bench_binerdle.cpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $<

quadnerdle: $(SRCDIR)/quadnerdle.cpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/quadnerdle.cpp

bench_quadnerdle: $(SRCDIR)/bench_quadnerdle.cpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $<

optimal_expected: $(SRCDIR)/optimal_expected.cpp $(SRCDIR)/equation_canonical.hpp $(SRCDIR)/nerdle_core.hpp $(SRCDIR)/micro_policy.hpp
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

equation_canonical_table: $(SRCDIR)/equation_canonical_table.cpp $(SRCDIR)/equation_canonical.hpp
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -o $(BINDIR)/$@ $(SRCDIR)/equation_canonical_table.cpp

# TSV: equation, distinct, purple, green, partition — canonical row order
canonical_tables: equation_canonical_table
	mkdir -p data/canonical_tables
	./equation_canonical_table data/equations_5.txt --out data/canonical_tables/eq_5.tsv
	./equation_canonical_table data/equations_6.txt --out data/canonical_tables/eq_6.tsv
	./equation_canonical_table data/equations_7.txt --out data/canonical_tables/eq_7.tsv
	./equation_canonical_table data/equations_8.txt --out data/canonical_tables/eq_8.tsv --max 10000
	./equation_canonical_table data/equations_10.txt --out data/canonical_tables/eq_10.tsv --max 5000

# Regenerate after changing equations or Bellman tie-break in optimal_expected.cpp
micro_policy: optimal_expected data/equations_5.txt
	./optimal_expected data/equations_5.txt --write-policy data/optimal_policy_5.bin --quiet

mini_policy: optimal_expected data/equations_6.txt
	./optimal_expected data/equations_6.txt --write-policy data/optimal_policy_6.bin --quiet

trace_target: $(SRCDIR)/trace_target.cpp $(SRCDIR)/equation_canonical.hpp $(SRCDIR)/nerdle_core.hpp
	$(CXX) $(CXXFLAGS) -fopenmp -o $(BINDIR)/$@ $(SRCDIR)/trace_target.cpp

clean:
	rm -f $(BINDIR)/generate $(BINDIR)/generate_maxi $(BINDIR)/solve \
	      $(BINDIR)/solve_adaptive $(BINDIR)/solve_binerdle $(BINDIR)/solve_quadnerdle \
	      $(BINDIR)/bench_nerdle $(BINDIR)/nerdle $(BINDIR)/nerdle_micro $(BINDIR)/binerdle \
	      $(BINDIR)/bench_binerdle $(BINDIR)/quadnerdle $(BINDIR)/bench_quadnerdle \
	      $(BINDIR)/optimal_expected $(BINDIR)/equation_canonical_table \
	      $(BINDIR)/optimal_subgame $(BINDIR)/midi_exact $(BINDIR)/compare_subgame_entropy \
	      $(BINDIR)/compare_full_restricted_subgame $(BINDIR)/explore_first_guess \
	      $(BINDIR)/first_guess_tiered_sim $(BINDIR)/first_guess_recursive_ev \
	      $(BINDIR)/first_guess_staged_sample $(BINDIR)/trace_target $(BINDIR)/compare_bellman \
	      $(BINDIR)/solver_json $(BINDIR)/partition_report $(BINDIR)/compare_first_8 \
	      $(BINDIR)/list_partition_failures_8 $(BINDIR)/maxi_unique_partition_sample \
	      $(BINDIR)/maxi_first_partition_sweep $(BINDIR)/bench_partition_aggregate \
	      $(BINDIR)/bench_entropy_aggregate
