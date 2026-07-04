"""Open-Meteo weather source.

Configuration:
- RCINK_LAT / RCINK_LON: WGS84 coordinates.
- RCINK_WEATHER_TEMP_UNIT: celsius (default) or fahrenheit.
"""

from __future__ import annotations

import datetime as dt
import os
from dataclasses import dataclass
from typing import Any

import httpx

from rcink.envutil import require_env

FORECAST_URL = "https://api.open-meteo.com/v1/forecast"
TEMP_UNITS = {"celsius": "C", "fahrenheit": "F"}


@dataclass(slots=True)
class Weather:
    temp: float
    high: float
    low: float
    unit: str
    code: int
    precip_probability: int | None
    is_day: bool
    observed_at: dt.datetime


def _env_float(key: str) -> float:
    raw = require_env(key)
    try:
        return float(raw)
    except ValueError as exc:
        raise ValueError(f"{key} must be a floating-point coordinate, got {raw!r}") from exc


def _temp_unit() -> tuple[str, str]:
    unit = os.environ.get("RCINK_WEATHER_TEMP_UNIT", "celsius").strip().lower()
    if unit not in TEMP_UNITS:
        raise ValueError(f"RCINK_WEATHER_TEMP_UNIT must be one of {sorted(TEMP_UNITS)}, got {unit!r}")
    return unit, TEMP_UNITS[unit]


def _number(obj: dict[str, Any], key: str) -> float:
    value = obj.get(key)
    if isinstance(value, bool) or not isinstance(value, int | float):
        raise ValueError(f"weather payload missing numeric {key!r}")
    return float(value)


def _int(obj: dict[str, Any], key: str) -> int:
    value = obj.get(key)
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"weather payload missing integer {key!r}")
    return value


def _daily_number(daily: dict[str, Any], key: str) -> float:
    values = daily.get(key)
    if not isinstance(values, list) or not values:
        raise ValueError(f"weather payload missing daily array {key!r}")
    value = values[0]
    if isinstance(value, bool) or not isinstance(value, int | float):
        raise ValueError(f"weather daily {key!r}[0] must be numeric")
    return float(value)


def _daily_int(daily: dict[str, Any], key: str) -> int | None:
    values = daily.get(key)
    if values is None:
        return None
    if not isinstance(values, list) or not values:
        raise ValueError(f"weather payload malformed daily array {key!r}")
    value = values[0]
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"weather daily {key!r}[0] must be integer")
    return value


def _observed_at(current: dict[str, Any], payload: dict[str, Any]) -> dt.datetime:
    raw = current.get("time")
    if not isinstance(raw, str) or not raw:
        raise ValueError("weather payload missing current time")
    offset = payload.get("utc_offset_seconds", 0)
    if isinstance(offset, bool) or not isinstance(offset, int):
        raise ValueError("weather payload utc_offset_seconds must be integer")
    parsed = dt.datetime.fromisoformat(raw)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=dt.timezone(dt.timedelta(seconds=offset)))
    return parsed.astimezone()


def fetch_weather() -> Weather:
    lat = _env_float("RCINK_LAT")
    lon = _env_float("RCINK_LON")
    temp_unit, unit_symbol = _temp_unit()
    response = httpx.get(
        FORECAST_URL,
        params={
            "latitude": lat,
            "longitude": lon,
            "current": "temperature_2m,weather_code,is_day",
            "daily": "temperature_2m_max,temperature_2m_min,weather_code,precipitation_probability_max",
            "timezone": "auto",
            "forecast_days": 1,
            "temperature_unit": temp_unit,
        },
        timeout=20,
    )
    if response.status_code != 200:
        raise RuntimeError(f"Open-Meteo HTTP {response.status_code}: {response.text}")
    payload = response.json()
    current = payload.get("current")
    daily = payload.get("daily")
    if not isinstance(current, dict) or not isinstance(daily, dict):
        raise ValueError("weather payload missing current/daily objects")

    return Weather(
        temp=_number(current, "temperature_2m"),
        high=_daily_number(daily, "temperature_2m_max"),
        low=_daily_number(daily, "temperature_2m_min"),
        unit=unit_symbol,
        code=_int(current, "weather_code"),
        precip_probability=_daily_int(daily, "precipitation_probability_max"),
        is_day=bool(_int(current, "is_day")),
        observed_at=_observed_at(current, payload),
    )
