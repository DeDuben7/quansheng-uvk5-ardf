import pytest

from sf import ui


@pytest.mark.parametrize("iso, expected", [
    ("2026-10-08", "8 oktober 2026"),
    ("2027-01-31", "31 januari 2027"),
    ("2026-03-05", "5 maart 2026"),
    ("2026-12-25", "25 december 2026"),
])
def test_dutch_date(iso, expected):
    assert ui.dutch_date(iso) == expected


@pytest.mark.parametrize("junk", ["", "gisteren", "2026-13-01", "2026-10-08T12:00", None])
def test_dutch_date_shows_unparseable_text_as_is(junk):
    assert ui.dutch_date(junk) == str(junk)


@pytest.mark.parametrize("release, expected", [
    ("1.1", "1.1"),
    ("1.1-rc1", "1.1"),
    ("1.2.3-beta.2", "1.2.3"),
    ("1.1.1", "1.1.1"),
])
def test_base_version_is_what_the_radio_shows_on_its_welcome_screen(release, expected):
    assert ui.base_version(release) == expected


@pytest.mark.parametrize("scale", [1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0])
def test_porto_picture_is_about_320px_at_96_dpi_scaled_with_the_screen(scale):
    zoom, sub = ui.photo_factors(scale)
    assert zoom >= 1 and sub >= 1
    height = 645 * zoom / sub
    wanted = 322 * scale
    assert abs(height - wanted) <= 0.08 * wanted     # a whole-number zoom can't be exact


def test_every_fetch_failure_kind_has_a_dutch_text():
    from sf import update
    assert set(ui.FETCH_PROBLEMS) == set(update.KINDS)


@pytest.mark.parametrize("note, button", [
    (ui.TOO_OLD_NOTE, "Nieuwe versie downloaden"),
    (ui.NEWER_EXE_NOTE, "Downloaden"),
])
def test_download_notes_say_close_this_program_before_starting_the_new_one(note, button):
    # The new EXE refuses to start while this one is open ("Het programma is al geopend").
    assert "‘{}’".format(button) in note
    assert note.index("sluit dit programma") < note.index("start daarna het nieuwe programma")
