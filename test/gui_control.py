#!/usr/bin/env python3
"""
gui_control.py — Interactive GUI for the Robot Game Scoreboard.

Sends serial text commands (same syntax as OSC addresses) over USB.
Requires:  pip install pyserial

Usage:
    python gui_control.py                  # auto-detect serial port
    python gui_control.py /dev/ttyACM0     # explicit port

Note: PlatformIO's bundled Python lacks tkinter.
    On macOS, avoid the Command Line Tools Python + Tk 8.5 runtime.
    Use a Python build with Tk 8.6+, e.g. python.org, Homebrew, or conda.
"""

import sys
import json
import os
import time
import queue
import threading
import tkinter as tk
import tkinter.font as tkfont
from tkinter import ttk, colorchooser, scrolledtext, filedialog
import serial
import serial.tools.list_ports


def _check_macos_tk_runtime_or_exit():
    """Fail fast on macOS when Python is backed by the old Apple Tk 8.5 runtime."""
    if sys.platform != "darwin":
        return

    try:
        import _tkinter
    except Exception:
        return

    tk_runtime_path = os.path.realpath(getattr(_tkinter, "__file__", ""))
    tk_version = float(getattr(tk, "TkVersion", 0.0))
    uses_clt_python = tk_runtime_path.startswith("/Library/Developer/CommandLineTools/")

    if uses_clt_python and tk_version < 8.6:
        msg = "\n".join([
            "gui_control.py cannot start with this macOS Python/Tk runtime.",
            "Detected: Command Line Tools Python linked to Apple Tk 8.5.",
            "This combination aborts when creating a Tk window on newer macOS releases.",
            "",
            "Use a Python build with Tk 8.6+, for example:",
            "  python.org Python",
            "  Homebrew python3",
            "  conda Python",
            "",
            "Typical setup:",
            "  python3 -m venv .venv-tk",
            "  source .venv-tk/bin/activate",
            "  pip install pyserial",
            "  python test/gui_control.py",
            "",
            "Alternative: use the scoreboard WiFi GUI from the firmware AP.",
        ])
        print(msg, file=sys.stderr)
        raise SystemExit(1)

# ── Serial helpers ────────────────────────────────────────────

def find_serial_ports():
    """Return list of available serial port device names."""
    return [p.device for p in serial.tools.list_ports.comports()]


def find_default_port():
    """Auto-detect first likely USB serial port."""
    for p in serial.tools.list_ports.comports():
        if any(k in p.description for k in ("USB", "CP210", "CH340")) or "ACM" in p.device:
            return p.device
    ports = serial.tools.list_ports.comports()
    return ports[0].device if ports else None


class ScoreboardGUI:
    BAUD = 115200
    SEND_THROTTLE_MS = 40  # min interval between repeated slider sends
    DEVICE_BOOT_SYNC_DELAY_MS = 1200
    SCRIPT_UPLOAD_SEND_INTERVAL_MS = 70

    def __init__(self, root, initial_port=None):
        self.root = root
        self.root.title("Scoreboard Control")
        self.ser = None
        self._reader_thread = None
        self._writer_thread = None
        self._stop_reader = threading.Event()
        self._write_queue = queue.Queue()
        self._throttle_id = None  # after-id for debounced slider send
        self._loading = False     # guard: suppress serial sends during state restore

        # Current colour (R, G, B)
        self._color = (255, 255, 255)

        # Per-display state tracking (display numbers are 1-based)
        self._current_display = 1
        self._display_states = {n: self._default_display_state() for n in range(1, 7)}
        self._captured_state = None
        self._script_files = []
        self._bank_slot_names = {slot: "" for slot in range(1, 6)}
        self._selected_bank_slot = 1
        self._pending_capture_path = None
        self._esp_bank_var = tk.IntVar(value=1)
        self._script_upload_in_progress = False
        self._display_state_request_log_queue = {n: [] for n in range(1, 7)}

        self._apply_compact_style()

        self._build_ui(initial_port)

        # Track display changes
        self._display_var.trace_add("write", self._on_display_change)

    # ── UI construction ───────────────────────────────────────

    def _apply_compact_style(self):
        """Reduce default Tk/ttk font sizes so the full UI fits typical desktops."""
        base = tkfont.nametofont("TkDefaultFont")
        base.configure(size=9)
        tkfont.nametofont("TkTextFont").configure(size=9)
        tkfont.nametofont("TkFixedFont").configure(size=9)
        tkfont.nametofont("TkHeadingFont").configure(size=9)
        tkfont.nametofont("TkMenuFont").configure(size=9)

        style = ttk.Style()
        style.configure("TLabel", font=base)
        style.configure("TButton", font=base, padding=(4, 2))
        style.configure("TCheckbutton", font=base)
        style.configure("TRadiobutton", font=base)
        style.configure("TLabelframe.Label", font=base)
        style.configure("TEntry", font=base)
        style.configure("TCombobox", font=base)
        style.configure("TSpinbox", font=base)

    def _build_ui(self, initial_port):
        pad = dict(padx=6, pady=3)

        # ── Connection frame ──────────────────────────────────
        conn = ttk.LabelFrame(self.root, text="Connection")
        conn.pack(fill="x", **pad)

        ttk.Label(conn, text="Port:").grid(row=0, column=0, **pad)
        self._port_var = tk.StringVar(value=initial_port or "")
        self._port_combo = ttk.Combobox(conn, textvariable=self._port_var, width=20)
        self._port_combo["values"] = find_serial_ports()
        self._port_combo.grid(row=0, column=1, **pad)

        self._btn_refresh = ttk.Button(conn, text="⟳", width=3, command=self._refresh_ports)
        self._btn_refresh.grid(row=0, column=2, **pad)

        self._btn_connect = ttk.Button(conn, text="Connect", command=self._toggle_connection)
        self._btn_connect.grid(row=0, column=3, **pad)

        self._status_var = tk.StringVar(value="Disconnected")
        ttk.Label(conn, textvariable=self._status_var, foreground="gray").grid(
            row=0, column=4, **pad
        )

        # ── Display selector ─────────────────────────────────
        disp = ttk.LabelFrame(self.root, text="Display")
        disp.pack(fill="x", **pad)

        ttk.Label(disp, text="Display #:").grid(row=0, column=0, **pad)
        self._display_var = tk.IntVar(value=1)
        ttk.Spinbox(disp, from_=1, to=6, textvariable=self._display_var, width=4).grid(
            row=0, column=1, sticky="w", **pad
        )

        # ── Two-column layout ────────────────────────────────
        mid = ttk.Frame(self.root)
        mid.pack(fill="both", **pad)
        left_col = ttk.Frame(mid)
        left_col.grid(row=0, column=0, sticky="nsew", padx=(0, 3))
        right_col = ttk.Frame(mid)
        right_col.grid(row=0, column=1, sticky="nsew", padx=(3, 0))
        mid.columnconfigure(0, weight=1)
        mid.columnconfigure(1, weight=1)

        # ── Layers & Mode frame ──────────────────────────────
        mode_f = ttk.LabelFrame(left_col, text="Layers & Mode")
        mode_f.pack(fill="x", **pad)

        # Row 0: Text layer enable + text mode selection
        self._text_enabled = tk.BooleanVar(value=True)
        ttk.Checkbutton(mode_f, text="Text", variable=self._text_enabled,
                        command=self._on_text_toggle).grid(row=0, column=0, **pad)

        self._mode_var = tk.StringVar(value="text")
        modes = [("Immediate", "text"), ("Scroll ↑", "scroll_up"),
                 ("Scroll ↓", "scroll_down")]
        for i, (label, val) in enumerate(modes):
            ttk.Radiobutton(mode_f, text=label, variable=self._mode_var,
                            value=val, command=self._on_mode_change).grid(
                row=0, column=1 + i, **pad
            )

        # Row 1: per-layer brightness
        ttk.Label(mode_f, text="Text bright:").grid(row=1, column=0, **pad)
        self._text_brightness_var = tk.IntVar(value=255)
        ttk.Scale(mode_f, from_=0, to=255, variable=self._text_brightness_var,
                  orient="horizontal", length=140,
                  command=lambda *_: self._on_text_brightness()).grid(row=1, column=1, columnspan=2, **pad)
        self._text_bright_label = ttk.Label(mode_f, text="255")
        self._text_bright_label.grid(row=1, column=3, **pad)

        # ── Text / Stack frame ─────────────────────────────────
        text_f = ttk.LabelFrame(left_col, text="Text / Stack")
        text_f.pack(fill="x", **pad)

        self._text_var = tk.StringVar()
        text_entry = ttk.Entry(text_f, textvariable=self._text_var, width=36)
        text_entry.grid(row=0, column=0, columnspan=3, sticky="ew", **pad)
        text_entry.bind("<Return>", lambda e: (self._send_text(), self.root.focus_set()))
        ttk.Button(text_f, text="Apply List", command=self._send_text).grid(row=0, column=3, **pad)
        ttk.Button(text_f, text="Clear List", command=self._ts_clear).grid(row=0, column=4, **pad)

        self._scrollcont_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(text_f, text="Continuous", variable=self._scrollcont_var,
                command=self._on_scrollcontinuous).grid(row=0, column=5, **pad)

        # Row 1: compact horizontal stack display
        ttk.Label(text_f, text="Stack:").grid(row=1, column=0, sticky="w", **pad)
        self._ts_listbox = tk.Listbox(text_f, height=2, width=60, font=("Menlo", 9))
        self._ts_listbox.grid(row=1, column=1, columnspan=5, sticky="ew", **pad)
        text_f.columnconfigure(0, weight=1)
        text_f.columnconfigure(1, weight=1)

        # ── Text colour frame ─────────────────────────────────
        color_f = ttk.LabelFrame(left_col, text="Text Colour")
        color_f.pack(fill="x", **pad)

        self._color_preview = tk.Canvas(color_f, width=30, height=30,
                                        bg=self._color_hex(), highlightthickness=1)
        self._color_preview.grid(row=0, column=0, rowspan=2, **pad)

        ttk.Button(color_f, text="Pick…", command=self._pick_color).grid(
            row=0, column=1, **pad
        )

        self._r_var = tk.IntVar(value=255)
        self._g_var = tk.IntVar(value=255)
        self._b_var = tk.IntVar(value=255)

        for i, (label, var) in enumerate([("R", self._r_var), ("G", self._g_var), ("B", self._b_var)]):
            ttk.Label(color_f, text=label).grid(row=0, column=2 + i * 2, **pad)
            s = ttk.Scale(color_f, from_=0, to=255, variable=var, orient="horizontal",
                          length=100, command=lambda *_: self._on_color_slider())
            s.grid(row=0, column=3 + i * 2, **pad)

        # ── Brightness frame ─────────────────────────────────
        bright_f = ttk.LabelFrame(left_col, text="Brightness")
        bright_f.pack(fill="x", **pad)

        self._brightness_var = tk.IntVar(value=10)
        ttk.Label(bright_f, text="0").grid(row=0, column=0)
        self._bright_scale = ttk.Scale(bright_f, from_=0, to=255,
                                       variable=self._brightness_var,
                                       orient="horizontal", length=300,
                                       command=lambda *_: self._on_brightness())
        self._bright_scale.grid(row=0, column=1, **pad)
        ttk.Label(bright_f, text="255").grid(row=0, column=2)
        self._bright_label = ttk.Label(bright_f, text="10")
        self._bright_label.grid(row=0, column=3, **pad)

        # ── Scroll speed frame ───────────────────────────────
        scroll_f = ttk.LabelFrame(left_col, text="Scroll Settings")
        scroll_f.pack(fill="x", **pad)

        ttk.Label(scroll_f, text="Speed (ms/px):").grid(row=0, column=0, **pad)
        self._scrollspeed_var = tk.IntVar(value=50)
        ttk.Scale(scroll_f, from_=5, to=200, variable=self._scrollspeed_var,
                  orient="horizontal", length=150,
                  command=lambda *_: self._on_scrollspeed()).grid(row=0, column=1, **pad)
        self._scrollspeed_label = ttk.Label(scroll_f, text="50")
        self._scrollspeed_label.grid(row=0, column=2, **pad)

        # ── Particle settings frame ──────────────────────────
        part_f = ttk.LabelFrame(right_col, text="Particle Settings")
        part_f.pack(fill="x", **pad)

        # Enable checkbox (particles as overlay, independent of text mode)
        self._particles_enabled = tk.BooleanVar(value=False)
        ttk.Checkbutton(part_f, text="Enable Particles",
                        variable=self._particles_enabled,
                        command=self._on_particles_toggle).grid(
            row=0, column=0, sticky="w", **pad
        )

        self._pspeedcolor_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(part_f, text="Speed Color",
                        variable=self._pspeedcolor_var,
                        command=self._send_particle_config).grid(
            row=0, column=1, sticky="w", **pad
        )

        # Particle colour (independent from text colour)
        self._pcolor = (100, 100, 255)  # default: bluish
        self._pcolor_preview = tk.Canvas(part_f, width=20, height=20,
                                         bg=self._pcolor_hex(), highlightthickness=1)
        self._pcolor_preview.grid(row=0, column=2, **pad)
        ttk.Button(part_f, text="Color…", command=self._pick_particle_color).grid(
            row=0, column=3, **pad
        )

        # Particle brightness
        ttk.Label(part_f, text="Bright:").grid(row=0, column=4, **pad)
        self._particle_brightness_var = tk.IntVar(value=255)
        ttk.Scale(part_f, from_=0, to=255, variable=self._particle_brightness_var,
                  orient="horizontal", length=100,
                  command=lambda *_: self._on_particle_brightness()).grid(row=0, column=5, **pad)

        # Row 1: count + render interval + physics substep
        ttk.Label(part_f, text="Count:").grid(row=1, column=0, **pad)
        self._pcount_var = tk.IntVar(value=6)
        ttk.Spinbox(part_f, from_=0, to=64, textvariable=self._pcount_var, width=4,
                     command=self._send_particle_config).grid(row=1, column=1, sticky="w", **pad)

        ttk.Label(part_f, text="Render (ms):").grid(row=1, column=2, **pad)
        self._prenderms_var = tk.IntVar(value=20)
        ttk.Spinbox(part_f, from_=5, to=100, textvariable=self._prenderms_var, width=4,
                     command=self._send_particle_config).grid(row=1, column=3, sticky="w", **pad)

        ttk.Label(part_f, text="Substep (ms):").grid(row=1, column=4, **pad)
        self._psubstep_var = tk.IntVar(value=20)
        ttk.Spinbox(part_f, from_=5, to=50, textvariable=self._psubstep_var, width=4,
                     command=self._send_particle_config).grid(row=1, column=5, sticky="w", **pad)

        # Row 2: gravity scale + gravity enable
        self._pgrav_enabled = tk.BooleanVar(value=True)
        ttk.Checkbutton(part_f, text="Gravity:", variable=self._pgrav_enabled,
                        command=self._send_particle_config).grid(row=2, column=0, **pad)
        self._pgrav_var = tk.DoubleVar(value=18.0)
        self._pgrav_scale = ttk.Scale(part_f, from_=0, to=60, variable=self._pgrav_var,
                                      orient="horizontal", length=180,
                                      command=lambda *_: self._send_particle_config())
        self._pgrav_scale.grid(row=2, column=1, columnspan=2, **pad)
        self._pgrav_label = ttk.Label(part_f, text="18.0")
        self._pgrav_label.grid(row=2, column=3, **pad)

        # Row 2 cont: collision enable checkbox
        self._pcollision_enabled = tk.BooleanVar(value=True)
        ttk.Checkbutton(part_f, text="Collision", variable=self._pcollision_enabled,
                        command=self._send_particle_config).grid(row=2, column=4, **pad)

        # Row 3: restitution (particle-particle) + wall restitution
        ttk.Label(part_f, text="Restit (p-p):").grid(row=3, column=0, **pad)
        self._pelast_var = tk.DoubleVar(value=0.92)
        ttk.Scale(part_f, from_=0, to=1.0, variable=self._pelast_var,
                  orient="horizontal", length=100,
                  command=lambda *_: self._send_particle_config()).grid(row=3, column=1, **pad)

        ttk.Label(part_f, text="Restit (wall):").grid(row=3, column=2, **pad)
        self._pwelast_var = tk.DoubleVar(value=0.78)
        ttk.Scale(part_f, from_=0, to=1.0, variable=self._pwelast_var,
                  orient="horizontal", length=100,
                  command=lambda *_: self._send_particle_config()).grid(row=3, column=3, **pad)

        # Row 3 cont: damping (per-substep velocity multiplier, 1=none)
        ttk.Label(part_f, text="Damping:").grid(row=3, column=4, **pad)
        self._pdamping_var = tk.DoubleVar(value=0.9998)
        ttk.Spinbox(part_f, from_=0.99, to=1.0, increment=0.0001,
                    textvariable=self._pdamping_var, width=7, format="%.4f",
                    command=self._send_particle_config).grid(row=3, column=5, sticky="w", **pad)

        # Row 4: radius + render style
        ttk.Label(part_f, text="Radius:").grid(row=4, column=0, **pad)
        self._pradius_var = tk.DoubleVar(value=0.45)
        ttk.Scale(part_f, from_=0.1, to=2.0, variable=self._pradius_var,
                  orient="horizontal", length=100,
                  command=lambda *_: self._send_particle_config()).grid(row=4, column=1, **pad)
        self._pradius_label = ttk.Label(part_f, text="0.45")
        self._pradius_label.grid(row=4, column=2, **pad)

        self._prender_var = tk.IntVar(value=4)  # 0=point,1=square,2=circle,3=text,4=glow
        style_f = ttk.Frame(part_f)
        style_f.grid(row=4, column=3, columnspan=3, sticky="w", **pad)
        for label, val in [("Point", 0), ("Square", 1), ("Circle", 2), ("Text", 3), ("Glow", 4)]:
            ttk.Radiobutton(style_f, text=label, variable=self._prender_var,
                            value=val, command=self._send_particle_config).pack(side="left", padx=2)

        # Row 5: glow sigma + wavelength (interference)
        ttk.Label(part_f, text="Glow σ:").grid(row=5, column=0, **pad)
        self._psigma_var = tk.DoubleVar(value=1.2)
        ttk.Scale(part_f, from_=0.2, to=4.0, variable=self._psigma_var,
                  orient="horizontal", length=120,
                  command=lambda *_: self._send_particle_config()).grid(row=5, column=1, **pad)
        self._psigma_label = ttk.Label(part_f, text="1.2")
        self._psigma_label.grid(row=5, column=2, **pad)

        ttk.Label(part_f, text="λ (wave):").grid(row=5, column=3, **pad)
        self._pwavelength_var = tk.DoubleVar(value=0.0)
        ttk.Scale(part_f, from_=0, to=8.0, variable=self._pwavelength_var,
                  orient="horizontal", length=120,
                  command=lambda *_: self._send_particle_config()).grid(row=5, column=4, **pad)
        self._pwavelength_label = ttk.Label(part_f, text="0.0")
        self._pwavelength_label.grid(row=5, column=5, **pad)
        # Row 6: temperature (Langevin jitter)
        ttk.Label(part_f, text="Temp:").grid(row=6, column=0, **pad)
        self._ptemp_var = tk.DoubleVar(value=0.0)
        ttk.Scale(part_f, from_=0, to=2.0, variable=self._ptemp_var,
                  orient="horizontal", length=180,
                  command=lambda *_: self._send_particle_config()).grid(row=6, column=1, columnspan=2, **pad)
        self._ptemp_label = ttk.Label(part_f, text="0.0")
        self._ptemp_label.grid(row=6, column=3, **pad)

        # Row 7: attraction strength + range
        ttk.Label(part_f, text="Attract:").grid(row=7, column=0, **pad)
        self._pattract_var = tk.DoubleVar(value=0.0)
        ttk.Scale(part_f, from_=0, to=1.0, variable=self._pattract_var,
                  orient="horizontal", length=100,
                  command=lambda *_: self._send_particle_config()).grid(row=7, column=1, **pad)
        self._pattract_label = ttk.Label(part_f, text="0.00")
        self._pattract_label.grid(row=7, column=2, **pad)

        ttk.Label(part_f, text="Range (×d):").grid(row=7, column=3, **pad)
        self._pattrange_var = tk.DoubleVar(value=3.0)
        ttk.Scale(part_f, from_=1.5, to=8.0, variable=self._pattrange_var,
                  orient="horizontal", length=100,
                  command=lambda *_: self._send_particle_config()).grid(row=7, column=4, **pad)

        # Row 8: spring force — enable + strength + range
        self._pspring_enabled = tk.BooleanVar(value=False)
        ttk.Checkbutton(part_f, text="Spring",
                        variable=self._pspring_enabled,
                        command=self._send_particle_config).grid(
            row=8, column=0, sticky="w", **pad
        )
        ttk.Label(part_f, text="Str:").grid(row=8, column=1, sticky="e", **pad)
        self._pspring_str_var = tk.DoubleVar(value=0.0)
        ttk.Scale(part_f, from_=-5.0, to=5.0, variable=self._pspring_str_var,
                  orient="horizontal", length=100,
                  command=lambda *_: self._send_particle_config()).grid(row=8, column=2, **pad)
        self._pspring_str_label = ttk.Label(part_f, text="0.00")
        self._pspring_str_label.grid(row=8, column=3, **pad)
        ttk.Label(part_f, text="Range:").grid(row=8, column=4, sticky="e", **pad)
        self._pspring_range_var = tk.DoubleVar(value=5.0)
        ttk.Scale(part_f, from_=0.5, to=20.0, variable=self._pspring_range_var,
                  orient="horizontal", length=100,
                  command=lambda *_: self._send_particle_config()).grid(row=8, column=5, **pad)

        # Row 9: Coulomb force — enable + strength + range
        self._pcoulomb_enabled = tk.BooleanVar(value=False)
        ttk.Checkbutton(part_f, text="Coulomb",
                        variable=self._pcoulomb_enabled,
                        command=self._send_particle_config).grid(
            row=9, column=0, sticky="w", **pad
        )
        ttk.Label(part_f, text="Str:").grid(row=9, column=1, sticky="e", **pad)
        self._pcoulomb_str_var = tk.DoubleVar(value=0.0)
        ttk.Scale(part_f, from_=-5.0, to=5.0, variable=self._pcoulomb_str_var,
                  orient="horizontal", length=100,
                  command=lambda *_: self._send_particle_config()).grid(row=9, column=2, **pad)
        self._pcoulomb_str_label = ttk.Label(part_f, text="0.00")
        self._pcoulomb_str_label.grid(row=9, column=3, **pad)
        ttk.Label(part_f, text="Range:").grid(row=9, column=4, sticky="e", **pad)
        self._pcoulomb_range_var = tk.DoubleVar(value=10.0)
        ttk.Scale(part_f, from_=0.5, to=30.0, variable=self._pcoulomb_range_var,
                  orient="horizontal", length=100,
                  command=lambda *_: self._send_particle_config()).grid(row=9, column=5, **pad)

        # Row 10: Scaffold attraction — enable + strength + range
        self._pscaffold_enabled = tk.BooleanVar(value=False)
        ttk.Checkbutton(part_f, text="Scaffold",
                        variable=self._pscaffold_enabled,
                        command=self._send_particle_config).grid(
            row=10, column=0, sticky="w", **pad
        )
        ttk.Label(part_f, text="Str:").grid(row=10, column=1, sticky="e", **pad)
        self._pscaffold_str_var = tk.DoubleVar(value=0.0)
        ttk.Scale(part_f, from_=0.0, to=5.0, variable=self._pscaffold_str_var,
                  orient="horizontal", length=100,
                  command=lambda *_: self._send_particle_config()).grid(row=10, column=2, **pad)
        self._pscaffold_str_label = ttk.Label(part_f, text="0.00")
        self._pscaffold_str_label.grid(row=10, column=3, **pad)
        ttk.Label(part_f, text="Range:").grid(row=10, column=4, sticky="e", **pad)
        self._pscaffold_range_var = tk.DoubleVar(value=10.0)
        ttk.Scale(part_f, from_=0.5, to=30.0, variable=self._pscaffold_range_var,
                  orient="horizontal", length=100,
                  command=lambda *_: self._send_particle_config()).grid(row=10, column=5, **pad)

        # Row 11: Text→Particles + Add/Delete particles + Pause Physics
        ttk.Button(part_f, text="Text → Particles",
                   command=self._text_to_particles).grid(row=11, column=0, columnspan=2, **pad)
        ttk.Button(part_f, text="Add Particle",
               command=self._add_particle).grid(row=11, column=2, **pad)
        ttk.Button(part_f, text="Delete All",
               command=self._clear_particles).grid(row=11, column=3, **pad)
        self._physics_paused = tk.BooleanVar(value=False)
        ttk.Checkbutton(part_f, text="Pause Physics", variable=self._physics_paused,
                command=self._toggle_physics_pause).grid(row=11, column=4, columnspan=2, sticky="w", **pad)

        # Row 12: View transform — rotation + scale
        ttk.Label(part_f, text="Rotate°:").grid(row=12, column=0, **pad)
        self._protate_var = tk.DoubleVar(value=0.0)
        ttk.Scale(part_f, from_=-180.0, to=180.0, variable=self._protate_var,
                  orient="horizontal", length=120,
                  command=lambda *_: self._send_transform()).grid(row=12, column=1, **pad)
        self._protate_label = ttk.Label(part_f, text="0.0")
        self._protate_label.grid(row=12, column=2, **pad)

        ttk.Label(part_f, text="Scale:").grid(row=12, column=3, **pad)
        self._pscale_var = tk.DoubleVar(value=1.0)
        ttk.Scale(part_f, from_=0.1, to=4.0, variable=self._pscale_var,
                  orient="horizontal", length=120,
                  command=lambda *_: self._send_transform()).grid(row=12, column=4, **pad)
        self._pscale_label = ttk.Label(part_f, text="1.0")
        self._pscale_label.grid(row=12, column=5, **pad)

        # Row 13: View transform — translate X/Y + reset
        ttk.Label(part_f, text="Tx:").grid(row=13, column=0, **pad)
        self._ptx_var = tk.DoubleVar(value=0.0)
        ttk.Scale(part_f, from_=-16.0, to=16.0, variable=self._ptx_var,
                  orient="horizontal", length=100,
                  command=lambda *_: self._send_transform()).grid(row=13, column=1, **pad)

        ttk.Label(part_f, text="Ty:").grid(row=13, column=2, **pad)
        self._pty_var = tk.DoubleVar(value=0.0)
        ttk.Scale(part_f, from_=-8.0, to=8.0, variable=self._pty_var,
                  orient="horizontal", length=100,
                  command=lambda *_: self._send_transform()).grid(row=13, column=3, **pad)

        ttk.Button(part_f, text="Reset Transform",
                   command=self._reset_transform).grid(row=13, column=4, **pad)

        # ── Actions frame ────────────────────────────────────
        act_f = ttk.LabelFrame(left_col, text="Actions")
        act_f.pack(fill="x", **pad)

        ttk.Button(act_f, text="Clear Display", command=self._clear_display).grid(
            row=0, column=0, **pad
        )
        ttk.Button(act_f, text="Clear All", command=self._clear_all).grid(
            row=0, column=1, **pad
        )
        ttk.Button(act_f, text="Raster Scan", command=self._raster_scan).grid(
            row=0, column=2, **pad
        )
        ttk.Button(act_f, text="Defaults", command=self._reset_defaults).grid(
            row=0, column=3, **pad
        )

        # ── Animation bank + display assignment ───────────────
        anim_f = ttk.LabelFrame(left_col, text="Animation Bank")
        anim_f.pack(fill="x", **pad)

        anim_left = ttk.Frame(anim_f)
        anim_left.grid(row=0, column=0, sticky="nsew", padx=(0, 6))
        anim_right = ttk.Frame(anim_f)
        anim_right.grid(row=0, column=1, sticky="nsew", padx=(6, 0))
        anim_f.columnconfigure(0, weight=1)
        anim_f.columnconfigure(1, weight=1)

        ttk.Label(anim_left, text="Bank slot:").grid(row=0, column=0, sticky="w", **pad)
        self._bank_slot_choice_var = tk.StringVar()
        self._bank_slot_combo = ttk.Combobox(
            anim_left,
            textvariable=self._bank_slot_choice_var,
            state="readonly",
            width=26,
        )
        self._bank_slot_combo.grid(row=1, column=0, columnspan=2, sticky="ew", **pad)
        self._bank_slot_combo.bind("<<ComboboxSelected>>", self._on_bank_slot_selected)
        ttk.Button(anim_left, text="Load File…", command=self._load_script_file_from_disk).grid(row=2, column=0, **pad)
        ttk.Button(anim_left, text="Set To Display", command=self._assign_animation_to_display).grid(row=2, column=1, **pad)
        anim_left.columnconfigure(0, weight=1)
        anim_left.columnconfigure(1, weight=1)

        self._animation_slot_var = tk.IntVar(value=0)
        self._runtime_label_var = tk.StringVar(value=self._runtime_label_text(1))
        ttk.Label(anim_right, textvariable=self._runtime_label_var).grid(row=0, column=0, sticky="w", **pad)
        self._current_animation_name_var = tk.StringVar(value="default")
        ttk.Label(anim_right, textvariable=self._current_animation_name_var).grid(row=1, column=0, sticky="w", **pad)
        ttk.Button(anim_right, text="Start", command=self._start_animation).grid(row=2, column=0, sticky="ew", **pad)
        ttk.Button(anim_right, text="Stop", command=self._stop_animation).grid(row=3, column=0, sticky="ew", **pad)
        anim_right.columnconfigure(0, weight=1)

        self._refresh_bank_slot_choices()

        # ── Snapshots and session files ───────────────────────
        snapshot_f = ttk.LabelFrame(left_col, text="Snapshot Parameters")
        snapshot_f.pack(fill="x", **pad)

        ttk.Button(snapshot_f, text="Capture To File…", command=self._capture_device_to_file).grid(
            row=0, column=0, columnspan=2, sticky="ew", **pad
        )
        ttk.Button(snapshot_f, text="Save Parameter File…", command=self._save_preset).grid(
            row=1, column=0, **pad
        )
        ttk.Button(snapshot_f, text="Load Parameter File…", command=self._load_preset).grid(
            row=1, column=1, **pad
        )

        # ── Raw command ──────────────────────────────────────
        raw_f = ttk.LabelFrame(self.root, text="Raw Command")
        raw_f.pack(fill="x", **pad)

        self._raw_var = tk.StringVar()
        raw_entry = ttk.Entry(raw_f, textvariable=self._raw_var, width=40)
        raw_entry.grid(row=0, column=0, **pad)
        raw_entry.bind("<Return>", lambda e: (self._send_raw(), self.root.focus_set()))
        ttk.Button(raw_f, text="Send", command=self._send_raw).grid(row=0, column=1, **pad)

        # ── Log frame ────────────────────────────────────────
        log_f = ttk.LabelFrame(self.root, text="Log")
        log_f.pack(fill="both", expand=True, **pad)

        self._log = scrolledtext.ScrolledText(log_f, height=10, state="disabled",
                                              font=("Menlo", 10))
        self._log.pack(fill="both", expand=True)

    # ── Connection ────────────────────────────────────────────

    def _refresh_ports(self):
        self._port_combo["values"] = find_serial_ports()

    def _toggle_connection(self):
        if self.ser and self.ser.is_open:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        port = self._port_var.get().strip()
        if not port:
            self._log_msg("No port selected")
            return
        try:
            self.ser = serial.Serial(port, self.BAUD, timeout=0.1)
            self._status_var.set(f"Connected to {port}")
            self._btn_connect.config(text="Disconnect")
            self._log_msg(f"Connected to {port} @ {self.BAUD}")
            self._stop_reader.clear()
            self._reader_thread = threading.Thread(target=self._serial_reader, daemon=True)
            self._reader_thread.start()
            self._writer_thread = threading.Thread(target=self._serial_writer, daemon=True)
            self._writer_thread.start()
            self.root.after(self.DEVICE_BOOT_SYNC_DELAY_MS, self._list_bank_slots)
            self.root.after(self.DEVICE_BOOT_SYNC_DELAY_MS + 200, self._sync_all_displays)
        except serial.SerialException as e:
            self._log_msg(f"Connection error: {e}")

    def _disconnect(self):
        self._stop_reader.set()
        self._script_upload_in_progress = False
        if self.ser:
            self.ser.close()
            self.ser = None
        self._status_var.set("Disconnected")
        self._btn_connect.config(text="Connect")
        self._log_msg("Disconnected")

    def _serial_reader(self):
        """Background thread: read firmware output and append to log."""
        while not self._stop_reader.is_set():
            try:
                if self.ser and self.ser.is_open and self.ser.in_waiting:
                    line = self.ser.readline().decode("utf-8", errors="replace").rstrip()
                    if line:
                        # Parse TEXT2PARTICLES response to sync GUI count
                        if line.startswith("TEXT2PARTICLES "):
                            self.root.after(0, self._log_msg, f"[fw] {line}")
                            try:
                                n = int(line.split()[1])
                                self.root.after(0, self._on_text2particles_count, n)
                            except (IndexError, ValueError):
                                pass
                        elif line.startswith("STARTUP_BANK "):
                            self.root.after(0, self._log_msg, f"[fw] {line}")
                            try:
                                bank = int(line.split()[1])
                                self.root.after(0, self._esp_bank_var.set, bank)
                            except (IndexError, ValueError):
                                pass
                        elif line.startswith("BANK_SLOT_LOADED "):
                            self.root.after(0, self._log_msg, f"[fw] {line}")
                            self.root.after(0, self._on_bank_slot_loaded_line, line)
                        elif line.startswith("BANK_SLOTS "):
                            self.root.after(0, self._log_msg, f"[fw] {line}")
                            self.root.after(0, self._on_bank_slots_line, line)
                        elif line.startswith("SCRIPT_FILE "):
                            self.root.after(0, self._log_msg, f"[fw] {line}")
                            self.root.after(0, self._on_script_file_line, line)
                        elif line.startswith("DISPLAY_STATE "):
                            payload = line[len("DISPLAY_STATE "):].strip()
                            self.root.after(0, self._on_display_state_line, payload)
                        else:
                            self.root.after(0, self._log_msg, f"[fw] {line}")
                else:
                    time.sleep(0.02)  # avoid busy-loop when idle
            except Exception:
                break

    def _serial_writer(self):
        """Background thread: drain the write queue so ser.write never blocks the GUI."""
        while not self._stop_reader.is_set():
            try:
                data = self._write_queue.get(timeout=0.1)
                if self.ser and self.ser.is_open:
                    self.ser.write(data)
            except queue.Empty:
                continue
            except Exception:
                break

    # ── Commands ──────────────────────────────────────────────

    # ── Per-display state management ─────────────────────────

    @staticmethod
    def _default_display_state():
        return {
            "textEnabled": True,
            "mode": "text",
            "animationSlot": 0,
            "animationName": "default",
            "textBrightness": 255,
            "particlesEnabled": False,
            "particleBrightness": 255,
            "color": (255, 255, 255),
            "particleColor": (100, 100, 255),
            "textStack": [],
            "particles": {
                "count": 6,
                "renderMs": 20,
                "substepMs": 20,
                "gravityScale": 18.0,
                "gravityEnabled": True,
                "collisionEnabled": True,
                "elasticity": 0.92,
                "wallElasticity": 0.78,
                "damping": 0.9998,
                "radius": 0.45,
                "renderStyle": 4,
                "glowSigma": 1.2,
                "glowWavelength": 0.0,
                "temperature": 0.0,
                "attractStrength": 0.0,
                "attractRange": 3.0,
                "speedColor": False,
                "springStrength": 0.0,
                "springRange": 5.0,
                "springEnabled": False,
                "coulombStrength": 0.0,
                "coulombRange": 10.0,
                "coulombEnabled": False,
                "scaffoldStrength": 0.0,
                "scaffoldRange": 10.0,
                "scaffoldEnabled": False,
                "physicsPaused": False,
                "viewRotation": 0.0,
                "viewScale": 1.0,
                "viewTx": 0.0,
                "viewTy": 0.0,
            },
        }

    def _snapshot_display_state(self):
        """Capture current widget values into a dict."""
        return {
            "textEnabled": self._text_enabled.get(),
            "mode": self._mode_var.get(),
            "animationSlot": self._animation_slot_var.get(),
            "animationName": self._current_animation_name_var.get(),
            "textBrightness": self._text_brightness_var.get(),
            "particlesEnabled": self._particles_enabled.get(),
            "particleBrightness": self._particle_brightness_var.get(),
            "color": (self._r_var.get(), self._g_var.get(), self._b_var.get()),
            "particleColor": self._pcolor,
            "textStack": list(self._ts_listbox.get(0, "end")),
            "particles": {
                "count": self._pcount_var.get(),
                "renderMs": self._prenderms_var.get(),
                "substepMs": self._psubstep_var.get(),
                "gravityScale": self._pgrav_var.get(),
                "gravityEnabled": self._pgrav_enabled.get(),
                "collisionEnabled": self._pcollision_enabled.get(),
                "elasticity": self._pelast_var.get(),
                "wallElasticity": self._pwelast_var.get(),
                "damping": self._pdamping_var.get(),
                "radius": self._pradius_var.get(),
                "renderStyle": self._prender_var.get(),
                "glowSigma": self._psigma_var.get(),
                "glowWavelength": self._pwavelength_var.get(),
                "temperature": self._ptemp_var.get(),
                "attractStrength": self._pattract_var.get(),
                "attractRange": self._pattrange_var.get(),
                "speedColor": self._pspeedcolor_var.get(),
                "springStrength": self._pspring_str_var.get(),
                "springRange": self._pspring_range_var.get(),
                "springEnabled": self._pspring_enabled.get(),
                "coulombStrength": self._pcoulomb_str_var.get(),
                "coulombRange": self._pcoulomb_range_var.get(),
                "coulombEnabled": self._pcoulomb_enabled.get(),
                "scaffoldStrength": self._pscaffold_str_var.get(),
                "scaffoldRange": self._pscaffold_range_var.get(),
                "scaffoldEnabled": self._pscaffold_enabled.get(),
                "physicsPaused": self._physics_paused.get(),
                "viewRotation": self._protate_var.get(),
                "viewScale": self._pscale_var.get(),
                "viewTx": self._ptx_var.get(),
                "viewTy": self._pty_var.get(),
            },
        }

    def _restore_display_state(self, state):
        """Load a state dict into widgets without sending serial commands."""
        self._loading = True
        try:
            self._text_enabled.set(state["textEnabled"])
            self._mode_var.set(state["mode"])
            animation_slot = state.get("animationSlot", state.get("animationId", 0))
            self._animation_slot_var.set(animation_slot)
            self._current_animation_name_var.set(state.get("animationName", self._slot_name(animation_slot)))
            self._text_brightness_var.set(state["textBrightness"])
            self._text_bright_label.config(text=str(state["textBrightness"]))
            self._particles_enabled.set(state["particlesEnabled"])
            self._particle_brightness_var.set(state["particleBrightness"])
            r, g, b = state["color"]
            self._r_var.set(r); self._g_var.set(g); self._b_var.set(b)
            self._color = (r, g, b)
            self._color_preview.config(bg=self._color_hex())
            self._pcolor = state["particleColor"]
            self._pcolor_preview.config(bg=self._pcolor_hex())
            # Text stack
            self._ts_listbox.delete(0, "end")
            for item in state["textStack"]:
                self._ts_listbox.insert("end", item)
            self._text_var.set(", ".join(state["textStack"]))
            # Particles
            p = state["particles"]
            self._pcount_var.set(p["count"])
            self._prenderms_var.set(p["renderMs"])
            self._psubstep_var.set(p["substepMs"])
            self._pgrav_var.set(p["gravityScale"])
            self._pgrav_enabled.set(p["gravityEnabled"])
            self._pcollision_enabled.set(p["collisionEnabled"])
            self._pelast_var.set(p["elasticity"])
            self._pwelast_var.set(p["wallElasticity"])
            self._pdamping_var.set(p["damping"])
            self._pradius_var.set(p["radius"])
            self._prender_var.set(p["renderStyle"])
            self._psigma_var.set(p["glowSigma"])
            self._pwavelength_var.set(p["glowWavelength"])
            self._ptemp_var.set(p["temperature"])
            self._pattract_var.set(p["attractStrength"])
            self._pattrange_var.set(p["attractRange"])
            self._pspeedcolor_var.set(p["speedColor"])
            self._pspring_str_var.set(p.get("springStrength", 0.0))
            self._pspring_range_var.set(p.get("springRange", 5.0))
            self._pspring_enabled.set(p.get("springEnabled", False))
            self._pcoulomb_str_var.set(p.get("coulombStrength", 0.0))
            self._pcoulomb_range_var.set(p.get("coulombRange", 10.0))
            self._pcoulomb_enabled.set(p.get("coulombEnabled", False))
            self._pscaffold_str_var.set(p.get("scaffoldStrength", 0.0))
            self._pscaffold_range_var.set(p.get("scaffoldRange", 10.0))
            self._pscaffold_enabled.set(p.get("scaffoldEnabled", False))
            self._physics_paused.set(p.get("physicsPaused", False))
            self._protate_var.set(p.get("viewRotation", 0.0))
            self._pscale_var.set(p.get("viewScale", 1.0))
            self._ptx_var.set(p.get("viewTx", 0.0))
            self._pty_var.set(p.get("viewTy", 0.0))
            # Update labels
            self._pgrav_label.config(text=f"{p['gravityScale']:.1f}")
            self._pradius_label.config(text=f"{p['radius']:.2f}")
            self._psigma_label.config(text=f"{p['glowSigma']:.1f}")
            self._ptemp_label.config(text=f"{p['temperature']:.2f}")
            self._pattract_label.config(text=f"{p['attractStrength']:.2f}")
            self._pspring_str_label.config(text=f"{p.get('springStrength', 0.0):.2f}")
            self._pcoulomb_str_label.config(text=f"{p.get('coulombStrength', 0.0):.2f}")
            self._pscaffold_str_label.config(text=f"{p.get('scaffoldStrength', 0.0):.2f}")
            self._pwavelength_label.config(text=f"{p['glowWavelength']:.1f}")
            self._protate_label.config(text=f"{p.get('viewRotation', 0.0):.1f}")
            self._pscale_label.config(text=f"{p.get('viewScale', 1.0):.2f}")
        finally:
            self._loading = False

    def _on_display_change(self, *_):
        """Called when the display selector changes — swap per-display state."""
        try:
            new_display = self._display_var.get()
        except tk.TclError:
            return  # transient invalid value while typing
        if new_display < 1 or new_display > 6:
            return
        if new_display == self._current_display:
            return
        # Save current widget state for the old display
        self._display_states[self._current_display] = self._snapshot_display_state()
        # Switch and restore
        self._current_display = new_display
        self._update_runtime_label(new_display)
        self._restore_display_state(self._display_states[new_display])
        if self.ser and self.ser.is_open:
            self._schedule_display_sync(new_display, delay_ms=60)

    def _send(self, cmd, log=True):
        if not self.ser or not self.ser.is_open:
            self._log_msg("Not connected")
            return
        line = cmd.strip() + "\n"
        self._write_queue.put(line.encode("utf-8"))
        if log:
            self._log_msg(f"→ {cmd}")

    def _send_command_sequence(self, commands, interval_ms=None, on_complete=None):
        if interval_ms is None:
            interval_ms = self.SCRIPT_UPLOAD_SEND_INTERVAL_MS

        if self._script_upload_in_progress:
            self._log_msg("Another animation file upload is already in progress")
            return False
        if not self.ser or not self.ser.is_open:
            self._log_msg("Not connected")
            return False

        command_list = list(commands)
        self._script_upload_in_progress = True

        def send_next(index):
            if not self.ser or not self.ser.is_open:
                self._script_upload_in_progress = False
                return
            if index >= len(command_list):
                self._script_upload_in_progress = False
                if on_complete:
                    on_complete()
                return
            self._send(command_list[index])
            self.root.after(interval_ms, lambda: send_next(index + 1))

        send_next(0)
        return True

    def _disp_prefix(self):
        return f"/display/{self._display_var.get()}"

    def _on_mode_change(self):
        if self._loading: return
        self._send(f"{self._disp_prefix()}/mode {self._mode_var.get()}")

    def _on_text_toggle(self):
        if self._loading: return
        en = 1 if self._text_enabled.get() else 0
        self._send(f"{self._disp_prefix()}/text/enable {en}")

    def _on_particles_toggle(self):
        if self._loading: return
        en = 1 if self._particles_enabled.get() else 0
        self._send(f"{self._disp_prefix()}/particles/enable {en}")

    def _selected_animation_slot(self):
        return max(1, min(5, self._selected_bank_slot))

    def _runtime_label_text(self, display_number):
        return f"Current Animation D{display_number}:"

    def _update_runtime_label(self, display_number=None):
        if display_number is None:
            display_number = self._display_var.get()
        self._runtime_label_var.set(self._runtime_label_text(display_number))

    def _slot_name(self, slot):
        if slot <= 0:
            return "default"
        name = self._bank_slot_names.get(slot, "")
        if not name:
            return f"slot {slot}"
        if name == "EMPTY":
            return f"slot {slot} (empty)"
        return name

    def _bank_slot_option_label(self, slot):
        name = self._bank_slot_names.get(slot, "")
        if not name:
            name = "empty"
        elif name == "EMPTY":
            name = "empty"
        return f"{slot}: {name}"

    def _refresh_bank_slot_choices(self):
        options = [self._bank_slot_option_label(slot) for slot in range(1, 6)]
        self._bank_slot_combo["values"] = options
        self._set_bank_slot_choice(self._selected_bank_slot)

    def _set_bank_slot_choice(self, slot):
        self._selected_bank_slot = max(1, min(5, int(slot)))
        self._bank_slot_choice_var.set(self._bank_slot_option_label(self._selected_bank_slot))

    def _on_bank_slot_selected(self, *_):
        choice = self._bank_slot_choice_var.get().strip()
        try:
            slot = int(choice.split(":", 1)[0])
        except (ValueError, IndexError):
            slot = 1
        self._set_bank_slot_choice(slot)

    def _assign_animation_to_display(self):
        if self._loading:
            return
        slot = self._selected_animation_slot()
        self._send(f"{self._disp_prefix()}/animation {slot}")
        self._schedule_display_sync()

    def _start_animation(self):
        if self._loading:
            return
        self._send(f"{self._disp_prefix()}/animation/start")
        self._schedule_display_sync()

    def _stop_animation(self):
        if self._loading:
            return
        self._send(f"{self._disp_prefix()}/animation/stop")
        self._schedule_display_sync()

    def _request_display_state(self, display_number=None, log=True):
        target = display_number if display_number is not None else self._display_var.get()
        try:
            target = int(target)
        except (TypeError, ValueError, tk.TclError):
            return
        if target < 1 or target > 6:
            return
        self._display_state_request_log_queue[target].append(bool(log))
        self._send(f"/display/{target}/state", log=log)

    def _schedule_display_sync(self, display_number=None, delay_ms=150, log=True):
        target = display_number if display_number is not None else self._display_var.get()
        self.root.after(delay_ms, lambda t=target, should_log=log: self._request_display_state(t, log=should_log))

    def _sync_all_displays(self):
        for index in range(1, 7):
            self.root.after((index - 1) * 80, lambda target=index: self._request_display_state(target, log=False))

    def _script_file_name(self):
        name = self._script_file_var.get().strip()
        if not name:
            self._log_msg("Enter a script file name first")
            return None
        return name

    def _set_script_file_choices(self):
        return

    def _on_script_file_line(self, line):
        # Expected firmware output: SCRIPT_FILE /games/foo.game 123
        if line.strip() == "SCRIPT_FILE NONE":
            self._script_files = []
            self._set_script_file_choices()
            return
        parts = line.split()
        if len(parts) >= 2 and parts[0] == "SCRIPT_FILE":
            path = parts[1]
            if path not in self._script_files:
                self._script_files.append(path)
                self._script_files.sort()
                self._set_script_file_choices()

    def _on_bank_slots_line(self, line):
        payload = line[len("BANK_SLOTS "):].strip()
        updated_names = {slot: "EMPTY" for slot in range(1, 6)}
        if payload:
            for entry in payload.split(";"):
                entry = entry.strip()
                if not entry or ":" not in entry:
                    continue
                slot_text, name = entry.split(":", 1)
                try:
                    slot = int(slot_text.strip())
                except ValueError:
                    continue
                if 1 <= slot <= 5:
                    clean_name = name.strip() or "EMPTY"
                    updated_names[slot] = clean_name

        self._bank_slot_names.update(updated_names)
        self._refresh_bank_slot_choices()

    def _on_bank_slot_loaded_line(self, line):
        parts = line.split(maxsplit=2)
        if len(parts) < 3 or parts[0] != "BANK_SLOT_LOADED":
            return
        try:
            slot = int(parts[1])
        except ValueError:
            return
        self._bank_slot_names[slot] = parts[2]
        self._set_bank_slot_choice(slot)
        self._refresh_bank_slot_choices()
        self._log_msg(f"Bank slot {slot} loaded: {parts[2]}")
        # Ask the device for the authoritative slot list after the upload/commit ack.
        self.root.after(80, self._list_bank_slots)

    def _merge_display_state_from_device(self, state):
        try:
            display_number = int(state.get("display", self._current_display))
        except (TypeError, ValueError):
            display_number = self._current_display

        merged = self._default_display_state()
        existing = self._display_states.get(display_number)
        if existing:
            merged.update({k: v for k, v in existing.items() if k != "particles"})
            merged["particles"].update(existing.get("particles", {}))

        slot = int(state.get("animationSlot", state.get("animationId", merged["animationSlot"])))
        merged["animationSlot"] = slot
        merged["animationName"] = state.get("animationName", self._slot_name(slot))
        merged["mode"] = state.get("mode", merged["mode"])
        merged["textEnabled"] = state.get("textEnabled", merged["textEnabled"])
        merged["particlesEnabled"] = state.get("particlesEnabled", merged["particlesEnabled"])
        merged["textBrightness"] = state.get("textBrightness", merged["textBrightness"])
        merged["particleBrightness"] = state.get("particleBrightness", merged["particleBrightness"])

        text_color = state.get("textColor")
        if isinstance(text_color, list) and len(text_color) == 3:
            merged["color"] = tuple(int(value) for value in text_color)

        particle_color = state.get("particleColor")
        if isinstance(particle_color, list) and len(particle_color) == 3:
            merged["particleColor"] = tuple(int(value) for value in particle_color)

        text_items = state.get("textItems")
        if isinstance(text_items, list):
            merged["textStack"] = [str(item) for item in text_items if str(item).strip()]

        if int(state.get("textCount", len(merged["textStack"]))) == 0:
            merged["textStack"] = []

        particles = state.get("particles", {})
        particle_state = merged["particles"]
        for key in (
            "count", "renderMs", "substepMs", "gravityScale", "gravityEnabled",
            "collisionEnabled", "elasticity", "wallElasticity", "damping", "radius",
            "renderStyle", "glowSigma", "glowWavelength", "temperature",
            "attractStrength", "attractRange", "speedColor", "springStrength",
            "springRange", "springEnabled", "coulombStrength", "coulombRange",
            "coulombEnabled", "scaffoldStrength", "scaffoldRange", "scaffoldEnabled",
            "physicsPaused", "viewRotation", "viewTx", "viewTy"
        ):
            if key in particles:
                particle_state[key] = particles[key]
        if "viewScaleX" in particles:
            particle_state["viewScale"] = particles["viewScaleX"]

        scroll = state.get("scroll", {})
        if "stepMs" in scroll:
            self._scrollspeed_var.set(int(scroll["stepMs"]))
            self._scrollspeed_label.config(text=str(int(scroll["stepMs"])))
        if "continuous" in scroll:
            self._scrollcont_var.set(bool(scroll["continuous"]))

        return display_number, merged

    def _on_display_state_line(self, payload):
        try:
            state = json.loads(payload)
        except json.JSONDecodeError:
            self._log_msg("Could not parse DISPLAY_STATE payload")
            return
        self._captured_state = state
        display_number, merged = self._merge_display_state_from_device(state)
        request_queue = self._display_state_request_log_queue.get(display_number, [])
        should_log_sync = request_queue.pop(0) if request_queue else False
        self._display_states[display_number] = merged
        if display_number == self._current_display:
            self._restore_display_state(merged)
        if self._pending_capture_path and display_number == self._current_display:
            path = self._pending_capture_path
            self._pending_capture_path = None
            with open(path, "w", encoding="utf-8") as f:
                json.dump(state, f, indent=2)
            self._log_msg(f"Captured device parameters saved → {path}")
        elif should_log_sync:
            self._log_msg(f"Display {display_number} synchronized from device")

    def _capture_display_state(self):
        self._request_display_state(self._current_display)

    def _capture_device_to_file(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".json",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
            initialdir=os.path.dirname(__file__),
            title="Capture current device parameters",
        )
        if not path:
            return
        self._pending_capture_path = path
        self._capture_display_state()

    def _save_captured_state(self):
        if not self._captured_state:
            self._log_msg("No captured device state yet.")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".json",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
            initialdir=os.path.dirname(__file__),
            title="Save captured device state",
        )
        if not path:
            return
        with open(path, "w") as f:
            json.dump(self._captured_state, f, indent=2)
        self._log_msg(f"Captured state saved → {path}")

    def _save_settings_capture(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".json",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
            initialdir=os.path.dirname(__file__),
            title="Save particle/text settings capture",
        )
        if not path:
            return
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self._gather_params(), f, indent=2)
        self._log_msg(f"Settings capture saved → {path}")

    def _load_script_file(self):
        name = self._script_file_name()
        if name:
            slot = self._selected_animation_slot()
            if slot < 1:
                self._log_msg("Choose bank slot 1..5 before storing a file")
                return
            self._send(f'/script/bank/load {slot} "{name}"')
            self._list_bank_slots()

    def _load_script_file_from_disk(self):
        path = filedialog.askopenfilename(
            filetypes=[("Game script files", "*.game"), ("Text files", "*.txt"), ("All files", "*.*")],
            initialdir=os.path.dirname(__file__),
            title="Load animation file from computer",
        )
        if not path:
            return

        try:
            with open(path, "r", encoding="utf-8") as f:
                lines = f.read().splitlines()
        except OSError as e:
            self._log_msg(f"Could not open animation file: {e}")
            return

        slot = self._selected_animation_slot()
        if slot < 1:
            self._log_msg("Choose bank slot 1..5 before storing a file")
            return

        commands = ["/script/begin"]
        for line in lines:
            safe = line.replace('\\', '\\\\').replace('"', '\\"')
            commands.append(f'/script/append "{safe}"')
        commands.append(f"/script/bank/commit {slot}")

        started = self._send_command_sequence(
            commands,
            on_complete=lambda: self.root.after(200, self._list_bank_slots),
        )
        if started:
            self._log_msg(f"Updating slot {slot} from file: {path}")

    def _save_script_file(self):
        name = self._script_file_name()
        if name:
            self._send(f'/script/save "{name}"')

    def _delete_script_file(self):
        name = self._script_file_name()
        if name:
            self._send(f'/script/delete "{name}"')

    def _list_script_files(self):
        self._script_files = []
        self._set_script_file_choices()
        self._send("/script/files")

    def _list_bank_slots(self):
        self._send("/script/bank/list")

    def _on_text_brightness(self):
        val = self._text_brightness_var.get()
        self._text_bright_label.config(text=str(val))
        if self._loading: return
        self._send(f"{self._disp_prefix()}/text/brightness {val}")

    def _on_particle_brightness(self):
        if self._loading: return
        val = self._particle_brightness_var.get()
        self._send(f"{self._disp_prefix()}/particles/brightness {val}")

    def _pcolor_hex(self):
        return f"#{self._pcolor[0]:02x}{self._pcolor[1]:02x}{self._pcolor[2]:02x}"

    def _pick_particle_color(self):
        result = colorchooser.askcolor(initialcolor=self._pcolor_hex())
        if result and result[0]:
            r, g, b = (int(c) for c in result[0])
            self._pcolor = (r, g, b)
            self._pcolor_preview.config(bg=self._pcolor_hex())
            self._send(f"{self._disp_prefix()}/particles/color {r} {g} {b}")

    def _parse_text_stack_input(self, text):
        raw = text.strip()
        if len(raw) >= 2 and raw[0] in "[{" and raw[-1] in "]}":
            raw = raw[1:-1].strip()
        if not raw:
            return []
        return [item.strip() for item in raw.split(",") if item.strip()]

    def _send_text(self):
        text = self._text_var.get().strip()
        if not text:
            return
        safe = text.replace('\\', '\\\\').replace('"', '\\"')
        self._send(f'{self._disp_prefix()}/text/stack "{safe}"')
        self._ts_listbox.delete(0, "end")
        for item in self._parse_text_stack_input(text):
            self._ts_listbox.insert("end", item)
        self._schedule_display_sync(log=False)

    def _pick_color(self):
        result = colorchooser.askcolor(initialcolor=self._color_hex())
        if result and result[0]:
            r, g, b = (int(c) for c in result[0])
            self._r_var.set(r)
            self._g_var.set(g)
            self._b_var.set(b)
            self._apply_color(r, g, b)

    def _on_color_slider(self):
        r, g, b = self._r_var.get(), self._g_var.get(), self._b_var.get()
        self._apply_color(r, g, b)

    def _apply_color(self, r, g, b):
        self._color = (r, g, b)
        self._color_preview.config(bg=self._color_hex())
        if self._loading: return
        self._send(f"{self._disp_prefix()}/color {r} {g} {b}")

    def _color_hex(self):
        return f"#{self._color[0]:02x}{self._color[1]:02x}{self._color[2]:02x}"

    def _on_brightness(self):
        val = self._brightness_var.get()
        self._bright_label.config(text=str(val))
        self._send(f"/brightness {val}")

    def _on_scrollspeed(self):
        val = self._scrollspeed_var.get()
        self._scrollspeed_label.config(text=str(val))
        self._send(f"/scrollspeed {val}")

    def _on_scrollcontinuous(self):
        self._send(f"/scrollcontinuous {1 if self._scrollcont_var.get() else 0}")

    def _send_particle_config(self):
        """Debounced: coalesces rapid slider drags into one send."""
        # Always update labels immediately (cheap, no serial)
        self._pgrav_label.config(text=f"{self._pgrav_var.get():.1f}")
        self._pradius_label.config(text=f"{self._pradius_var.get():.2f}")
        self._psigma_label.config(text=f"{self._psigma_var.get():.1f}")
        self._ptemp_label.config(text=f"{self._ptemp_var.get():.2f}")
        self._pattract_label.config(text=f"{self._pattract_var.get():.2f}")
        self._pspring_str_label.config(text=f"{self._pspring_str_var.get():.2f}")
        self._pcoulomb_str_label.config(text=f"{self._pcoulomb_str_var.get():.2f}")
        self._pscaffold_str_label.config(text=f"{self._pscaffold_str_var.get():.2f}")
        if self._loading: return
        if self._throttle_id is not None:
            self.root.after_cancel(self._throttle_id)
        self._throttle_id = self.root.after(
            self.SEND_THROTTLE_MS, self._send_particle_config_now
        )

    def _send_particle_config_now(self):
        """Actually send the particle config over serial."""
        self._throttle_id = None
        count = self._pcount_var.get()
        renderms = self._prenderms_var.get()
        grav = self._pgrav_var.get()
        elast = self._pelast_var.get()
        welast = self._pwelast_var.get()
        radius = self._pradius_var.get()
        render = self._prender_var.get()
        sigma = self._psigma_var.get()
        temp = self._ptemp_var.get()
        attract = self._pattract_var.get()
        att_range = self._pattrange_var.get()
        grav_en = 1 if self._pgrav_enabled.get() else 0
        substep = self._psubstep_var.get()
        damping = self._pdamping_var.get()
        wavelength = self._pwavelength_var.get()
        speedcol = 1 if self._pspeedcolor_var.get() else 0
        spring_str = self._pspring_str_var.get()
        spring_range = self._pspring_range_var.get()
        spring_en = 1 if self._pspring_enabled.get() else 0
        coulomb_str = self._pcoulomb_str_var.get()
        coulomb_range = self._pcoulomb_range_var.get()
        coulomb_en = 1 if self._pcoulomb_enabled.get() else 0
        scaffold_str = self._pscaffold_str_var.get()
        scaffold_range = self._pscaffold_range_var.get()
        scaffold_en = 1 if self._pscaffold_enabled.get() else 0
        collision_en = 1 if self._pcollision_enabled.get() else 0
        self._send(
            f"{self._disp_prefix()}/particles {count} {renderms} {grav:.2f} {elast:.2f} {welast:.2f}"
            f" {radius:.2f} {render} {sigma:.2f} {temp:.2f}"
            f" {attract:.2f} {att_range:.2f} {grav_en} {substep} {damping:.4f} {wavelength:.2f}"
            f" {speedcol} {spring_str:.2f} {spring_range:.2f} {spring_en}"
            f" {coulomb_str:.2f} {coulomb_range:.2f} {coulomb_en}"
            f" {scaffold_str:.2f} {scaffold_range:.2f} {scaffold_en}"
            f" {collision_en}"
        )

    def _clear_display(self):
        self._send(f"{self._disp_prefix()}/clear")

    def _clear_all(self):
        self._send("/clearall")

    def _text_to_particles(self):
        self._send(f"{self._disp_prefix()}/text2particles")
        # Reflect state change: particles on, physics paused, scaffold spring on, collision off
        self._particles_enabled.set(True)
        self._physics_paused.set(True)
        self._pcollision_enabled.set(False)
        self._pscaffold_enabled.set(True)
        self._pscaffold_str_var.set(1.0)
        self._pscaffold_str_label.config(text="1.00")
        self._pscaffold_range_var.set(10.0)

    def _on_text2particles_count(self, count):
        """Called on main thread when firmware reports TEXT2PARTICLES <count>."""
        self._pcount_var.set(count)
        # Also update the glow params to match what the firmware set
        self._prender_var.set(4)         # RENDER_GLOW
        self._psigma_var.set(0.6)
        self._psigma_label.config(text="0.6")
        self._pradius_var.set(0.35)
        self._pradius_label.config(text="0.35")

    def _clear_particles(self):
        self._send(f"{self._disp_prefix()}/particles/clear")
        self._pcount_var.set(0)
        self._particles_enabled.set(False)
        self._physics_paused.set(False)
        self._schedule_display_sync(log=False)

    def _add_particle(self):
        self._send(f"{self._disp_prefix()}/particles/add")
        self._particles_enabled.set(True)
        self._pcount_var.set(min(64, self._pcount_var.get() + 1))
        self._schedule_display_sync(log=False)

    def _toggle_physics_pause(self):
        if self._loading:
            return
        val = 1 if self._physics_paused.get() else 0
        self._send(f"{self._disp_prefix()}/particles/pause {val}")

    def _send_transform(self):
        """Send the full view transform (debounced via particle config throttle)."""
        self._protate_label.config(text=f"{self._protate_var.get():.1f}")
        self._pscale_label.config(text=f"{self._pscale_var.get():.2f}")
        if self._loading:
            return
        angle = self._protate_var.get()
        s = self._pscale_var.get()
        tx = self._ptx_var.get()
        ty = self._pty_var.get()
        self._send(
            f"{self._disp_prefix()}/particles/transform"
            f" {angle:.1f} {s:.2f} {s:.2f} {tx:.1f} {ty:.1f}"
        )

    def _reset_transform(self):
        self._protate_var.set(0.0)
        self._pscale_var.set(1.0)
        self._ptx_var.set(0.0)
        self._pty_var.set(0.0)
        self._protate_label.config(text="0.0")
        self._pscale_label.config(text="1.00")
        self._send(f"{self._disp_prefix()}/particles/resettransform")

    def _raster_scan(self):
        self._send("/rasterscan 20")

    def _reset_defaults(self):
        self._send("/defaults")
        # Reset local per-display state to defaults
        self._display_states = {n: self._default_display_state() for n in range(1, 7)}
        self._restore_display_state(self._display_states[self._current_display])
        self._brightness_var.set(10)
        self._bright_label.config(text="10")
        self._scrollspeed_var.set(50)
        self._scrollspeed_label.config(text="50")
        self._scrollcont_var.set(False)
        self._log_msg("Sent /defaults — all params reset")

    # ── Text stack UI handlers ────────────────────────────────

    def _ts_push(self):
        text = self._text_var.get().strip()
        if not text:
            return
        self._ts_listbox.insert("end", text)
        self._send(f'{self._disp_prefix()}/text/push "{text}"')
        self._text_var.set("")

    def _ts_pop(self):
        if self._ts_listbox.size() == 0:
            return
        self._ts_listbox.delete("end")
        self._send(f"{self._disp_prefix()}/text/pop")

    def _ts_update_selected(self):
        sel = self._ts_listbox.curselection()
        if not sel:
            return
        idx = sel[0]
        text = self._text_var.get().strip()
        if not text:
            return
        self._ts_listbox.delete(idx)
        self._ts_listbox.insert(idx, text)
        self._send(f'{self._disp_prefix()}/text/set {idx} "{text}"')

    def _ts_clear(self):
        self._ts_listbox.delete(0, "end")
        self._send(f"{self._disp_prefix()}/text/clear")
        self._text_var.set("")

    # ── Presets (JSON) ────────────────────────────────────────

    def _gather_params(self):
        """Return all GUI parameters as a structured dict (new schema)."""
        # Snapshot current display into _display_states first
        self._display_states[self._current_display] = self._snapshot_display_state()
        cur = self._display_states[self._current_display]
        result = {
            "display": self._display_var.get(),
            "brightness": self._brightness_var.get(),
            "color": list(cur["color"]),
            "activeMode": cur["mode"],
            "textEnabled": cur["textEnabled"],
            "textBrightness": cur["textBrightness"],
            "particlesEnabled": cur["particlesEnabled"],
            "particleBrightness": cur["particleBrightness"],
            "particleColor": list(cur["particleColor"]),
            "textStack": cur["textStack"],
            "scrollSpeed": self._scrollspeed_var.get(),
            "scrollContinuous": self._scrollcont_var.get(),
            "modes": {
                "text": {"textIndex": 0},
                "scroll_up": {
                    "scrollStepMs": self._scrollspeed_var.get(),
                    "continuous": self._scrollcont_var.get(),
                },
                "scroll_down": {
                    "scrollStepMs": self._scrollspeed_var.get(),
                    "continuous": self._scrollcont_var.get(),
                },
                "particles": dict(cur["particles"]),
            },
            # Per-display states (all 6 displays)
            "allDisplays": {
                str(n): self._display_states[n] for n in range(1, 7)
            },
        }
        return result

    def _apply_params(self, d):
        """Set GUI variables from a parameter dict (supports old and new schema)."""
        self._loading = True
        try:
            if "brightness" in d:
                self._brightness_var.set(d["brightness"])
                self._bright_label.config(text=str(d["brightness"]))

            # Load per-display states if present (new format)
            if "allDisplays" in d:
                for key, state in d["allDisplays"].items():
                    n = int(key)
                    if 1 <= n <= 6:
                        # Merge with defaults so missing keys don't crash
                        merged = self._default_display_state()
                        merged.update({k: v for k, v in state.items() if k != "particles"})
                        if "particles" in state:
                            merged["particles"].update(state["particles"])
                        # Ensure tuple for colors
                        if isinstance(merged["color"], list):
                            merged["color"] = tuple(merged["color"])
                        if isinstance(merged["particleColor"], list):
                            merged["particleColor"] = tuple(merged["particleColor"])
                        self._display_states[n] = merged

            if "display" in d:
                target = d["display"]
                self._current_display = target
                self._display_var.set(target)
            else:
                target = self._current_display

            # If no allDisplays, load flat params into current display (old format compat)
            if "allDisplays" not in d:
                if "color" in d and len(d["color"]) == 3:
                    r, g, b = d["color"]
                    self._r_var.set(r); self._g_var.set(g); self._b_var.set(b)
                    self._color = (r, g, b)
                    self._color_preview.config(bg=self._color_hex())
                if "activeMode" in d:
                    mode = d["activeMode"]
                    if mode == "particles":
                        self._mode_var.set("text")
                        self._particles_enabled.set(True)
                    else:
                        self._mode_var.set(mode)
                elif "mode" in d:
                    self._mode_var.set(d["mode"])
                if "animationSlot" in d:
                    self._animation_slot_var.set(d["animationSlot"])
                    self._current_animation_name_var.set(d.get("animationName", self._slot_name(d["animationSlot"])))
                elif "animationId" in d:
                    self._animation_slot_var.set(d["animationId"])
                    self._current_animation_name_var.set(d.get("animationName", self._slot_name(d["animationId"])))
                if "particlesEnabled" in d:
                    self._particles_enabled.set(d["particlesEnabled"])
                if "textEnabled" in d:
                    self._text_enabled.set(d["textEnabled"])
                if "textBrightness" in d:
                    self._text_brightness_var.set(d["textBrightness"])
                    self._text_bright_label.config(text=str(d["textBrightness"]))
                if "particleBrightness" in d:
                    self._particle_brightness_var.set(d["particleBrightness"])
                if "particleColor" in d and len(d["particleColor"]) == 3:
                    self._pcolor = tuple(d["particleColor"])
                    self._pcolor_preview.config(bg=self._pcolor_hex())
                if "textStack" in d:
                    self._ts_listbox.delete(0, "end")
                    for item in d["textStack"]:
                        self._ts_listbox.insert("end", item)

                modes = d.get("modes", {})
                if modes:
                    scroll = modes.get("scroll_up", modes.get("scroll_down", {}))
                    if "scrollStepMs" in scroll:
                        self._scrollspeed_var.set(scroll["scrollStepMs"])
                        self._scrollspeed_label.config(text=str(scroll["scrollStepMs"]))
                    if "continuous" in scroll:
                        self._scrollcont_var.set(scroll["continuous"])
                    p = modes.get("particles", {})
                else:
                    if "scrollspeed" in d:
                        self._scrollspeed_var.set(d["scrollspeed"])
                        self._scrollspeed_label.config(text=str(d["scrollspeed"]))
                    p = d.get("particles", {})

                if "count" in p: self._pcount_var.set(p["count"])
                if "renderMs" in p: self._prenderms_var.set(p["renderMs"])
                if "substepMs" in p: self._psubstep_var.set(p["substepMs"])
                if "gravityScale" in p: self._pgrav_var.set(p["gravityScale"])
                if "gravityEnabled" in p: self._pgrav_enabled.set(p["gravityEnabled"])
                if "collisionEnabled" in p: self._pcollision_enabled.set(p["collisionEnabled"])
                if "elasticity" in p: self._pelast_var.set(p["elasticity"])
                if "wallElasticity" in p: self._pwelast_var.set(p["wallElasticity"])
                if "damping" in p: self._pdamping_var.set(p["damping"])
                if "radius" in p: self._pradius_var.set(p["radius"])
                if "renderStyle" in p: self._prender_var.set(p["renderStyle"])
                if "glowSigma" in p: self._psigma_var.set(p["glowSigma"])
                if "temperature" in p: self._ptemp_var.set(p["temperature"])
                if "attractStrength" in p: self._pattract_var.set(p["attractStrength"])
                if "attractRange" in p: self._pattrange_var.set(p["attractRange"])
                if "glowWavelength" in p: self._pwavelength_var.set(p["glowWavelength"])
                if "speedColor" in p: self._pspeedcolor_var.set(p["speedColor"])
                if "springStrength" in p: self._pspring_str_var.set(p["springStrength"])
                if "springRange" in p: self._pspring_range_var.set(p["springRange"])
                if "springEnabled" in p: self._pspring_enabled.set(p["springEnabled"])
                if "coulombStrength" in p: self._pcoulomb_str_var.set(p["coulombStrength"])
                if "coulombRange" in p: self._pcoulomb_range_var.set(p["coulombRange"])
                if "coulombEnabled" in p: self._pcoulomb_enabled.set(p["coulombEnabled"])
                if "scaffoldStrength" in p: self._pscaffold_str_var.set(p["scaffoldStrength"])
                if "scaffoldRange" in p: self._pscaffold_range_var.set(p["scaffoldRange"])
                if "scaffoldEnabled" in p: self._pscaffold_enabled.set(p["scaffoldEnabled"])

            # Global scroll settings
            if "scrollSpeed" in d:
                self._scrollspeed_var.set(d["scrollSpeed"])
                self._scrollspeed_label.config(text=str(d["scrollSpeed"]))
            if "scrollContinuous" in d:
                self._scrollcont_var.set(d["scrollContinuous"])
        finally:
            self._loading = False

        # Restore active display widgets from its state
        if target in self._display_states:
            self._restore_display_state(self._display_states[target])

        # Refresh value labels
        self._pgrav_label.config(text=f"{self._pgrav_var.get():.1f}")
        self._pradius_label.config(text=f"{self._pradius_var.get():.2f}")
        self._psigma_label.config(text=f"{self._psigma_var.get():.1f}")
        self._ptemp_label.config(text=f"{self._ptemp_var.get():.2f}")
        self._pattract_label.config(text=f"{self._pattract_var.get():.2f}")
        self._pspring_str_label.config(text=f"{self._pspring_str_var.get():.2f}")
        self._pwavelength_label.config(text=f"{self._pwavelength_var.get():.1f}")

    def _save_preset(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".json",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
            initialdir=os.path.dirname(__file__),
            title="Save parameter file",
        )
        if not path:
            return
        with open(path, "w") as f:
            json.dump(self._gather_params(), f, indent=2)
        self._log_msg(f"Parameter file saved → {path}")

    def _load_preset(self):
        path = filedialog.askopenfilename(
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
            initialdir=os.path.dirname(__file__),
            title="Load parameter file",
        )
        if not path:
            return
        with open(path, "r") as f:
            d = json.load(f)
        self._apply_params(d)
        self._log_msg(f"Parameter file loaded ← {path}")

        # Push global settings to device
        self._send(f"/brightness {self._brightness_var.get()}")
        self._send(f"/scrollspeed {self._scrollspeed_var.get()}")
        self._send(f"/scrollcontinuous {1 if self._scrollcont_var.get() else 0}")

        # Push per-display state to device
        for n in range(1, 7):
            st = self._display_states[n]
            prefix = f"/display/{n}"
            self._send(f"{prefix}/text/enable {1 if st['textEnabled'] else 0}")
            self._send(f"{prefix}/particles/enable {1 if st['particlesEnabled'] else 0}")
            self._send(f"{prefix}/mode {st['mode']}")
            self._send(f"{prefix}/animation {st.get('animationSlot', st.get('animationId', 0))}")
            r, g, b = st["color"]
            self._send(f"{prefix}/color {r} {g} {b}")
            self._send(f"{prefix}/text/brightness {st['textBrightness']}")
            self._send(f"{prefix}/particles/brightness {st['particleBrightness']}")
            pr, pg, pb = st["particleColor"]
            self._send(f"{prefix}/particles/color {pr} {pg} {pb}")
            # Text stack
            self._send(f"{prefix}/text/clear")
            for item in st["textStack"]:
                self._send(f'{prefix}/text/push "{item}"')
            # Particle config
            p = st["particles"]
            self._send(
                f"{prefix}/particles {p['count']} {p['renderMs']} {p['gravityScale']:.2f}"
                f" {p['elasticity']:.2f} {p['wallElasticity']:.2f}"
                f" {p['radius']:.2f} {p['renderStyle']} {p['glowSigma']:.2f}"
                f" {p['temperature']:.2f} {p['attractStrength']:.2f} {p['attractRange']:.2f}"
                f" {1 if p['gravityEnabled'] else 0} {p['substepMs']} {p['damping']:.4f}"
                f" {p['glowWavelength']:.2f} {1 if p['speedColor'] else 0}"
                f" {p.get('springStrength', 0.0):.2f} {p.get('springRange', 5.0):.2f}"
                f" {1 if p.get('springEnabled', False) else 0}"
                f" {p.get('coulombStrength', 0.0):.2f} {p.get('coulombRange', 10.0):.2f}"
                f" {1 if p.get('coulombEnabled', False) else 0}"
                f" {p.get('scaffoldStrength', 0.0):.2f} {p.get('scaffoldRange', 10.0):.2f}"
                f" {1 if p.get('scaffoldEnabled', False) else 0}"
                f" {1 if p.get('collisionEnabled', True) else 0}"
            )

    # ── ESP32 NVS banks ────────────────────────────────────────

    def _bank_value(self):
        try:
            b = int(self._esp_bank_var.get())
        except (TypeError, ValueError):
            b = 1
        return max(1, min(5, b))

    def _save_to_esp_bank(self):
        b = self._bank_value()
        self._send(f"/saveparams {b}")
        self._log_msg(f"Sent /saveparams {b} to ESP32")

    def _load_from_esp_bank(self):
        b = self._bank_value()
        self._send(f"/loadparams {b}")
        self._log_msg(f"Sent /loadparams {b} to ESP32")

    def _set_startup_bank(self):
        b = self._bank_value()
        self._send(f"/startupbank {b}")
        self._log_msg(f"Sent /startupbank {b} to ESP32")

    def _query_startup_bank(self):
        self._send("/startupbank")
        self._log_msg("Sent /startupbank (query)")

    def _send_raw(self):
        cmd = self._raw_var.get()
        if cmd:
            self._send(cmd)
            self._raw_var.set("")

    # ── Log ───────────────────────────────────────────────────

    def _log_msg(self, msg):
        self._log.config(state="normal")
        self._log.insert("end", msg + "\n")
        self._log.see("end")
        self._log.config(state="disabled")


# ── Main ──────────────────────────────────────────────────────

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_default_port()

    _check_macos_tk_runtime_or_exit()

    root = tk.Tk()
    root.geometry("1360x760")
    app = ScoreboardGUI(root, initial_port=port)

    # Auto-connect if a port was found/specified
    if port:
        root.after(100, app._connect)

    root.mainloop()


if __name__ == "__main__":
    main()
