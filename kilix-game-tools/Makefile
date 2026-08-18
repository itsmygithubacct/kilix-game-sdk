PYTHON ?= python3
COVERAGE_MIN ?= 90

.PHONY: all benchmark clean compile coverage release-gate test test-games test-install

all: test

compile:
	PYTHONPATH=src $(PYTHON) -m compileall -q src tests benchmarks

test: compile
	PYTHONPATH=src $(PYTHON) -m unittest discover -s tests -v
	PYTHONPATH=src $(PYTHON) -m kilix_game_tools --help >/dev/null
	PYTHONPATH=src $(PYTHON) -m kilix_game_tools --version | \
		grep -Fx 'kilix-game-tools 0.3.0'

coverage:
	$(PYTHON) -m coverage erase
	PYTHONPATH=src $(PYTHON) -m coverage run --branch \
		--source=kilix_game_tools -m unittest discover -s tests -v
	$(PYTHON) -m coverage report --show-missing --fail-under=$(COVERAGE_MIN)

test-install:
	@set -eu; staging=$$(mktemp -d); \
	trap 'rm -rf "$$staging"' EXIT HUP INT TERM; \
	mkdir -p "$$staging/project" "$$staging/wheel" "$$staging/site"; \
	cp pyproject.toml README.md LICENSE "$$staging/project/"; \
	cp -R src "$$staging/project/"; \
	$(PYTHON) -m pip wheel --no-deps --no-build-isolation --no-index \
		--wheel-dir "$$staging/wheel" "$$staging/project"; \
	wheel=$$(find "$$staging/wheel" -maxdepth 1 -type f -name '*.whl'); \
	test -n "$$wheel"; \
	$(PYTHON) -m pip install --no-deps --no-index --target "$$staging/site" \
		"$$wheel"; \
	PYTHONPATH="$$staging/site" $(PYTHON) -c \
		'import kilix_game_tools; assert kilix_game_tools.__version__ == "0.3.0"'; \
	PYTHONPATH="$$staging/site" $(PYTHON) -m kilix_game_tools --version | \
		grep -Fx 'kilix-game-tools 0.3.0'; \
	PYTHONPATH="$$staging/site" "$$staging/site/bin/kilix-game-tools" \
		--help >/dev/null

benchmark:
	PYTHONPATH=src $(PYTHON) benchmarks/benchmark_tools.py tar --size-mib 32
	PYTHONPATH=src $(PYTHON) benchmarks/benchmark_tools.py zip --size-mib 32
	PYTHONPATH=src $(PYTHON) benchmarks/benchmark_tools.py ppm \
		--width 4096 --height 2048 --rounds 10

release-gate: test coverage test-install benchmark

test-games:
	@printf '%s\n' "Run consumer validation from each pinned game checkout."

clean:
	rm -rf build dist .pytest_cache .coverage .coverage.* \
		src/*.egg-info src/*/*.egg-info
	find . -type d -name __pycache__ -prune -exec rm -rf {} +
