// SNES PPU (S-PPU1/2) scanline renderer with widescreen extension support.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include "hfr_match.h"

// Max extra pixels on each side: a game module may split up to twice its widest extension to one side (the view
// stopped at a level's edge: Aladdin's 140 at 21:9 square pixels gives 280).
constexpr int kMaxExt = 288;
constexpr int kLineW = 256 + 2 * kMaxExt;     // framebuffer line width
constexpr int kFrameH = 240;

// Horizontal remap for a layer on one scanline (used to re-anchor HUDs).
// For output x in [dst0,dst1): source x = src0 + (x - dst0)  (or modulo period)
struct PpuRemapSeg {
  int16_t dst0, dst1;
  int16_t src0;
  int16_t period;  // 0 = linear copy, >0 = repeat source span of this width, -1 = reversed (src0 down)
};
struct PpuLayerRemap {
  uint8_t count = 0;  // 0 = no remap
  PpuRemapSeg seg[8];
};
// Sprites whose top-left (screen coords) lies in the zone get shifted by dx.
struct PpuObjZone {
  int16_t x0, y0, x1, y1;
  int16_t dx;
};

class Ppu;
class PpuHooks {
public:
  virtual ~PpuHooks() = default;
  // Called before each visible line is rendered. `row` is 0-based output row.
  virtual void beginLine(Ppu& ppu, int row) {}
  // Called when a BG layer fetches a tilemap entry in the widescreen margin.
  // Return kWideVram to use VRAM, kWideEntry to use *entry, kWideBlank for a
  // transparent tile.
  enum { kWideVram = 0, kWideEntry = 1, kWideBlank = 2 };
  virtual int fetchWideTile(Ppu& ppu, int layer, int mapX, int mapY, int screenX, uint16_t* entry) {
    return kWideVram;
  }
  // Called for every VRAM word write (CPU or DMA).
  virtual void onVramWrite(Ppu& ppu, uint16_t adr) {}
};

class Ppu {
public:
  Ppu();
  void reset(bool hard);

  uint8_t read(uint8_t reg, uint8_t openBus);
  void write(uint8_t reg, uint8_t val);

  void frameStart();
  void vblankStart();
  void renderLine(int line);  // line = V counter (1..239)
  void latchCounters(uint16_t h, uint16_t v) {
    hCount_ = h;
    vCount_ = v;
    countersLatched_ = true;
  }
  bool forcedBlank() const { return forcedBlank_; }
  bool overscan() const { return overscan_; }

  // ---- widescreen / enhancement parameters
  // Active extension on each side (0..kMaxExt). extL + extR must be < 256 so
  // 9-bit sprite X coordinates stay unambiguous.
  int extL = 0, extR = 0;
  void setExt(int l, int r) {
    extL = l < 0 ? 0 : (l > kMaxExt ? kMaxExt : l);
    extR = r < 0 ? 0 : (r > kMaxExt ? kMaxExt : r);
  }
  bool noSpriteLimit = false;
  int wideWindowMode = 0;    // 0 = extend edge values, 1 = outside of windows
  PpuHooks* hooks = nullptr;
  PpuLayerRemap layerRemap[5];  // per line, filled by hooks (BG1-4, OBJ unused)
  PpuObjZone objZones[8];
  int objZoneCount = 0;
  // Per-OBJ-palette color overrides (character color variants).
  uint8_t objPalOverrideMask = 0;
  uint16_t objPalOverride[8][16] = {};
  bool hideWideBg[4] = {};   // hide BG layer pixels in the margins
  int16_t bgHofsAdj[4] = {};  // extra horizontal scroll for this line (per BG layer)
  bool wideFetchAll[4] = {};  // ask fetchWideTile for every pixel of the layer, not only margins
  // Per line: a layer's margin pixels drawn from this map and scroll instead (a raster band pointing the layer at other
  // content on the game's 256 px: beside it, the level's own map).
  struct WideAlt {
    bool on = false;
    uint16_t mapBase = 0, hofs = 0, vofs = 0;
  };
  WideAlt wideAlt[4];
  uint8_t debugLayers = 0x1f;  // debugging: layers to draw (bit 0-3 BG1-4, bit 4 OBJ)
  // Debugging (analysis tools): each rendered row's BG1 / BG2 scroll registers and the hook's bgHofsAdj for them.
  struct LineRegs {
    uint16_t hofs[2], vofs[2];
    int16_t adj[2];
  };
  LineRegs lineRegs[kFrameH] = {};
  bool hideWideObj = false;
  bool maskWide = false;     // black out margins for this line
  // Margin pixels outside [wideShowLo, wideShowHi) are black on this line (a
  // screen wipe the game draws only on its 256 px, continued by a hook; never
  // the 256 px themselves).
  int wideShowLo = -kMaxExt, wideShowHi = 256 + kMaxExt;
  uint8_t wideShowKeep = 0;  // layers (bit 0-3 BG1-4, 4 OBJ) still shown outside [wideShowLo, wideShowHi) (a status bar)
  // BG layers (bit 0-3) shown mirrored about the span's edges outside [wideShowLo, wideShowHi) on this line, instead
  // of black (past a level's end: the level reflected there). The other layers there are hidden, the kept ones stay.
  uint8_t wideMirror = 0;
  // ...reflected back and forth within [wideMirrorLo, wideMirrorHi) of that span (default: all of it; a strip of
  // the level at its edge that hides a layer behind it left out)
  int wideMirrorLo = -kMaxExt, wideMirrorHi = 256 + kMaxExt;
  // ...except the layers of wideWall (bit 0-3): on a side with a wall edge (wideWallL / wideWallR, screen x; kNoWall:
  // none) the layer's strip [edge, edge + wideWallW) inside it is reflected back and forth outward from the edge over
  // the whole side, and no other layer is drawn there (but wideShowKeep's): a wall the level's own edge goes on as.
  // An in-between picture's edge moves with the layer.
  static constexpr int kNoWall = -0x8000;
  uint8_t wideWall = 0;
  int wideWallL = kNoWall, wideWallR = kNoWall, wideWallW = 8;
  bool edgeMirror = true;    // setting: wideMirror is used (off: black past the span, as without it)
  // Pixels outside [wideDimLo, wideDimHi) are drawn at half brightness on this line (beyond where the game's camera
  // can go: a boss arena centred in a wider picture), except where the top layer is one of wideDimSpare's (a status
  // bar over the level).
  int wideDimLo = -kMaxExt, wideDimHi = 256 + kMaxExt;
  uint8_t wideDimSpare = 0;
  // How much darker, in 32nds (0-16: 16 is half brightness, the default; less while a dim fades in or out).
  int wideDimAmt = 16;
  int winShift = 0;          // windows moved right by this much on this line (a hook's overlay)
  // Per line: window w's true edges for the margins, in screen x (inclusive; may lie outside 0-255), used there instead
  // of the edge pixel's value (a window the game clips at its screen's edge, or keeps empty while its object is off it).
  struct WideWin {
    bool on = false;
    int lo = 0, hi = -1;
  };
  WideWin wideWin[2];
  // Per line (wideOptOn, reset each line; the table is kept): offset-per-tile (BG modes 2 / 4 / 6) in the margins.
  // The game's table (BG3's map) holds a vertical scroll for its screen's 8-px columns only, so a margin column took
  // the layer's register: an arch the game waves column by column stood flat beside its screen. With wideOptOn[l]
  // a margin column of layer l (col = floor((x + (hofs & 7)) / 8), as on the screen) takes wideOptV[l][col +
  // kWideOptOff] as its vertical scroll instead (clamped to the table).
  static constexpr int kWideOptOff = 48, kWideOptN = 128;
  bool wideOptOn[2] = {};
  int16_t wideOptV[2][kWideOptN] = {};
  // Per-sprite X relative to the 4:3 screen, set by the game layer for
  // sprites whose 9-bit OAM X is ambiguous in a wide view (it repeats every
  // 512 px: at 21:9, objects exist further out than that); used when set.
  int16_t objTrueX[128] = {};
  bool objTrueXSet[128] = {};
  void clearObjTrueX() {
    for (bool& b : objTrueXSet) b = false;
  }

  // Framebuffer: kLineW x kFrameH, centered at kMaxExt. Each pixel is the console's output: bits 0-14 the
  // BGR555 colour, bits 15-18 the master brightness (0 = black). to_rgb() converts one plainly.
  uint32_t frame[kLineW * kFrameH];
  static uint32_t to_rgb(uint32_t px) {
    const uint32_t br = px >> 15, r = px & 31, g = (px >> 5) & 31, b = (px >> 10) & 31;
    auto c8 = [br](uint32_t c) { return (((c << 3) | (c >> 2)) * br) / 15; };
    return (c8(r) << 16) | (c8(g) << 8) | c8(b);
  }
  int frameHeight() const { return overscan_ ? 239 : 224; }

  // ---- high frame rate: in-between pictures
  // With subFrames = K > 1 every line is also drawn into subFrame[k-1] (k = 1 .. K-1, same format as frame), k/K of
  // the way from the last frame to this one: each BG layer drawn again with its scroll part of the way back to the
  // last frame's on that line, as the picture shows it (the scroll with the hook's bgHofsAdj, less the left
  // extension: a view that slid moves back with it); the sprites part of the way back to where they were (matched by
  // their look, see objMoves). The margins are fetched from the hook at the place the layer is drawn (fetchWideTile
  // gets the screen x the pixel has at the frame's scroll; subDy tells it how far the layer is drawn down). A jump of
  // more than 32 px, a line in forced blank or black, another BG mode, map or width than the last frame's are cuts:
  // shown as they are. Lines the hook marks with a band (lineBand: raster bands, each scrolled its own way) are
  // matched with the last frame's line of the same band, and not moved vertically. Nothing the game reads changes.
  static constexpr int kMaxSubFrames = 4;
  int subFrames = 1;
  uint32_t subFrame[kMaxSubFrames - 1][kLineW * kFrameH];
  void hfrForget();  // nothing in between with the last frame (a state loaded, a reset)
  uint8_t lineBand[4] = {};  // per line, set by the hook: the raster band a BG layer shows on it (0 = none)
  bool subLine() const { return sub_; }
  int subDy(int l) const { return sub_ ? subVofs_[l & 3] : 0; }

  // Memory (public for debugging and game hooks)
  uint16_t vram[0x8000];
  uint16_t cgram[256];
  uint8_t oam[544];

  // Selected registers exposed for hooks
  uint8_t bgMode() const { return mode_; }
  uint16_t bgHofs(int l) const { return bg_[l].hofs; }
  uint16_t bgVofs(int l) const { return bg_[l].vofs; }
  uint16_t bgMapBase(int l) const { return bg_[l].mapBase; }
  uint8_t bgMapSize(int l) const { return bg_[l].mapSize; }
  uint8_t mainScreen() const { return tm_; }
  uint8_t subScreen() const { return ts_; }
  uint8_t brightness() const { return brightness_; }
  uint8_t windowLeft(int w) const { return winL_[w]; }
  uint8_t windowRight(int w) const { return winR_[w]; }
  uint8_t mainScreenWindow() const { return tmw_; }
  uint8_t windowSelect(int layer) const { return winSel_[layer]; }   // (0-3 BG1-4, 4 OBJ, 5 colour)
  uint8_t colorWindowSel() const { return cgwsel_; }
  uint8_t colorMathSel() const { return cgadsub_; }
  uint16_t fixedColor() const { return (uint16_t)(fixedR_ | (fixedG_ << 5) | (fixedB_ << 10)); }
  uint16_t bgCharBase(int l) const { return bg_[l].charBase; }
  uint16_t tileEntry(int l, int tx, int ty) const { return mapEntry(l, tx, ty); }
  bool bgBigTiles(int l) const { return bg_[l].bigTiles; }
  bool bg3Priority() const { return bg3Priority_; }
  int bgBpp(int l) const;
  uint16_t objBase() const { return objBase_; }
  uint8_t objSizeSel() const { return objSize_; }
  std::string debugString() const;
  // Render a whole BG tilemap (debug). out must hold w*h pixels.
  void debugRenderMap(int l, uint32_t* out, int* w, int* h) const;

  // Serialization
  static constexpr int kStateVersion = 1;
  size_t stateSize() const;
  void saveState(uint8_t* dst) const;
  void loadState(const uint8_t* src);

  // Debug
  uint16_t vramAddr() const { return vramAdr_; }
  using VramWatch = void (*)(void* ctx, uint16_t adr, uint16_t val);
  VramWatch vramWatch = nullptr;
  void* vramWatchCtx = nullptr;

private:
  struct Bg {
    uint16_t hofs = 0, vofs = 0;
    uint16_t mapBase = 0;   // word address
    uint8_t mapSize = 0;
    uint16_t charBase = 0;  // word address
    bool bigTiles = false;
    bool mosaic = false;
  };

  // ---- registers
  bool forcedBlank_ = true;
  uint8_t brightness_ = 0;
  uint8_t objSize_ = 0;
  uint16_t objBase_ = 0, objNameSel_ = 0;
  uint16_t oamAdr_ = 0, oamAdrReload_ = 0;
  bool oamPriority_ = false;
  uint8_t oamLatch_ = 0;
  bool oamSecondWrite_ = false;
  uint8_t mode_ = 0;
  bool bg3Priority_ = false;
  uint8_t mosaicSize_ = 1;
  uint8_t mosaicStart_ = 1;
  Bg bg_[4];
  uint8_t scrollPrev_ = 0, scrollPrev2_ = 0;
  uint8_t m7Prev_ = 0;
  uint16_t vramAdr_ = 0;
  uint8_t vramInc_ = 1;
  uint8_t vramRemap_ = 0;
  bool vramIncOnHigh_ = false;
  uint16_t vramReadBuf_ = 0;
  uint8_t m7sel_ = 0;
  int16_t m7a_ = 0, m7b_ = 0, m7c_ = 0, m7d_ = 0, m7x_ = 0, m7y_ = 0, m7hofs_ = 0, m7vofs_ = 0;
  // Last byte written to any BGnHOFS. Not saved in states: games always write
  // both bytes, so it is set again before it is used.
  uint8_t hofsPrev_ = 0;
  uint8_t cgAdr_ = 0;
  bool cgSecond_ = false;
  uint8_t cgLatch_ = 0;
  uint8_t winSel_[6] = {};  // per layer (BG1-4, OBJ, COL) 4-bit window settings
  uint8_t winL_[2] = {}, winR_[2] = {};
  uint8_t winLogic_[6] = {};
  uint8_t tm_ = 0, ts_ = 0, tmw_ = 0, tsw_ = 0;
  uint8_t cgwsel_ = 0, cgadsub_ = 0;
  uint8_t fixedR_ = 0, fixedG_ = 0, fixedB_ = 0;
  uint8_t setini_ = 0;
  bool overscan_ = false;
  bool interlace_ = false;
  uint16_t hCount_ = 0, vCount_ = 0;
  bool hFlip_ = false, vFlip_ = false;
  bool countersLatched_ = false;
  uint8_t ppu1Bus_ = 0, ppu2Bus_ = 0;
  bool rangeOver_ = false, timeOver_ = false;
  bool frameOdd_ = false;

  // ---- per-line render buffers (index = x + kMaxExt)
  struct Px {
    uint16_t col;
    uint8_t pri;  // 0 = transparent, else priority+1
    uint8_t pal;  // OBJ palette (0-7) or BG palette
  };
  Px layer_[5][kLineW];
  uint8_t winMask_[6][kLineW];  // 1 = inside effective window for layer
  uint8_t colorLut_[16][32];    // brightness scaled 5-bit -> 8-bit

  // ---- high frame rate (see subFrames): each line's scroll as shown, this frame's and the last one's (by
  // scrollCur_); the sprites' places and their moves
  int16_t lineScroll_[2][4][kFrameH][2] = {};
  uint8_t lineBand_[2][4][kFrameH] = {};
  int16_t lineExt_[2][kFrameH] = {};  // each line's left extension (the view sliding)
  uint8_t lineOk_[2][kFrameH] = {};   // the line was drawn lit (not forced blank, not black)
  int scrollCur_ = 0;
  uint32_t hfrFrame_ = 0, hfrRec_[2] = {~0u, ~0u}, hfrSig_[2] = {};
  bool hfrPrevOk_ = false;  // the last frame was recorded, and is this one's kind (a cut otherwise)
  HfrSprite objPrev_[128], objPrev2_[128];  // last frame's sprites, and the frame before's (flicker)
  int16_t objDx_[128] = {}, objDy_[128] = {};
  bool sub_ = false;  // an in-between picture's line is being drawn
  int16_t subHofs_[4] = {}, subVofs_[4] = {};
  int16_t objSubDx_[128] = {}, objSubDy_[128] = {};
  int subExt_ = 0;  // (the view's slide part of the way back: sprites not placed by a zone go with it)
  static constexpr int kCacheMargin = 96;
  Px subCache_[4][kLineW + 2 * kCacheMargin];  // a line's BG layers at the frame's scroll, wider (renderSubLine)
  bool subCacheOk_[4] = {};
  void objMoves();
  void renderSubLine(int row, int k, int subs, int x0, int x1, int eL, int eR, bool mirror);
  void composeLine(uint32_t* fb, int x0, int x1, int eL, int eR, bool mirror);

  uint16_t readVramWord(uint16_t adr) const { return vram[adr & 0x7fff]; }
  uint16_t remapVramAdr(uint16_t adr) const;
  void renderBg(int l, int row, int x0, int x1, Px* outBase = nullptr);   // (outBase: index 0 is screen x 0)
  void renderMode7(int l, int row, int x0, int x1);
  void renderObj(int row, int x0, int x1);
  void computeWindows(int x0, int x1);
  void applyRemap(int l, int x0, int x1);
  void mirrorOutside(int x0, int x1, bool sub);
  uint16_t mapEntry(int l, int tx, int ty) const;
  uint16_t mapEntryAt(int l, uint16_t base, int tx, int ty) const;
  uint16_t directColor(uint8_t idx, uint8_t pal) const;
  void buildLut();
};
