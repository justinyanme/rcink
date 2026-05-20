.PHONY: install test lint fmt smoke

export UV_CACHE_DIR := .uv-cache

install:
	uv sync --all-extras

test:
	uv run pytest

lint:
	uv run ruff check .
	uv run ruff format --check .

fmt:
	uv run ruff format .
	uv run ruff check --fix .

smoke:
	uv run tlsr-layout dry-run example --out /tmp/tlsr-layout-smoke
