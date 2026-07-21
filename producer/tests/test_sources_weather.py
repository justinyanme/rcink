import datetime as dt

import pytest

from rcink.sources.weather import WeatherConfigError, fetch_weather


def _weather_payload():
    return {
        "utc_offset_seconds": 28800,
        "current": {
            "time": "2026-07-04T09:15",
            "interval": 900,
            "temperature_2m": 28.4,
            "weather_code": 80,
            "is_day": 1,
        },
        "daily": {
            "time": ["2026-07-04"],
            "temperature_2m_max": [31.2],
            "temperature_2m_min": [24.6],
            "weather_code": [80],
            "precipitation_probability_max": [67],
        },
    }


def test_fetch_weather_parses_open_meteo_response(monkeypatch, httpx_mock):
    monkeypatch.setenv("RCINK_LAT", "22.3")
    monkeypatch.setenv("RCINK_LON", "114.2")
    monkeypatch.setenv("RCINK_WEATHER_LABEL", " Hong   Kong ")
    httpx_mock.add_response(method="GET", json=_weather_payload())

    weather = fetch_weather()

    assert weather.location == "HONG KONG"
    assert weather.temp == 28.4
    assert weather.high == 31.2
    assert weather.low == 24.6
    assert weather.unit == "C"
    assert weather.code == 80
    assert weather.precip_probability == 67
    assert weather.is_day is True
    assert weather.observed_at == dt.datetime(2026, 7, 4, 9, 15, tzinfo=dt.timezone(dt.timedelta(hours=8)))

    req = httpx_mock.get_requests()[0]
    assert req.url.params["latitude"] == "22.3"
    assert req.url.params["longitude"] == "114.2"
    assert req.url.params["current"] == "temperature_2m,weather_code,is_day"
    assert req.url.params["forecast_days"] == "1"


def test_fetch_weather_honors_temperature_unit(monkeypatch, httpx_mock):
    monkeypatch.setenv("RCINK_LAT", "22.3")
    monkeypatch.setenv("RCINK_LON", "114.2")
    monkeypatch.setenv("RCINK_WEATHER_LABEL", "HONG KONG")
    monkeypatch.setenv("RCINK_WEATHER_TEMP_UNIT", "fahrenheit")
    payload = _weather_payload()
    payload["current"]["temperature_2m"] = 82.0
    payload["daily"]["temperature_2m_max"] = [88.0]
    payload["daily"]["temperature_2m_min"] = [75.0]
    httpx_mock.add_response(method="GET", json=payload)

    weather = fetch_weather()

    assert weather.unit == "F"
    assert weather.temp == 82.0
    assert httpx_mock.get_requests()[0].url.params["temperature_unit"] == "fahrenheit"


def test_fetch_weather_requires_coordinates(monkeypatch):
    monkeypatch.delenv("RCINK_LAT", raising=False)
    monkeypatch.setenv("RCINK_LON", "114.2")
    monkeypatch.setenv("RCINK_WEATHER_LABEL", "HONG KONG")

    with pytest.raises(WeatherConfigError, match="RCINK_LAT"):
        fetch_weather()


def test_fetch_weather_requires_location_label(monkeypatch):
    monkeypatch.setenv("RCINK_LAT", "22.3")
    monkeypatch.setenv("RCINK_LON", "114.2")
    monkeypatch.delenv("RCINK_WEATHER_LABEL", raising=False)

    with pytest.raises(WeatherConfigError, match="RCINK_WEATHER_LABEL"):
        fetch_weather()


def test_fetch_weather_rejects_malformed_payload(monkeypatch, httpx_mock):
    monkeypatch.setenv("RCINK_LAT", "22.3")
    monkeypatch.setenv("RCINK_LON", "114.2")
    monkeypatch.setenv("RCINK_WEATHER_LABEL", "HONG KONG")
    payload = _weather_payload()
    payload["current"]["temperature_2m"] = None
    httpx_mock.add_response(method="GET", json=payload)

    with pytest.raises(ValueError, match="temperature_2m"):
        fetch_weather()
