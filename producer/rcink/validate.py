"""Producer-side DSL validation."""

from __future__ import annotations

import json
from functools import cache
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator
from jsonschema.exceptions import best_match

SCHEMA_PATH = Path(__file__).resolve().parent.parent.parent / "dsl" / "schema.json"


class LayoutValidationError(ValueError):
    """Layout does not conform to the rcink DSL schema."""


@cache
def _validator() -> Draft202012Validator:
    schema = json.loads(SCHEMA_PATH.read_text())
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema)


def validate_page(layout: dict[str, Any]) -> None:
    error = best_match(_validator().iter_errors(layout))
    if error is None:
        return
    path = "".join(f"[{part!r}]" if isinstance(part, int) else f".{part}" for part in error.absolute_path)
    if path.startswith("."):
        path = path[1:]
    location = path or "<root>"
    raise LayoutValidationError(f"invalid rcink layout at {location}: {error.message}")
