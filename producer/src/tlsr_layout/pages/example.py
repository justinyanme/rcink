"""Demo page used in tests and the dry-run CLI."""

from ..layout import hbar, spacer, sparkline, text, vstack


def example_page() -> dict:
    """A small dashboard page that exercises all common primitives."""

    return vstack(
        pad=14,
        gap=6,
        children=[
            text(
                "CLAUDE",
                size="h1",
                color="white",
                bg="red",
                pad=8,
                align="center",
                weight="bold",
            ),
            hbar(pct=42, color="blue", height=14),
            sparkline([12, 18, 14, 22, 19, 28, 31], color="blue", height=40),
            spacer(),
            text("updated 14:32", size="sm", align="center"),
        ],
    )
