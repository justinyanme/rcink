"""Tiny fixture page used by BLE transport tests.

Self-contained so the transport tests don't depend on the live page registry.
"""

from rcink.layout import hbar, spacer, sparkline, text, vstack


def example_page() -> dict:
    return vstack(
        pad=4,
        gap=3,
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
            hbar(pct=42, color="black", height=14),
            sparkline([12, 18, 14, 22, 19, 28, 31], color="red", height=32),
            spacer(),
            text("updated 14:32", size="sm", align="center"),
        ],
    )
