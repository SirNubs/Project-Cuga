# Convenience commands; CMake remains the build system.
.DEFAULT_GOAL := help

PYTHON ?= python3
CMAKE ?= cmake
CTEST ?= ctest
MAVEN ?= mvn
JOBS ?= 2
BUILD_DIR ?= out/tests
BUILD_TYPE ?= Debug
GENERATOR ?= Ninja
CMAKE_ARGS ?=
CTEST_ARGS ?=
TOOL_DIR ?= out/dev-tools
TOOL_BIN := $(TOOL_DIR)/bin
TOOL_PYTHON := $(TOOL_BIN)/python
SANITIZER_CXX ?= clang++-18
SANITIZER_SYMBOLIZER ?= llvm-symbolizer-18
ifeq ($(shell uname -s),Darwin)
COVERAGE_CXX ?= clang++
else
COVERAGE_CXX ?= clang++-18
endif
COVERAGE_PYTHON := out/coverage-tools/bin/python
NATIVE_COVERAGE_RUN ?= $(PYTHON)
HEADLESS_ARGS := -DPSPRECOMP_PROFILE=mhp3rd -DPSPRECOMP_BUILD_TESTS=ON -DMHP3RD_RENDERER=OFF -DMHP3RD_FFMPEG=OFF

.PHONY: help configure build test python-test java-test check tools lint format-check format tidy-tools tidy cppcheck-tools cppcheck sanitizers coverage coverage-tools python-coverage project-coverage app

help:
	@printf '%s\n' \
	  'make configure       Configure public headless tests (no game data)' \
	  'make build           Build test binaries in BUILD_DIR (default: out/tests)' \
	  'make test            Build and run native tests' \
	  'make python-test     Run archive, coverage-policy and shader-tool suites' \
	  'make java-test       Run Android Java contracts (requires SDL_SOURCE, Maven, Java 17+)' \
	  'make tools           Install pinned Ruff and clang-format in TOOL_DIR' \
	  'make lint            Check Python with Ruff' \
	  'make format-check    Check all scoped first-party C++ formatting' \
	  'make format          Apply the approved first-party C++ formatting' \
	  'make check           Run lint, format-check, native and Python tests in order' \
	  'make tidy-tools      Install the pinned clang-tidy analyzer' \
	  'make tidy            Configure out/tidy and analyze (requires tidy-tools)' \
	  'make cppcheck-tools  Build the pinned Cppcheck analyzer' \
	  'make cppcheck        Configure out/cppcheck and analyze (requires cppcheck-tools)' \
	  'make sanitizers      Configure, verify and run ASan/UBSan on native Linux' \
	  'make coverage        Build/test with LLVM coverage and write an HTML report' \
	  'make coverage-tools  Install the pinned Python coverage collector' \
	  'make python-coverage Run full Python suites with source/branch coverage' \
	  'make project-coverage Collect reports and enforce 80% changed-line coverage' \
	  'make app             Build Yakumo in an already configured APP_BUILD_DIR' \
	  'Overrides: JOBS=2 BUILD_DIR=out/tests CMAKE_ARGS="..." CTEST_ARGS="..."'

configure:
	$(CMAKE) -S . -B "$(BUILD_DIR)" -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) $(HEADLESS_ARGS) $(CMAKE_ARGS)

build:
	$(CMAKE) --build "$(BUILD_DIR)" --target psprecomp_test_binaries --parallel $(JOBS)

test: build
	$(CTEST) --test-dir "$(BUILD_DIR)" --output-on-failure --no-tests=error --timeout 120 $(CTEST_ARGS)

python-test:
	$(PYTHON) profiles/mhp3rd/tests/tool_security_tests.py
	$(PYTHON) -m unittest discover -s scripts/ci -p 'test_*.py'

java-test:
	@test -n "$(SDL_SOURCE)" || { printf '%s\n' 'Set SDL_SOURCE to the pinned SDL source checkout; see docs/COVERAGE.md.' >&2; exit 1; }
	$(MAVEN) -B -f profiles/mhp3rd/tests/android/pom.xml -Dsdl.source="$(SDL_SOURCE)" clean verify

# Recursive commands keep this sequence ordered even with make -j.
check:
	$(MAKE) lint
	$(MAKE) format-check
	$(MAKE) test
	$(MAKE) python-test

tools:
	$(PYTHON) -m venv "$(TOOL_DIR)"
	"$(TOOL_PYTHON)" -m pip install -r requirements/dev.txt
	"$(TOOL_PYTHON)" -m pip install --only-binary=:all: --require-hashes -r scripts/requirements-format.txt

lint:
	"$(TOOL_PYTHON)" -m ruff check .

format-check:
	PATH="$(TOOL_BIN):$$PATH" $(PYTHON) scripts/format_cpp.py check --json

format:
	PATH="$(TOOL_BIN):$$PATH" $(PYTHON) scripts/format_cpp.py format

tidy-tools:
	$(PYTHON) -m venv out/tidy-tools
	out/tidy-tools/bin/python -m pip install --require-hashes -r scripts/ci/clang-tidy-requirements.txt

tidy:
	$(CMAKE) -S . -B out/tidy -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $(HEADLESS_ARGS) $(CMAKE_ARGS)
	$(PYTHON) scripts/ci/clang_tidy.py

cppcheck-tools:
	$(PYTHON) scripts/ci/install_cppcheck.py

cppcheck:
	$(CMAKE) -S . -B out/cppcheck -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $(HEADLESS_ARGS) $(CMAKE_ARGS)
	$(PYTHON) scripts/ci/cppcheck.py

sanitizers:
	$(CMAKE) -S . -B out/sanitizers -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=$(SANITIZER_CXX) -DPSPRECOMP_SANITIZERS=ON $(HEADLESS_ARGS) $(CMAKE_ARGS)
	$(PYTHON) scripts/ci/check_sanitizers.py out/sanitizers
	$(CMAKE) --build out/sanitizers --target psprecomp_test_binaries --parallel $(JOBS)
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=0:exitcode=99 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:abort_on_error=0:exitcode=98 \
	ASAN_SYMBOLIZER_PATH="$$(command -v $(SANITIZER_SYMBOLIZER))" \
	$(CTEST) --test-dir out/sanitizers --output-on-failure --no-tests=error --timeout 120 $(CTEST_ARGS)

coverage:
	$(PYTHON) -m unittest discover -s scripts/ci -p test_native_coverage.py
	$(CMAKE) -S . -B out/coverage -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=$(COVERAGE_CXX) -DPSPRECOMP_COVERAGE=ON $(HEADLESS_ARGS) $(CMAKE_ARGS)
	$(CMAKE) --build out/coverage --target psprecomp_coverage_binaries --parallel $(JOBS)
	$(NATIVE_COVERAGE_RUN) scripts/ci/native_coverage.py

coverage-tools:
	$(PYTHON) -m venv out/coverage-tools
	"$(COVERAGE_PYTHON)" -m pip install -r scripts/ci/coverage-requirements.txt

python-coverage:
	"$(COVERAGE_PYTHON)" -m coverage erase
	"$(COVERAGE_PYTHON)" -m coverage run --parallel-mode profiles/mhp3rd/tests/tool_security_tests.py
	"$(COVERAGE_PYTHON)" -m coverage run --parallel-mode -m unittest discover -s scripts/ci -p 'test_*.py'
	"$(COVERAGE_PYTHON)" -m coverage combine --append
	"$(COVERAGE_PYTHON)" -m coverage json
	"$(COVERAGE_PYTHON)" -m coverage html

project-coverage:
	$(MAKE) python-coverage
	$(MAKE) coverage NATIVE_COVERAGE_RUN='"$(COVERAGE_PYTHON)" -m coverage run --parallel-mode'
	"$(COVERAGE_PYTHON)" -m coverage run --parallel-mode scripts/ci/project_coverage.py --report-only
	"$(COVERAGE_PYTHON)" -m coverage combine --append
	"$(COVERAGE_PYTHON)" -m coverage json
	"$(COVERAGE_PYTHON)" -m coverage html
	"$(COVERAGE_PYTHON)" scripts/ci/project_coverage.py

app:
	@test -n "$(APP_BUILD_DIR)" || { printf '%s\n' 'Set APP_BUILD_DIR to an existing game build; see docs/BUILDING.md.' >&2; exit 1; }
	$(CMAKE) --build "$(APP_BUILD_DIR)" --target Yakumo --parallel $(JOBS)
