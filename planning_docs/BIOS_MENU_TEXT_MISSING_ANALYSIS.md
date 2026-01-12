# BIOS Menu Text Missing - Root Cause Analysis
## Date: January 8, 2026
**Status**: 🔴 **TEXT NOT RENDERING** - Background works, text invisible

---

## 🎯 Problem Summary

### What Works ✅
1. **Sony Logo** - Displays perfectly (textures working)
2. **Menu Background** - Purple spheres visible (non-textured polygons working)
3. **VRAM Uploads** - Font data uploaded to VRAM (640,0) successfully
4. **GP0 Commands** - All commands received and buffered correctly
5. **Texture Page** - Set correctly to page_base=(10,0) → X=640

### What's Broken ❌
1. **Text Rendering** - "MEMORY CARD" and "CD PLAYER" text not visible
2. **Textured Rectangles (0x78, 0x7C-0x7F)** - Commands received but text not displayed

---

## 🔍 Evidence from Logs

### 1. Font Texture Upload ✅
```
[15:56:27] *** GP0(0xA0): VRAM UPLOAD START -> Dest(640,0) Size(60x48) = 1440 words [FONT/TEXTURE DATA?] ***
[15:56:27] GP0(0xA0): Switched to IMAGE_LOAD mode, words_remaining=1440
[15:56:27] *** GP0(0xA0): VRAM UPLOAD COMPLETE -> Region(640,0) Size(60x48) | Sample[0,0]=0x0000 ***
```
✅ Font texture (60x48 pixels) uploaded to VRAM at (640, 0)

### 2. Texture Page Setup ✅
```
[15:56:27] GP0(0xE1): Draw Mode set page_base=(10,0) texture_depth=0 semi_trans=0 draw_to_display=0 tex_disable=0 flip=(0,0)
```
✅ Texture page set to (10,0) → X coordinate = 10 * 64 = **640** (where font is!)

### 3. Textured Rectangle Commands ✅
```
[15:56:32] GP0(0x78) = 0x78140000  (16x16 rectangle, opaque)
[15:56:32] GP0(0x78) = 0x78100000  (16x16 rectangle, opaque)
[15:56:32] GP0(0x78) = 0x780c0000  (16x16 rectangle, opaque)
```
✅ Hundreds of GP0(0x78) commands (textured 16x16 rectangles) received

### 4. VRAM Texture Uploads ✅
```
126 occurrences of "vram.*upload|texture.*upload"
```
✅ VRAM being uploaded to OpenGL texture 126 times

---

## 🧩 What Should Happen

### PS1 Font Rendering Process:
1. **BIOS uploads font texture** to VRAM (640, 0) → ✅ **DONE**
2. **BIOS sets texture page** to (10, 0) = X:640 → ✅ **DONE**
3. **BIOS sends textured rectangles** (0x78, 0x7C) with UV coordinates → ✅ **DONE**
4. **GPU samples font texture** from VRAM at UV coords → ❌ **FAILING?**
5. **Text appears on screen** → ❌ **NOT HAPPENING**

---

## 🔧 Potential Root Causes

### **Theory 1: UV Coordinate Calculation** 🟡
**File**: `src/gpu.c:505` - `draw_rectangle()` function

**Code**:
```c
if (use_texture) {
    t[0].u = tex->u;       t[0].v = tex->v;
    t[1].u = tex->u + w;   t[1].v = tex->v;        // ⚠️ Adding pixel width to UV!
    t[2].u = tex->u;       t[2].v = tex->v + h;    // ⚠️ Adding pixel height to UV!
    t[3].u = tex->u + w;   t[3].v = tex->v + h;
}
```

**Issue**: For a 16x16 rectangle, this adds 16 pixels to U and V coordinates, but UV coords might need to be:
- Relative to **texture page** (0-255 range)
- Or **absolute VRAM coords** (0-1023 range)

**Expected for 16x16 character**:
- Input UV from BIOS: (U, V) = (0, 0) for first char
- Should sample **16x16 region** from texture page at (640+U, 0+V)

---

### **Theory 2: Texture Page Base Not Applied** 🔴 **MOST LIKELY**
**File**: `src/renderer.c` - Shader uniform setup

**Problem**: The texture page base (X=640) might not be added to UV coordinates in the shader!

**What Should Happen**:
```glsl
// In fragment shader
vec2 final_uv = vertex_uv + vec2(texture_page_x, texture_page_y);
uint16_t texel = texture(vram_texture, final_uv / vec2(1024, 512));
```

**What Might Be Happening**:
```glsl
// Shader sampling from (0,0) instead of (640,0)!
uint16_t texel = texture(vram_texture, vertex_uv / vec2(1024, 512));
```

---

### **Theory 3: CLUT (Color Lookup Table) Not Working** 🟡
**BIOS fonts use 4-bit textures** (texture_depth=0) which require CLUT for colors.

**Evidence from logs**:
```
texture_depth=0  → 4-bit indexed color (needs CLUT)
```

**CLUT coords from rectangle command** (word 2, bits 16-31):
```c
uint16_t clut = (uint16_t)(uv_clut >> 16);  // CLUT location in VRAM
```

**Shader must**:
1. Sample 4-bit index from texture
2. Use CLUT coords to lookup actual RGB color
3. Return final color

**If CLUT not working**: Text pixels would be black/transparent!

---

### **Theory 4: Semi-Transparency Breaking Text** 🟢
**Less likely** - Logs show `semi_trans=0` for text commands.

---

### **Theory 5: Raw Texture Mode Issue** 🟢
**Less likely** - Font rendering typically uses **blended mode**, not raw.

---

## 🔬 Code Locations to Check

### 1. **GPU Command Handler** ✅
**File**: `src/gpu.c:668`
```c
static void gp0_rect_tex_16x16_opaque(Gpu* gpu) {
    uint8_t opcode = (uint8_t)(cmd >> 24);
    bool raw_texture = ((cmd & 0x01000000) != 0) || (opcode & 1);
    draw_rectangle(gpu, x, y, 16, 16, col, true, raw_texture, &tex, clut, tpage);
}
```
✅ Looks correct - passes texture coords, CLUT, tpage

### 2. **Rectangle Drawing** ⚠️
**File**: `src/gpu.c:505`
```c
static void draw_rectangle(Gpu* gpu, int16_t x, int16_t y, uint16_t w, uint16_t h, 
                          RendererColor col, bool textured, bool raw_texture, 
                          RendererTexCoord* tex, uint16_t clut, uint16_t tpage) {
    if (use_texture) {
        renderer_upload_vram(&gpu->renderer, (const uint16_t*)gpu->vram.data);
        t[0].u = tex->u;       t[0].v = tex->v;
        t[1].u = tex->u + w;   t[1].v = tex->v;        // ⚠️ Check this!
        t[2].u = tex->u;       t[2].v = tex->v + h;
        t[3].u = tex->u + w;   t[3].v = tex->v + h;
        renderer_push_quad(&gpu->renderer, p, c, t, clut, tpage);
    }
}
```
⚠️ UV calculation might be wrong for 4-bit textures!

### 3. **Renderer Texture Setup** 🔴 **CRITICAL**
**File**: `src/renderer.c`
- `renderer_push_quad()` - Passes UV, CLUT, tpage to vertex buffer
- `renderer_flush()` - Sets shader uniforms
- **Fragment shader** - Must decode 4-bit texture + apply CLUT

**Need to check**:
- Is `tpage` being passed to shader correctly?
- Is shader adding `texture_page_x` to UV coords?
- Is CLUT lookup implemented?

---

## 🎯 Recommended Fix Priority

### 🔴 **CRITICAL - Check These First**

#### **1. Verify Texture Page Applied in Shader** (10 min)
**Check**: Fragment shader adds texture page offset to UV

**Expected shader code**:
```glsl
vec2 texture_page_offset = vec2(float(tpage_x), float(tpage_y));
vec2 final_uv = vertex_uv + texture_page_offset;
vec2 normalized_uv = final_uv / vec2(1024.0, 512.0);
uint texel_index = texture(vram_texture, normalized_uv).r;
```

**File to check**: Shader code in `src/renderer.c` or separate shader files

---

#### **2. Verify CLUT Implementation** (15 min)
**Check**: 4-bit texture decoding + CLUT lookup

**Expected process**:
1. Sample 4-bit index from texture at (640 + U, 0 + V)
2. Extract CLUT coords from command (word 2, upper 16 bits)
3. CLUT X = (clut & 0x3F) * 16, CLUT Y = (clut >> 6) & 0x1FF
4. Sample actual color from VRAM at CLUT position
5. Return RGB color

**Code location**: Fragment shader or `renderer.c`

---

#### **3. Check UV Coordinate Scaling** (5 min)
**Question**: Are UV coords in pixels or normalized (0-1)?

**PS1 behavior**:
- UV coords are in **pixels** (0-255 within texture page)
- Must convert to VRAM pixels (0-1023 for X, 0-511 for Y)
- Then normalize to (0.0-1.0) for OpenGL sampling

**Check**: `draw_rectangle()` UV calculation

---

### 🟡 **MEDIUM - If Above Don't Fix**

#### **4. Check Texture Wrapping/Filtering**
- OpenGL texture wrap mode (should be `GL_CLAMP_TO_EDGE`)
- Texture filtering (should be `GL_NEAREST` for pixel-perfect)

#### **5. Verify VRAM Upload Timing**
- VRAM uploaded **before** each textured draw? ✅ (yes, line 518)
- VRAM data correct format (R16UI)?

---

### 🟢 **LOW - Unlikely Causes**

#### **6. Check Alpha/Blending**
- Text might be rendering but invisible due to alpha=0
- Check fragment shader alpha output

#### **7. Verify Drawing Offset**
- Text might be offscreen due to wrong drawing offset
- Check `gpu->drawing_x_offset` and `gpu->drawing_y_offset`

---

## 📊 Comparison: Logo vs Text

| Feature | Sony Logo | BIOS Menu Text | Status |
|---------|-----------|----------------|--------|
| **Texture Source** | Uploaded to VRAM | Uploaded to VRAM (640,0) | ✅ Both work |
| **Texture Page** | Various pages | Page (10,0) = X:640 | ✅ Set correctly |
| **Command Type** | Textured polygons | Textured rectangles (0x78) | ✅ Both received |
| **Texture Depth** | 4-bit/8-bit/15-bit | 4-bit (needs CLUT) | ⚠️ **CLUT critical** |
| **CLUT Usage** | Maybe not needed? | **Required for fonts** | 🔴 **Suspect** |
| **Rendering** | ✅ **Perfect** | ❌ **Invisible** | 🔴 **Different!** |

**Key Difference**: Sony logo might use **15-bit direct color** textures, while fonts use **4-bit indexed + CLUT**!

---

## 🛠️ Debugging Steps

### Step 1: Enable Renderer Debug Logs (2 min)
```c
// In src/renderer.c - renderer_push_quad()
printf("[RENDERER] Textured quad: UV=(%d,%d) CLUT=0x%04X TPage=0x%04X\n", 
       tex[0].u, tex[0].v, clut, tpage);
```

### Step 2: Check Shader Uniforms (5 min)
```c
// In renderer_flush() - print uniform values
printf("[SHADER] TPage uniform: X=%d Y=%d\n", tpage_x, tpage_y);
printf("[SHADER] CLUT uniform: X=%d Y=%d\n", clut_x, clut_y);
```

### Step 3: Dump VRAM Region (10 min)
```c
// After font upload - dump VRAM (640,0) to (700,48)
for (int y = 0; y < 48; y++) {
    for (int x = 640; x < 700; x++) {
        uint16_t pixel = vram_load16(&gpu->vram, x, y);
        if (pixel != 0) printf("VRAM[%d,%d]=0x%04X ", x, y, pixel);
    }
}
```

### Step 4: Test with Simple Texture (15 min)
```c
// Create a known pattern in VRAM (640,0)
for (int y = 0; y < 16; y++) {
    for (int x = 0; x < 16; x++) {
        vram_store16(&gpu->vram, 640+x, y, 0xFFFF); // White square
    }
}
// If white square appears → texture sampling works, CLUT is the issue
// If nothing appears → texture page offset not applied
```

---

## ✅ Most Likely Root Cause

**Hypothesis**: **CLUT (Color Lookup Table) not implemented or broken**

**Why**:
1. Sony logo works (might use direct color, not indexed)
2. Font texture uploaded successfully (data is in VRAM)
3. Texture page set correctly (640, 0)
4. Commands received (GP0 0x78 × hundreds)
5. **But fonts use 4-bit indexed color requiring CLUT!**

**If CLUT not working**:
- 4-bit texture index sampled correctly
- But index not converted to RGB via CLUT
- Result: Black/transparent pixels (invisible text)

---

## 🎯 Immediate Action Plan

1. **Check shader code** - Look for CLUT implementation
2. **Check texture page offset** - Must add (640, 0) to UV coords
3. **Add debug logging** - Print UV, CLUT, TPage values
4. **Test with white square** - Verify texture sampling works

**ETA to fix**: 30-60 minutes once root cause confirmed

---

**Status**: 🔴 **Ready for debugging** - All evidence collected, root cause narrowed to shader texture sampling or CLUT implementation.
