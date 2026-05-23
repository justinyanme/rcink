from conftest import ROOT

SRC = ROOT.parent / "src"


def test_default_scene_is_static_image():
    text = (SRC / "epd" / "scene.h").read_text()
    assert "EPD_SCENE_DEFAULT = EPD_SCENE_IMAGE" in text


def test_legacy_scenes_are_opt_in():
    text = (SRC / "CMakeLists.txt").read_text()
    assert 'option(ENABLE_EPD_SCENE_TIME "Include the legacy clock scene." OFF)' in text
    assert 'option(ENABLE_EPD_SCENE_LIST "Include the legacy text-list scene." OFF)' in text
