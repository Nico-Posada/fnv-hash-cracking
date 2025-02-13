TEST_NAME = test
BENCHMARK_NAME = benchmark
OUTPUT_DIR = build
CXX = g++
CXXFLAGS = -x c++ -march=native -O3 -std=c++20
LIBS = -lfplll -lgmp
INCLUDES = -I./src

.PHONY: build test run-test benchmark run-benchmark clean

test:
	@mkdir -vp $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) -o $(OUTPUT_DIR)/$(TEST_NAME) tests/$(TEST_NAME).cpp $(LIBS) $(INCLUDES)

run-test: test
	./$(OUTPUT_DIR)/$(TEST_NAME)

benchmark:
	@mkdir -vp $(OUTPUT_DIR)
	$(CXX) $(CXXFLAGS) -o $(OUTPUT_DIR)/$(BENCHMARK_NAME) tests/$(BENCHMARK_NAME).cpp $(LIBS) $(INCLUDES)

run-benchmark: benchmark
	./$(OUTPUT_DIR)/$(BENCHMARK_NAME)

clean:
	@if [ ! -d $(OUTPUT_DIR) ]; then \
		echo "No files to clean"; \
	else \
		rm -vr $(OUTPUT_DIR)/; \
	fi