PYTHON ?= python3

.PHONY: all clean test test-games

all: test

test:
	PYTHONPATH=src $(PYTHON) -m unittest discover -s tests -v
	PYTHONPATH=src $(PYTHON) -m kilix_game_tools --help >/dev/null

test-games:
	@printf '%s\n' "Run consumer validation from each pinned game checkout."

clean:
	rm -rf build dist .pytest_cache src/*.egg-info src/*/*.egg-info
	find . -type d -name __pycache__ -prune -exec rm -rf {} +
