// Display presets and the f1_res.ini writer, shared by index.html and the
// command line (see the bottom of this file).

const DEFAULTS = {
  display: "fullscreen", // fullscreen | borderless | window
  resolution: "auto", // auto | original | custom
  width: 1280,
  height: 800,
  scale: 2, // 1, 2, 3
  keepDesktopMode: true,
  fit: 0, // 0 keep shape, 1 stretch, 2 whole pixels
  smooth: false,
  hud: "bar", // bar | dock
  sideArt: 1, // 0 none, 1 metal, 2 leather
  sidesFromEdges: false,
  menu: 1, // 0 original, 1 hi-res, 2 scaled, 3 stretched
  movies: 1, // 0 original, 1 fit, 2 stretch
};

const PRESETS = [
  {
    id: "fill-classic",
    name: "Fill the screen",
    about: "Game world covers the whole screen, UI close to its original size.",
    settings: {},
  },
  {
    id: "fill-dock",
    name: "Fill the screen, docked HUD",
    about: "Same, with the map running under and beside a floating HUD.",
    settings: { hud: "dock" },
  },
  {
    id: "widest",
    name: "Widest view",
    about: "Native resolution at 1×: the most map on screen, small UI.",
    settings: { scale: 1 },
  },
  {
    id: "original-fill",
    name: "Original 640×480, full screen",
    about: "The original resolution stretched edge to edge. Giant UI, no bars.",
    settings: { resolution: "original", scale: 1, fit: 1, menu: 0, movies: 2 },
  },
  {
    id: "original-4x3",
    name: "Original 640×480, 4:3",
    about: "Original resolution with its shape kept, bars left and right.",
    settings: { resolution: "original", scale: 1, fit: 0, menu: 0, movies: 2 },
  },
  {
    id: "original-pixel",
    name: "Original 640×480, pixel-perfect",
    about: "Every game pixel drawn as an exact block of screen pixels.",
    settings: { resolution: "original", scale: 1, fit: 2, menu: 0, movies: 2 },
  },
  {
    id: "legacy",
    name: "Legacy 640×480 mode switch",
    about: "Switches the display to the closest real mode, like the 1997 game.",
    settings: { resolution: "original", scale: 1, keepDesktopMode: false, fit: 0, menu: 0, movies: 0, sideArt: 0 },
  },
  {
    id: "window",
    name: "Window",
    about: "A 1280×800 window with the original 640×480 game inside.",
    settings: { display: "window", resolution: "custom", width: 1280, height: 800, scale: 2, fit: 2 },
  },
];

function presetSettings(preset) {
  return Object.assign({}, DEFAULTS, preset.settings);
}

// Settings object -> f1_res.ini text. Only keys the engine reads are written.
function toIni(s, title) {
  let w = 0;
  let h = 0;
  if (s.resolution === "original") {
    w = 640 * s.scale;
    h = 480 * s.scale;
  } else if (s.resolution === "custom") {
    w = s.width;
    h = s.height;
  }
  const lines = [];
  if (title) lines.push(`; ${title}`);
  lines.push("; Fallout CE display settings, read at game start.", "");
  lines.push("[MAIN]");
  lines.push(`SCR_WIDTH=${w}`, `SCR_HEIGHT=${h}`);
  lines.push(`WINDOWED=${s.display === "window" ? 1 : 0}`);
  lines.push(`WINDOWED_FULLSCREEN=${s.display === "borderless" ? 1 : 0}`);
  lines.push(`KEEP_DESKTOP_MODE=${s.keepDesktopMode ? 1 : 0}`);
  lines.push(`SCALE_2X=${s.scale - 1}`);
  lines.push(`SCREEN_FIT=${s.fit}`);
  lines.push(`SCALE_FILTER=${s.smooth ? 1 : 0}`, "");
  lines.push("[MOVIES]", `MOVIE_SIZE=${s.movies}`, "");
  lines.push("[MAINMENU]", `MAIN_MENU_SIZE=${s.menu}`, "");
  lines.push("[IFACE]");
  lines.push(`IFACE_BAR_MODE=${s.hud === "dock" ? 1 : 0}`);
  lines.push(`IFACE_BAR_SIDE_ART=${s.sideArt}`);
  lines.push(`IFACE_BAR_SIDES_ORI=${s.sidesFromEdges ? 1 : 0}`, "");
  return lines.join("\n");
}

// `node presets.js <dir>` writes every preset to <dir>/display-presets and the
// first one to <dir>/f1_res.ini (point it at the game folder).
if (typeof module !== "undefined" && require.main === module) {
  const fs = require("fs");
  const path = require("path");
  const target = process.argv[2];
  if (!target) {
    console.error("usage: node presets.js /path/to/Fallout");
    process.exit(1);
  }
  const dir = path.join(target, "display-presets");
  fs.mkdirSync(dir, { recursive: true });
  PRESETS.forEach((p, i) => {
    const file = path.join(dir, `${i + 1}-${p.id}.ini`);
    fs.writeFileSync(file, toIni(presetSettings(p), p.name));
    console.log(path.relative(target, file));
  });
  fs.writeFileSync(path.join(target, "f1_res.ini"), toIni(presetSettings(PRESETS[0]), PRESETS[0].name));
}

if (typeof module !== "undefined") module.exports = { DEFAULTS, PRESETS, presetSettings, toIni };
