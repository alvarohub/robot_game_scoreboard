#!/usr/bin/env python3
"""
gui_control.py — Interactive GUI for the Robot Game Scoreboard.

Sends serial text commands (same syntax as OSC addresses) over USB.
Requires:  pip install pyserial

Usage:
    python gui_control.py                  # auto-detect serial port
    python gui_control.py /dev/ttyACM0     # explicit port

Note: PlatformIO's bundled Python lacks tkinter.
      Use system or conda Python, e.g.:
        /opt/anaconda3/envs/ML/bin/python test/gui_control.py
"""

import sys
import json
import os
import time
import queue
import threading
import tkinter as tk
from tkinter import ttk, colorchooser, scrolledtext, filedialog
import serial
import serial.tools.list_ports

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

    def __init__(self, root, initial_port=None):
        self.root = root
        self.root.title("Scoreboard Control")
        self.ser = None
        self._reader_thread = None
        self._writer_thread = None
        self._stop_reader = threading.Event()
        self._write_queue = queue.Queue()
        self._throttle_id = None  # after-id for debounced slider send

        # Current colour (R, G, B)
        self._color = (255, 255, 255)

        self._build_ui(initial_port)

    # ── UI construction ───────────────────────────────────────

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

        # ── Layers & Mode frame ──────────────────────────────
        mode_f = ttk.LabelFrame(self.root, text="Layers & Mode")
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
        text_f = ttk.LabelFrame(self.root, text="Text / Stack")
        text_f.pack(fill="x", **pad)

        self._text_var = tk.StringVar()
        text_entry = ttk.Entry(text_f, textvariable=self._text_var, width=16)
        text_entry.grid(row=0, column=0, **pad)
        text_entry.bind("<Return>", lambda e: self._send_text())
        ttk.Button(text_f, text="Send", command=self._send_text).grid(row=0, column=1, **pad)
        ttk.Button(text_f, text="Push", command=self._ts_push).grid(row=0, column=2, **pad)
        ttk.Button(text_f, text="Pop", command=self._ts_pop).grid(row=0, column=3, **pad)
        ttk.Button(text_f, text="Clear stk", command=self._ts_clear).grid(row=0, column=4, **pad)

        self._scrollcont_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(text_f, text="Continuous", variable=self._scrollcont_var,
                        command=self._on_scrollcontinuous).grid(row=0, column=5, **pad)

        # Row 1: compact horizontal stack display
        ttk.Label(text_f, text="Stack:").grid(row=1, column=0, sticky="w", **pad)
        self._ts_listbox = tk.Listbox(text_f, height=2, width=50, font=("Menlo", 9))
        self._ts_listbox.grid(row=1, column=1, columnspan=4, sticky="ew", **pad)
        ttk.Button(text_f, text="Update", command=self._ts_update_selected).grid(row=1, column=5, **pad)
        text_f.columnconfigure(1, weight=1)

        # ── Text colour frame ─────────────────────────────────
        color_f = ttk.LabelFrame(self.root, text="Text Colour")
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
        bright_f = ttk.LabelFrame(self.root, text="Brightness")
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
        scroll_f = ttk.LabelFrame(self.root, text="Scroll Settings")
        scroll_f.pack(fill="x", **pad)

        ttk.Label(scroll_f, text="Speed (ms/px):").grid(row=0, column=0, **pad)
        self._scrollspeed_var = tk.IntVar(value=50)
        ttk.Scale(scroll_f, from_=5, to=200, variable=self._scrollspeed_var,
                  orient="horizontal", length=150,
                  command=lambda *_: self._on_scrollspeed()).grid(row=0, column=1, **pad)
        self._scrollspeed_label = ttk.Label(scroll_f, text="50")
        self._scrollspeed_label.grid(row=0, column=2, **pad)

        # ── Particle settings frame ──────────────────────────
        part_f = ttk.LabelFrame(self.root, text="Particle Settings")
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
        ttk.Spinbox(part_f, from_=1, to=64, textvariable=self._pcount_var, width=4,
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
        # ── Actions frame ────────────────────────────────────
        act_f = ttk.LabelFrame(self.root, text="Actions")
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

        # ── Presets frame (JSON save/load + ESP32 NVS) ────────
        preset_f = ttk.LabelFrame(self.root, text="Presets")
        preset_f.pack(fill="x", **pad)

        ttk.Button(preset_f, text="Save to JSON…", command=self._save_preset).grid(
            row=0, column=0, **pad
        )
        ttk.Button(preset_f, text="Load from JSON…", command=self._load_preset).grid(
            row=0, column=1, **pad
        )
        ttk.Separator(preset_f, orient="vertical").grid(row=0, column=2, sticky="ns", padx=6)
        ttk.Button(preset_f, text="Save to ESP32", command=self._save_to_esp).grid(
            row=0, column=3, **pad
        )
        ttk.Button(preset_f, text="Load from ESP32", command=self._load_from_esp).grid(
            row=0, column=4, **pad
        )

        # ── Raw command ──────────────────────────────────────
        raw_f = ttk.LabelFrame(self.root, text="Raw Command")
        raw_f.pack(fill="x", **pad)

        self._raw_var = tk.StringVar()
        raw_entry = ttk.Entry(raw_f, textvariable=self._raw_var, width=40)
        raw_entry.grid(row=0, column=0, **pad)
        raw_entry.bind("<Return>", lambda e: self._send_raw())
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
        except serial.SerialException as e:
            self._log_msg(f"Connection error: {e}")

    def _disconnect(self):
        self._stop_reader.set()
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

    def _send(self, cmd):
        if not self.ser or not self.ser.is_open:
            self._log_msg("Not connected")
            return
        line = cmd.strip() + "\n"
        self._write_queue.put(line.encode("utf-8"))
        self._log_msg(f"→ {cmd}")

    def _disp_prefix(self):
        return f"/display/{self._display_var.get()}"

    def _on_mode_change(self):
        self._send(f"{self._disp_prefix()}/mode {self._mode_var.get()}")

    def _on_text_toggle(self):
        en = 1 if self._text_enabled.get() else 0
        self._send(f"{self._disp_prefix()}/text/enable {en}")

    def _on_particles_toggle(self):
        en = 1 if self._particles_enabled.get() else 0
        self._send(f"{self._disp_prefix()}/particles/enable {en}")

    def _on_text_brightness(self):
        val = self._text_brightness_var.get()
        self._text_bright_label.config(text=str(val))
        self._send(f"{self._disp_prefix()}/text/brightness {val}")

    def _on_particle_brightness(self):
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

    def _send_text(self):
        text = self._text_var.get()
        if text:
            self._send(f'{self._disp_prefix()} "{text}"')
            # Also add to the GUI stack display (firmware pushes automatically)
            self._ts_listbox.insert("end", text)
            self._text_var.set("")

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
        if self._throttle_id is not None:
            self.root.after_cancel(self._throttle_id)
        self._throttle_id = self.root.after(
            self.SEND_THROTTLE_MS, self._send_particle_config_now
        )
        # Always update labels immediately (cheap, no serial)
        self._pgrav_label.config(text=f"{self._pgrav_var.get():.1f}")
        self._pradius_label.config(text=f"{self._pradius_var.get():.2f}")
        self._psigma_label.config(text=f"{self._psigma_var.get():.1f}")
        self._ptemp_label.config(text=f"{self._ptemp_var.get():.2f}")
        self._pattract_label.config(text=f"{self._pattract_var.get():.2f}")

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
        self._send(
            f"{self._disp_prefix()}/particles {count} {renderms} {grav:.2f} {elast:.2f} {welast:.2f}"
            f" {radius:.2f} {render} {sigma:.2f} {temp:.2f}"
            f" {attract:.2f} {att_range:.2f} {grav_en} {substep} {damping:.4f} {wavelength:.2f}"
            f" {speedcol}"
        )

    def _clear_display(self):
        self._send(f"{self._disp_prefix()}/clear")

    def _clear_all(self):
        self._send("/clearall")

    def _raster_scan(self):
        self._send("/rasterscan 20")

    def _reset_defaults(self):
        self._send("/defaults")
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

    # ── Presets (JSON) ────────────────────────────────────────

    def _gather_params(self):
        """Return all GUI parameters as a structured dict (new schema)."""
        return {
            "display": self._display_var.get(),
            "brightness": self._brightness_var.get(),
            "color": list(self._color),
            "activeMode": self._mode_var.get(),
            "textEnabled": self._text_enabled.get(),
            "textBrightness": self._text_brightness_var.get(),
            "particlesEnabled": self._particles_enabled.get(),
            "particleBrightness": self._particle_brightness_var.get(),
            "particleColor": list(self._pcolor),
            "textStack": list(self._ts_listbox.get(0, "end")),
            "modes": {
                "text": {
                    "textIndex": 0,
                },
                "scroll_up": {
                    "scrollStepMs": self._scrollspeed_var.get(),
                    "continuous": self._scrollcont_var.get(),
                },
                "scroll_down": {
                    "scrollStepMs": self._scrollspeed_var.get(),
                    "continuous": self._scrollcont_var.get(),
                },
                "particles": {
                    "count": self._pcount_var.get(),
                    "renderMs": self._prenderms_var.get(),
                    "substepMs": self._psubstep_var.get(),
                    "gravityScale": self._pgrav_var.get(),
                    "gravityEnabled": self._pgrav_enabled.get(),
                    "elasticity": self._pelast_var.get(),
                    "wallElasticity": self._pwelast_var.get(),
                    "damping": self._pdamping_var.get(),
                    "radius": self._pradius_var.get(),
                    "renderStyle": self._prender_var.get(),
                    "glowSigma": self._psigma_var.get(),
                    "temperature": self._ptemp_var.get(),
                    "attractStrength": self._pattract_var.get(),
                    "attractRange": self._pattrange_var.get(),
                    "glowWavelength": self._pwavelength_var.get(),
                    "speedColor": self._pspeedcolor_var.get(),
                },
            },
        }

    def _apply_params(self, d):
        """Set GUI variables from a parameter dict (supports old and new schema)."""
        if "display" in d:
            self._display_var.set(d["display"])
        if "brightness" in d:
            self._brightness_var.set(d["brightness"])
            self._bright_label.config(text=str(d["brightness"]))
        if "color" in d and len(d["color"]) == 3:
            r, g, b = d["color"]
            self._r_var.set(r); self._g_var.set(g); self._b_var.set(b)
            self._color = (r, g, b)
            self._color_preview.config(bg=self._color_hex())

        # New schema: "activeMode" + "modes" block
        if "activeMode" in d:
            mode = d["activeMode"]
            # Backwards compat: old "particles" mode → text + particlesEnabled
            if mode == "particles":
                self._mode_var.set("text")
                self._particles_enabled.set(True)
            else:
                self._mode_var.set(mode)
        elif "mode" in d:
            self._mode_var.set(d["mode"])  # old schema compat
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

        # Text stack
        if "textStack" in d:
            self._ts_listbox.delete(0, "end")
            for item in d["textStack"]:
                self._ts_listbox.insert("end", item)

        # New schema: per-mode params
        modes = d.get("modes", {})
        if modes:
            # Scroll settings (shared between scroll_up/scroll_down)
            scroll = modes.get("scroll_up", modes.get("scroll_down", {}))
            if "scrollStepMs" in scroll:
                self._scrollspeed_var.set(scroll["scrollStepMs"])
                self._scrollspeed_label.config(text=str(scroll["scrollStepMs"]))
            if "continuous" in scroll:
                self._scrollcont_var.set(scroll["continuous"])

            # Particle settings
            p = modes.get("particles", {})
        else:
            # Old schema compat: flat params
            if "scrollspeed" in d:
                self._scrollspeed_var.set(d["scrollspeed"])
                self._scrollspeed_label.config(text=str(d["scrollspeed"]))
            p = d.get("particles", {})

        if "count" in p: self._pcount_var.set(p["count"])
        if "renderMs" in p: self._prenderms_var.set(p["renderMs"])
        if "substepMs" in p: self._psubstep_var.set(p["substepMs"])
        if "gravityScale" in p: self._pgrav_var.set(p["gravityScale"])
        if "gravityEnabled" in p: self._pgrav_enabled.set(p["gravityEnabled"])
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
        # Refresh value labels
        self._pgrav_label.config(text=f"{self._pgrav_var.get():.1f}")
        self._pradius_label.config(text=f"{self._pradius_var.get():.2f}")
        self._psigma_label.config(text=f"{self._psigma_var.get():.1f}")
        self._ptemp_label.config(text=f"{self._ptemp_var.get():.2f}")
        self._pattract_label.config(text=f"{self._pattract_var.get():.2f}")
        self._pwavelength_label.config(text=f"{self._pwavelength_var.get():.1f}")

    def _save_preset(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".json",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
            initialdir=os.path.dirname(__file__),
            title="Save preset",
        )
        if not path:
            return
        with open(path, "w") as f:
            json.dump(self._gather_params(), f, indent=2)
        self._log_msg(f"Preset saved → {path}")

    def _load_preset(self):
        path = filedialog.askopenfilename(
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
            initialdir=os.path.dirname(__file__),
            title="Load preset",
        )
        if not path:
            return
        with open(path, "r") as f:
            d = json.load(f)
        self._apply_params(d)
        self._log_msg(f"Preset loaded ← {path}")
        # Push all values to the device
        self._on_brightness()
        self._on_color_slider()
        self._on_scrollspeed()
        self._on_scrollcontinuous()
        self._on_mode_change()
        self._on_text_toggle()
        self._on_particles_toggle()
        self._on_text_brightness()
        self._on_particle_brightness()
        # Push particle colour
        r, g, b = self._pcolor
        self._send(f"{self._disp_prefix()}/particles/color {r} {g} {b}")
        self._send_particle_config()
        # Push text stack to device
        self._send(f"{self._disp_prefix()}/text/clear")
        for i in range(self._ts_listbox.size()):
            text = self._ts_listbox.get(i)
            self._send(f'{self._disp_prefix()}/text/push "{text}"')

    # ── ESP32 NVS save / load ─────────────────────────────────

    def _save_to_esp(self):
        self._send("/saveparams")
        self._log_msg("Sent /saveparams to ESP32")

    def _load_from_esp(self):
        self._send("/loadparams")
        self._log_msg("Sent /loadparams to ESP32")

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

    root = tk.Tk()
    root.geometry("720x960")
    app = ScoreboardGUI(root, initial_port=port)

    # Auto-connect if a port was found/specified
    if port:
        root.after(100, app._connect)

    root.mainloop()


if __name__ == "__main__":
    main()
