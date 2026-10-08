"""The wizard the user sees (Dutch). One screen at a time, one main button.

Welkom -> Kabel -> Porto -> Bezig -> Klaar, plus Fout from any step.
The welcome screen first fetches the latest release (backend.prepare); Start
stays disabled until that worked. Backend calls block, so they run on a
worker thread; results come back to the Tk thread through root.after().
"""

import math
import os
import shutil
import tempfile
import threading
import tkinter as tk
import traceback
import webbrowser
from datetime import datetime
from pathlib import Path
from tkinter import messagebox, ttk

from sf import FLASHER_VERSION, buildinfo, driver, paths, update

ACCENT = "#1f4e5f"
ORANGE = "#e8730c"
BG = "#ffffff"
MUTED = "#5b676d"
OK_GREEN = "#2e7d32"

HELP_AFTER_S = 15          # "is the white LED on?" hint on the Porto screen
CABLE_POLL_MS = 2000
MAX_FIX_ATTEMPTS = 2       # then stop offering "Herstellen" (no endless UAC loop)

RADIO_NAMES = {"V1": "versie 1 (oud type)", "V3": "versie 3"}

TITLE = "Scout porto bijwerken"
MONTHS_NL = ("januari", "februari", "maart", "april", "mei", "juni", "juli", "augustus",
             "september", "oktober", "november", "december")

# Welcome screen, when the latest release could not be fetched (UpdateError.kind).
FETCH_PROBLEMS = {
    "offline": "Geen verbinding met internet. Verbind de computer met internet en klik op Opnieuw.",
    "too_old": "Deze versie van het programma is te oud voor de nieuwste software.",
    "bad_info": "De software kon niet goed worden opgehaald.",
    "bad_download": "De software kon niet goed worden opgehaald.",
}

# What to do next. The new program refuses to start while this one is still open
# (scout_flasher.already_running), so both texts say: close this one first.
TOO_OLD_NOTE = ("Klik op ‘Nieuwe versie downloaden’, sluit dit programma en start daarna het nieuwe "
                "programma.")
NEWER_EXE_NOTE = ("Er is een nieuwe versie van dit programma. Klik op ‘Downloaden’, sluit dit programma "
                  "en start daarna het nieuwe programma.")


def dutch_date(iso: str) -> str:
    """"2026-10-08" -> "8 oktober 2026". Anything that isn't a date is shown as it is."""
    try:
        d = datetime.strptime(iso, "%Y-%m-%d")
    except (TypeError, ValueError):
        return str(iso)
    return "{} {} {}".format(d.day, MONTHS_NL[d.month - 1], d.year)


def base_version(release: str) -> str:
    """"1.1-rc1" -> "1.1". The radio's welcome screen shows SCOUT_VERSION, which
    never has a pre-release suffix."""
    return str(release).split("-")[0]


def photo_factors(scale: float) -> tuple[int, int]:
    """(zoom, subsample) that show the 310 x 645 picture of the porto about 320 px
    tall at 96 DPI, growing with the screen scale. Tk resizes images by whole
    numbers only, so the picture is zoomed by n and shrunk by 8 (a rounding to
    quarters of the half-size would make it 20% too tall at 125% and push the
    keypad behind the buttons)."""
    z = max(1, round(4 * scale))
    g = math.gcd(z, 8)
    return z // g, 8 // g


class App:

    def __init__(self, root: tk.Tk, backend, log, help_after_s=HELP_AFTER_S):
        self.root = root
        self.backend = backend
        self.log = log
        self.help_after_s = help_after_s
        self.package = None        # update.Package of the latest release, once fetched
        self.port = None
        self.beacon = None
        self.busy_flashing = False
        self.cancel = threading.Event()
        self.retry_step = self.show_cable
        self.fix_attempts = 0
        self._photo = None
        self._worker = None

        # Pixel sizes below are for 96 DPI; scale them like the fonts (Tk
        # scales point sizes itself once the process is DPI aware).
        self.scale = max(1.0, root.winfo_fpixels("1i") / 96.0)
        root.title(TITLE)
        root.geometry("{}x{}".format(self.px(700), self.px(560)))
        root.minsize(self.px(640), self.px(520))
        root.configure(bg=BG)
        try:   # the fox in the title bar and on the taskbar
            root.iconbitmap(default=str(paths.icon_ico()))
        except tk.TclError:
            pass
        root.protocol("WM_DELETE_WINDOW", self.on_close)

        style = ttk.Style(root)
        style.theme_use("vista" if "vista" in style.theme_names() else style.theme_use())
        style.configure("Big.TButton", font=("Segoe UI", 13, "bold"), padding=(self.px(22), self.px(10)))
        style.configure("TButton", font=("Segoe UI", 11), padding=(self.px(14), self.px(7)))
        style.configure("Scout.Horizontal.TProgressbar", thickness=self.px(26))

        self.frame = None
        self.show_welcome()

    # --- helpers -------------------------------------------------------------
    def px(self, n: int) -> int:
        return int(round(n * self.scale))

    def new_screen(self, title: str):
        """Title + rule on top; footer and button bar reserved at the bottom
        first, so tall content (the photo) can't squeeze the buttons."""
        if self.frame is not None:
            self.frame.destroy()
        self.frame = tk.Frame(self.root, bg=BG, padx=self.px(36), pady=self.px(28))
        self.frame.pack(fill="both", expand=True)
        tk.Label(self.frame, text=title, font=("Segoe UI", 21, "bold"), fg=ACCENT, bg=BG,
                 anchor="w").pack(fill="x")
        tk.Frame(self.frame, height=self.px(3), bg=ACCENT).pack(fill="x", pady=(self.px(6), self.px(18)))
        tk.Label(self.frame, text="{}  ·  Logbestand: {}".format(buildinfo.label(), self.log.path.name),
                 font=("Segoe UI", 9), fg=MUTED, bg=BG, anchor="w").pack(side="bottom", fill="x")
        self._bar = tk.Frame(self.frame, bg=BG)
        self._bar.pack(side="bottom", fill="x", pady=(self.px(16), self.px(6)))
        return self.frame

    def text(self, parent, s, size=13, color="#1d2529", bold=False, wrap=620, pady=(0, 10)):
        lbl = tk.Label(parent, text=s, font=("Segoe UI", size, "bold" if bold else "normal"),
                       fg=color, bg=BG, justify="left", anchor="w", wraplength=self.px(wrap))
        lbl.pack(fill="x", pady=(self.px(pady[0]), self.px(pady[1])))
        return lbl

    def buttons(self, *specs):
        """specs: (label, command, primary), right to left. Buttons added
        later go to the far right (the next step is always there)."""
        made = []
        for label, cmd, primary in reversed(specs):
            b = ttk.Button(self._bar, text=label, command=cmd,
                           style="Big.TButton" if primary else "TButton")
            existing = [w for w in self._bar.pack_slaves()]
            if existing:
                b.pack(side="right", padx=(self.px(10), 0), before=existing[0])
            else:
                b.pack(side="right", padx=(self.px(10), 0))
            made.append(b)
        return list(reversed(made))

    def run_bg(self, fn, done, *args):
        """Run fn(*args) on a worker thread; done(result) or self.fail(...)."""
        def work():
            try:
                result = fn(*args)
            except Exception as e:  # noqa: BLE001
                self.log.write("ERROR: {!r}\n{}".format(e, traceback.format_exc()))
                self.root.after(0, lambda: self.fail(
                    "Er ging iets mis dat ik niet verwachtte ({}).".format(type(e).__name__)))
                return
            self.root.after(0, lambda: done(result))
        t = threading.Thread(target=work, daemon=True)
        t.start()
        return t

    def open_manual(self):
        """Open the release's PDF in the default viewer (Edge). A copy is
        opened: the download folder is deleted when the program closes. Before
        the release is fetched there is no PDF yet: show the release page."""
        pkg = self.package
        if pkg is None:
            self.log.write("Manual: no package yet, opening " + update.RELEASES_PAGE)
            webbrowser.open(update.RELEASES_PAGE)
            return
        try:
            m = pkg.manifest
            # One folder per PDF (named by its hash): a changed manual never shows a stale copy.
            target = (Path(tempfile.gettempdir()) / "ScoutFlasher"
                      / m.sha256.get(m.manual, pkg.info.release)[:12] / m.manual)
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.exists():
                shutil.copyfile(pkg.dir / m.manual, target)
            os.startfile(target)
            self.log.write("Opened manual " + str(target))
        except OSError as e:
            messagebox.showerror("Handleiding", "De handleiding kon niet worden geopend:\n{}".format(e))

    # --- screens ---------------------------------------------------------------
    def show_welcome(self):
        f = self.new_screen(TITLE)
        self._welcome_screen = f
        self.welcome_status = self.text(f, "", size=15, bold=True)
        self.text(f, "Het werkt voor beide porto’s: versie 1 en versie 3. U hebt de programmeerkabel "
                     "nodig. Het programma zoekt alles zelf uit en vertelt u wat u moet doen.")
        self.text(f, "Doe één porto tegelijk. Aan het eind kunt u meteen de volgende doen.", color=MUTED)
        self.welcome_note = self.text(f, "", color=MUTED)
        self.start_prepare()

    def welcome_buttons(self, *specs, start_enabled=False):
        """Rebuild the button bar. specs as in buttons(); the one labelled
        "Start" is disabled unless start_enabled."""
        for w in self._bar.winfo_children():
            w.destroy()
        for spec, b in zip(specs, self.buttons(*specs)):
            if spec[0] == "Start" and not start_enabled:
                b.state(["disabled"])

    def start_prepare(self):
        """(Re)start fetching the latest release; Opnieuw comes back here."""
        screen = self._welcome_screen
        self.package = None
        self.root.title(TITLE)
        self.welcome_status.configure(text="Nieuwste software ophalen…", fg=ACCENT)
        self.welcome_note.configure(text="")
        self.welcome_buttons(("Start", self.show_cable, True), ("Handleiding", self.open_manual, False))
        self.log.section("Fetching the latest release")

        def work():
            pkg, kind, message = None, None, ""
            try:
                pkg = self.backend.prepare()
            except update.UpdateError as e:
                kind, message = e.kind, e.message
            except Exception as e:  # noqa: BLE001 - a surprise must not crash the wizard
                self.log.write("ERROR: {!r}\n{}".format(e, traceback.format_exc()))
                kind, message = "bad_download", repr(e)
            try:
                self.root.after(0, lambda: self.on_prepared(screen, pkg, kind, message))
            except (RuntimeError, tk.TclError):
                pass            # the window was closed meanwhile
        threading.Thread(target=work, daemon=True).start()

    def on_prepared(self, screen, pkg, kind, message):
        if self.frame is not screen:
            return
        manual = ("Handleiding", self.open_manual, False)
        if pkg is None:
            text = FETCH_PROBLEMS.get(kind, FETCH_PROBLEMS["bad_download"])
            self.log.write("Shown to user: {} ({}: {})".format(text, kind, message))
            self.welcome_status.configure(text=text, fg=ORANGE)
            if kind == "too_old":
                self.welcome_note.configure(text=TOO_OLD_NOTE, fg="#1d2529")
                self.welcome_buttons(("Nieuwe versie downloaden", self.download_exe, True),
                                     ("Start", self.show_cable, False), manual)
            else:
                self.welcome_buttons(("Opnieuw", self.start_prepare, True),
                                     ("Start", self.show_cable, False), manual)
            return
        self.package = pkg
        info = pkg.info
        self.root.title("{} – versie {}".format(TITLE, info.release))
        self.welcome_status.configure(
            text="Software versie {} (nieuwste, {}) is klaar om op uw porto te zetten.".format(
                info.release, dutch_date(info.date)), fg="#1d2529")
        newer = update.newer_exe_available(info, FLASHER_VERSION)
        self.log.write("Ready: release {} (newer program available: {})".format(
            info.release, "yes" if newer else "no"))
        specs = [("Start", self.show_cable, True), manual]
        if newer:
            self.welcome_note.configure(text=NEWER_EXE_NOTE, fg=MUTED)
            specs.insert(1, ("Downloaden", self.download_exe, False))
        self.welcome_buttons(*specs, start_enabled=True)

    def download_exe(self):
        url = update.exe_url()
        self.log.write("Opening " + url)
        webbrowser.open(url)

    def show_cable(self):
        self.retry_step = self.show_cable
        f = self.new_screen("Stap 1 – De kabel")
        self.text(f, "Steek de programmeerkabel in een USB-poort van de computer.", size=15, bold=True)
        self.text(f, "De porto nog niet aansluiten.", color=MUTED)
        self.status = self.text(f, "Zoeken naar de kabel…", size=14, color=ACCENT, pady=(24, 10))
        self.fix_button = None
        self.buttons(("Handleiding", self.open_manual, False))
        self._cable_screen = f
        self.poll_cable()

    def poll_cable(self):
        if self.frame is not self._cable_screen:
            return
        self.run_bg(self.backend.check_cable, self.on_cable)

    def on_cable(self, res):
        if self.frame is not self._cable_screen:
            return
        if res.state == "ok":
            self.port = res.port
            self.status.configure(text="✓  Kabel gevonden ({})".format(res.port), fg=OK_GREEN)
            self.log.write("Cable OK on {}".format(res.port))
            self.root.after(1200, self.show_radio)
            return
        if res.state == "needs_fix":
            if self.fix_attempts >= MAX_FIX_ATTEMPTS:
                self.fail("Het stuurprogramma van de kabel blijft verkeerd, ook na herstellen. Steek de "
                          "kabel in een andere USB-poort en start de computer opnieuw op. Probeer het "
                          "daarna nog eens.")
                return
            self.status.configure(
                text="Het stuurprogramma van de kabel moet worden hersteld.\n"
                     "Klik op ‘Herstellen’. Windows vraagt dan om toestemming: klik op Ja.",
                fg=ORANGE)
            if self.fix_button is None:
                self.fix_button = self.buttons(("Herstellen", self.fix_driver, True))[0]
            return
        if res.state == "driver_problem":
            self.log.write("Driver right but problem code {} (Memory Integrity: {})".format(
                res.problem, res.memory_integrity))
            if res.memory_integrity and res.problem not in (10, 14, 22, 45):
                # Most likely blocked by Memory Integrity: nothing the user can fix here.
                self.fail("Windows blokkeert het stuurprogramma van deze kabel (code {}). Dit ligt aan "
                          "een beveiligingsinstelling van Windows. Stuur het logbestand naar degene die "
                          "u met de porto helpt; die kan een andere kabel regelen."
                          .format(res.problem), can_retry=False)
                return
            self.status.configure(
                text="Windows kan de kabel nog niet gebruiken (code {}). Haal de kabel uit de computer "
                     "en steek hem er weer in. Helpt dat niet: start de computer opnieuw op."
                .format(res.problem), fg=ORANGE)
        elif res.state == "not_found":
            self.status.configure(text="Nog geen kabel gevonden. Steek hem in, of probeer een andere "
                                       "USB-poort.", fg=ACCENT)
        else:
            self.status.configure(text="Windows meldt een probleem met de kabel (code {}). Haal de "
                                       "kabel uit de computer en steek hem er weer in."
                                  .format(res.problem), fg=ORANGE)
        self.root.after(CABLE_POLL_MS, self.poll_cable)

    def fix_driver(self):
        self.fix_attempts += 1
        self.fix_button.state(["disabled"])
        self.status.configure(text="Bezig met herstellen… Klik op Ja als Windows om toestemming vraagt.",
                              fg=ACCENT)
        self.run_bg(self.backend.fix_driver, self.on_fixed)

    def on_fixed(self, code):
        if self.frame is not self._cable_screen:
            return
        if code == driver.FIX_CANCELLED:
            self.status.configure(text="U klikte op Nee. Klik nog eens op ‘Herstellen’ en dan op Ja.",
                                  fg=ORANGE)
            self.fix_attempts -= 1        # declining doesn't use up an attempt
            self.fix_button.state(["!disabled"])
            return
        if code == driver.FIX_REBOOT_NEEDED:
            self.fail("Het stuurprogramma is hersteld, maar Windows wil eerst opnieuw opstarten. Start "
                      "de computer opnieuw op en start dit programma daarna weer.", can_retry=False)
            return
        if code != driver.FIX_OK:
            self.fail("Het stuurprogramma kon niet worden hersteld (code {}). Hebt u op Ja geklikt toen "
                      "Windows om toestemming vroeg?".format(code))
            return
        self.status.configure(text="Hersteld. Haal de kabel even uit de computer en steek hem er weer "
                                   "in…", fg=ACCENT)
        self.fix_button.destroy()
        self.fix_button = None
        self.root.after(CABLE_POLL_MS, self.poll_cable)

    def show_radio(self):
        # A retry starts again at the cable: the port may have changed.
        self.retry_step = self.show_cable
        self.beacon = None
        f = self.new_screen("Stap 2 – De porto")
        body = tk.Frame(f, bg=BG)
        body.pack(fill="both", expand=True)
        try:
            photo = tk.PhotoImage(file=str(paths.image("porto-front.png")))   # 310 x 645
            zoom, sub = photo_factors(self.scale)
            self._photo = photo.zoom(zoom, zoom).subsample(sub, sub)
            tk.Label(body, image=self._photo, bg=BG).pack(side="left", anchor="n", padx=(0, self.px(24)))
        except tk.TclError:
            pass
        right = tk.Frame(body, bg=BG)
        right.pack(side="left", fill="both", expand=True)
        steps = [
            "Zet de porto uit.",
            "Steek de kabel in de porto (zijkant rechts, onder het klepje). Druk hem stevig aan.",
            "Houd de PTT (de grote knop links) ingedrukt en zet de porto aan.",
            "Laat de PTT los als het lampje bovenop wit brandt. Het scherm blijft leeg.",
        ]
        for i, s in enumerate(steps, 1):
            self.text(right, "{}.  {}".format(i, s), wrap=420, pady=(0, 8))
        self.status = self.text(right, "Wachten op de porto…", size=14, color=ACCENT, wrap=420,
                                pady=(14, 6))
        self.hint = self.text(right, "", size=11, color=MUTED, wrap=420)
        self.start_button, _ = self.buttons(("Start bijwerken", self.start_flash, True),
                                            ("Handleiding", self.open_manual, False))
        self.start_button.state(["disabled"])
        self._radio_screen = f
        self.cancel = threading.Event()
        self.run_bg(self.backend.wait_radio, self.on_radio, self.port, self.cancel)
        self.root.after(self.help_after_s * 1000, lambda: self.radio_hint(f))

    def radio_hint(self, screen):
        if self.frame is screen and self.beacon is None:
            self.hint.configure(text="Ziet u het witte lampje niet? Zet de porto uit en doe stap 3 "
                                     "opnieuw. Zit de stekker er helemaal in?")

    def on_radio(self, beacon):
        if self.frame is not self._radio_screen or beacon is None:
            return
        if beacon.kind is None:
            self.fail("Deze porto herken ik niet (opstartprogramma {}). Er is niets veranderd aan de "
                      "porto.".format(beacon.version), can_retry=False)
            return
        self.beacon = beacon
        self.status.configure(text="✓  Porto gevonden: {}".format(RADIO_NAMES[beacon.kind]), fg=OK_GREEN)
        self.hint.configure(text="Klik op ‘Start bijwerken’.")
        self.start_button.state(["!disabled"])

    def start_flash(self):
        self.cancel.set()                 # stop listening (port must be free)
        self.retry_step = self.show_cable
        f = self.new_screen("Bezig met bijwerken…")
        self.text(f, "Haal de kabel er niet uit en zet de porto niet uit.", size=15, bold=True)
        self.text(f, "Dit duurt tien seconden tot een halve minuut.", color=MUTED)
        self.bar = ttk.Progressbar(f, style="Scout.Horizontal.TProgressbar", mode="determinate",
                                   maximum=100)
        self.bar.pack(fill="x", pady=(self.px(30), self.px(8)))
        self.pct = self.text(f, "0%", size=13, color=ACCENT)
        self.busy_flashing = True
        flash_cancel = threading.Event()
        self.flash_cancel = flash_cancel
        self.log.section("Flash {} on {}".format(self.beacon.kind, self.port))
        # Give the listening thread a moment to release the port.
        self.root.after(400, lambda: setattr(self, "_worker", self.run_bg(
            self.backend.flash, self.on_flashed, self.port, self.beacon, self.on_progress, flash_cancel)))

    def on_progress(self, done, total):
        pct = int(100 * done / total) if total else 0
        self.root.after(0, lambda: self._set_progress(pct))

    def _set_progress(self, pct):
        if getattr(self, "bar", None) is not None and self.busy_flashing:
            self.bar["value"] = pct
            self.pct.configure(text="{}%".format(pct))

    def on_flashed(self, ok):
        self.busy_flashing = False
        self.log.write("Flash result: {}".format("OK" if ok else "FAILED"))
        if not ok:
            self.fail("Het bijwerken is niet gelukt. Zet de porto uit en weer in de bijwerk-stand "
                      "(PTT ingedrukt houden en aanzetten), en klik op Opnieuw.")
            return
        f = self.new_screen("Klaar!")
        release = self.package.info.release
        self.text(f, "✓  De porto heeft nu software versie {}.".format(release), size=16,
                  bold=True, color=OK_GREEN)
        self.text(f, "Haal de kabel uit de porto. Zet de porto uit en weer aan: op het welkomstscherm "
                     "staat nu v{}.".format(base_version(release)))
        self.text(f, "Nog een porto bijwerken? Klik op ‘Nog een porto’.", color=MUTED)
        self.buttons(("Nog een porto", self.show_cable, True), ("Afsluiten", self.root.destroy, False))

    def fail(self, message, can_retry=True):
        self.busy_flashing = False
        self.cancel.set()
        self.log.write("Shown to user: " + message)
        f = self.new_screen("Dat ging niet goed")
        self.text(f, message, size=14)
        if can_retry:
            self.text(f, "Er is niets kapot: u kunt het gewoon opnieuw proberen.", bold=True)
        self.text(f, "Lukt het steeds niet? Op uw bureaublad staat het logbestand {}. Stuur het "
                     "logbestand naar degene die u met de porto helpt.".format(self.log.path.name),
                  color=MUTED, pady=(14, 10))
        specs = [("Opnieuw", self.retry_step, True)] if can_retry else []
        specs.append(("Afsluiten", self.root.destroy, False))
        self.buttons(*specs)

    def on_close(self):
        if self.busy_flashing and not messagebox.askyesno(
                "Afsluiten", "De porto wordt nog bijgewerkt. Toch afsluiten?\n"
                             "(U kunt het daarna gewoon opnieuw doen.)"):
            return
        self.cancel.set()
        if getattr(self, "flash_cancel", None):
            self.flash_cancel.set()
        self._close_when_idle(time_left_ms=3000)

    def _close_when_idle(self, time_left_ms):
        """Let a running flash stop first (K5TOOL is killed within ~0.2 s), so
        no flasher keeps running from the EXE's unpack folder."""
        if self._worker is not None and self._worker.is_alive() and time_left_ms > 0:
            self.root.after(100, lambda: self._close_when_idle(time_left_ms - 100))
            return
        self.root.destroy()
