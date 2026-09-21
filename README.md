# 🎮 Pixel Image Generator

> **Turn your photos into tiny pixels — right in your browser.**  
> No server. No uploads. Powered by **C + WebAssembly**.

---

## 📋 Table of Contents

1. [Project Overview](#-project-overview)  
2. [Features](#-features)  
3. [Technologies Used](#-technologies-used)  
4. [How the Pixelation Algorithm Works](#-how-the-pixelation-algorithm-works)  
5. [Why C?](#-why-c)  
6. [Why WebAssembly?](#-why-webassembly)  
7. [Project Structure](#-project-structure)  
8. [How to Compile C to WebAssembly](#-how-to-compile-c-to-webassembly)  
9. [How to Run Locally](#-how-to-run-locally)  
10. [How to Deploy to Vercel](#-how-to-deploy-to-vercel)  
11. [How JavaScript Communicates with C/WASM](#-how-javascript-communicates-with-cwasm)  
12. [Limitations](#-limitations)  
13. [Future Improvements](#-future-improvements)  
14. [How to Explain This Project in a Viva](#-how-to-explain-this-project-in-a-viva)  
15. [Privacy](#-privacy)

---

## 📖 Project Overview

**Pixel Image Generator** is a browser-based image pixelation tool.

You upload a photo → the pixel data is sent to a C function compiled to WebAssembly → the C code divides the image into square blocks and replaces each block with its average color → you get a pixelated image → you download it.

**The entire image processing happens locally in your browser. Your images are processed locally in your browser. Nothing is sent to any server.**

---

## ✨ Features

- 🖼️ Upload images (PNG, JPG, JPEG, WEBP)
- 🎚️ Adjustable pixel block size (2px to 50px)
- 🎨 Optional grayscale mode
- 🎨 Optional color reduction mode
- ⚡ Live preview as you move the slider
- 📐 Side-by-side original vs. pixelated comparison
- ⬇️ Download the pixelated image as PNG
- 📱 Fully responsive — works on mobile and desktop
- 🔒 100% client-side — no server, no uploads
- ♿ Accessible — keyboard navigation, ARIA labels, focus management

---

## 🛠️ Technologies Used

| Layer            | Technology                  | Purpose                              |
|------------------|-----------------------------|--------------------------------------|
| Core Algorithm   | **C**                       | Image pixelation algorithm           |
| Runtime          | **WebAssembly (WASM)**      | Run C code in the browser            |
| Build Tool       | **Emscripten**              | Compile C → WASM                     |
| Frontend HTML    | **HTML5**                   | Page structure                       |
| Frontend Styles  | **CSS3**                    | Responsive design, animations        |
| Frontend Logic   | **Vanilla JavaScript**      | UI, file handling, WASM bridge       |
| Image Rendering  | **HTML Canvas API**         | Display and process images           |
| Deployment       | **Vercel**                  | Static hosting                       |

---

## 🧮 How the Pixelation Algorithm Works

The algorithm is implemented in `src/pixelator.c`.

### Concept

An image is a grid of pixels. Each pixel has a color stored as **RGBA** (Red, Green, Blue, Alpha). The pixelation effect divides the image into square blocks and replaces every pixel in a block with the block's average color.

### Step-by-Step Example

Suppose your image is **12 × 8 pixels** and you choose **Pixel Size = 4**.

```
┌────────────────────────────┐
│ ▪▪▪▪ │ ▪▪▪▪ │ ▪▪▪▪       │
│ ▪▪▪▪ │ ▪▪▪▪ │ ▪▪▪▪       │
│ ▪▪▪▪ │ ▪▪▪▪ │ ▪▪▪▪       │
│ ▪▪▪▪ │ ▪▪▪▪ │ ▪▪▪▪       │
│──────┼───────┼────────────│
│ ▪▪▪▪ │ ▪▪▪▪ │ ▪▪▪▪       │
│ ▪▪▪▪ │ ▪▪▪▪ │ ▪▪▪▪       │
│ ▪▪▪▪ │ ▪▪▪▪ │ ▪▪▪▪       │
│ ▪▪▪▪ │ ▪▪▪▪ │ ▪▪▪▪       │
└────────────────────────────┘
 Block1  Block2  Block3    (top row)
 Block4  Block5  Block6    (bottom row)
```

**For each 4×4 block:**

```
Average Red   = (R1 + R2 + ... + R16) / 16
Average Green = (G1 + G2 + ... + G16) / 16
Average Blue  = (B1 + B2 + ... + B16) / 16
```

**Then every pixel in that block is set to (avgR, avgG, avgB).**

The result is a mosaic of flat-color squares — the pixelated effect.

### The C Code (Simplified)

```c
for (int blockY = 0; blockY < height; blockY += pixelSize) {
    for (int blockX = 0; blockX < width; blockX += pixelSize) {

        // Calculate actual block size (handle edge blocks)
        int blockW = min(pixelSize, width  - blockX);
        int blockH = min(pixelSize, height - blockY);

        long totalR = 0, totalG = 0, totalB = 0;
        int count = blockW * blockH;

        // Step 1: Sum up all R, G, B values in this block
        for (int py = 0; py < blockH; py++) {
            for (int px = 0; px < blockW; px++) {
                int idx = ((blockY + py) * width + (blockX + px)) * 4;
                totalR += pixels[idx + 0];
                totalG += pixels[idx + 1];
                totalB += pixels[idx + 2];
            }
        }

        // Step 2: Compute averages
        unsigned char avgR = totalR / count;
        unsigned char avgG = totalG / count;
        unsigned char avgB = totalB / count;

        // Step 3: Fill the entire block with the average color
        for (int py = 0; py < blockH; py++) {
            for (int px = 0; px < blockW; px++) {
                int idx = ((blockY + py) * width + (blockX + px)) * 4;
                pixels[idx + 0] = avgR;
                pixels[idx + 1] = avgG;
                pixels[idx + 2] = avgB;
                // Alpha (idx + 3) is not modified
            }
        }
    }
}
```

### Time Complexity

- **O(W × H)** — Every pixel is visited a constant number of times (once to read, once to write).
- `W` = image width, `H` = image height.
- A 1920×1080 image has ~2 million pixels — processed in milliseconds in C/WASM.

### Memory Complexity

- **O(1)** extra space — we work in-place on the pixel buffer.
- Only a handful of variables (averages, counters) are needed per block.

---

## ❓ Why C?

| Reason | Explanation |
|--------|-------------|
| **Performance** | C is a compiled language. It runs near machine speed with no garbage collector pauses. |
| **Low-level memory access** | C lets us directly manipulate pixel byte arrays with pointers. |
| **WASM compatibility** | Emscripten can compile C to WebAssembly, bringing it to the browser. |
| **Educational value** | Understanding C arrays, pointers, and loops is foundational to computer science. |
| **Industry standard** | Image processing libraries like libjpeg and libpng are written in C. |

---

## ❓ Why WebAssembly?

| Reason | Explanation |
|--------|-------------|
| **Run C in browser** | WASM allows compiled C code to run inside any modern web browser. |
| **Near-native speed** | WASM executes significantly faster than equivalent JavaScript for math-heavy tasks. |
| **Safe sandbox** | WASM runs in a secure browser sandbox — it cannot access your file system. |
| **No plugin required** | Modern browsers support WASM natively — no Flash, no Java, no plugin needed. |
| **Portable** | The same .wasm file works on Chrome, Firefox, Safari, and Edge. |

---

## 📁 Project Structure

```
pixel-image-generator/
│
├── index.html              # Main HTML page (UI structure)
├── style.css               # All CSS styles (responsive, pixel-art theme)
├── app.js                  # JavaScript UI logic and WASM bridge
├── vercel.json             # Vercel deployment configuration
├── README.md               # This file
│
├── src/
│   └── pixelator.c         # ★ C source code — the core algorithm
│
├── wasm/
│   ├── pixelator_wrapper.js   # JS class wrapping WASM calls (written by developer)
│   ├── pixelator.js           # ⚙ Generated by Emscripten — run build command
│   └── pixelator.wasm         # ⚙ Generated by Emscripten — run build command
│
├── assets/
│   └── mascot.jpg          # Pixel art mascot image
│
└── examples/
    └── README.md           # Example images and usage guide
```

**Files written by the developer (you):**
- `index.html`, `style.css`, `app.js`, `vercel.json`, `README.md`
- `src/pixelator.c`
- `wasm/pixelator_wrapper.js`
- `assets/mascot.jpg`

**Files generated by the build tool (Emscripten):**
- `wasm/pixelator.js` ← Auto-generated, do not edit manually
- `wasm/pixelator.wasm` ← Auto-generated, do not edit manually

---

## ⚙️ How to Compile C to WebAssembly

### Prerequisites

Install **Emscripten SDK** from [emscripten.org](https://emscripten.org/docs/getting_started/downloads.html).

```bash
# Clone and install Emscripten
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest

# On Windows (PowerShell):
.\emsdk install latest
.\emsdk activate latest

# Activate environment variables (run this in every new terminal)
source ./emsdk_env.sh          # Linux / macOS
.\emsdk_env.ps1                # Windows PowerShell
```

### Compile Command

Run this from the root of the project:

```bash
emcc src/pixelator.c \
     -O2 \
     -s WASM=1 \
     -s EXPORTED_FUNCTIONS="['_pixelate','_pixelate_grayscale','_pixelate_reduced','_get_buffer','_free_buffer']" \
     -s EXPORTED_RUNTIME_METHODS="['ccall','cwrap','HEAPU8']" \
     -s ALLOW_MEMORY_GROWTH=1 \
     -s MODULARIZE=0 \
     -s EXPORT_NAME="Module" \
     -o wasm/pixelator.js
```

> **Windows PowerShell version** (wrap long arguments with backtick):

```powershell
emcc src/pixelator.c `
     -O2 `
     -s WASM=1 `
     -s EXPORTED_FUNCTIONS="['_pixelate','_pixelate_grayscale','_pixelate_reduced','_get_buffer','_free_buffer']" `
     -s EXPORTED_RUNTIME_METHODS="['ccall','cwrap','HEAPU8']" `
     -s ALLOW_MEMORY_GROWTH=1 `
     -s MODULARIZE=0 `
     -s EXPORT_NAME="Module" `
     -o wasm/pixelator.js
```

### What this command does

| Flag | Meaning |
|------|---------|
| `src/pixelator.c` | Input C source file |
| `-O2` | Optimization level 2 (smaller, faster output) |
| `-s WASM=1` | Output WebAssembly (.wasm) alongside the JS glue code |
| `-s EXPORTED_FUNCTIONS` | List of C functions to expose to JavaScript |
| `-s EXPORTED_RUNTIME_METHODS` | Expose HEAPU8 memory view and helper functions |
| `-s ALLOW_MEMORY_GROWTH=1` | Allow the WASM heap to grow dynamically for large images |
| `-s MODULARIZE=0` | Output as a simple global Module (not an ES module) |
| `-o wasm/pixelator.js` | Output the generated JS glue file to the wasm/ folder |

Emscripten will automatically also create `wasm/pixelator.wasm` alongside `pixelator.js`.

---

## 🏃 How to Run Locally

After compiling the WASM:

```bash
# Option 1: Python (most systems have it)
python -m http.server 8080
# Open http://localhost:8080

# Option 2: Node.js (if installed)
npx serve .
# Open http://localhost:3000

# Option 3: VS Code Live Server extension
# Right-click index.html → Open with Live Server
```

> **Important:** You MUST use a local web server. Opening `index.html` directly as a `file://` URL will fail because browsers block WASM loading from file:// for security reasons.

---

## 🚀 How to Deploy to Vercel

### Option A: Vercel CLI

```bash
# Install Vercel CLI
npm install -g vercel

# Deploy from project root
cd pixel-image-generator
vercel

# Follow the prompts — it will ask:
# - Link to existing project? No → create new
# - What is your project root? ./
# - Override build command? No (this is a static site)
```

### Option B: Vercel Dashboard (GitHub)

1. Push the project to a GitHub repository (make sure `wasm/pixelator.js` and `wasm/pixelator.wasm` are committed).
2. Go to [vercel.com](https://vercel.com) → New Project → Import from GitHub.
3. Select your repository.
4. Framework Preset: **Other** (this is a static site).
5. Build Command: leave **empty**.
6. Output Directory: leave **empty** (defaults to root).
7. Click **Deploy**.

The `vercel.json` file in the project automatically configures:
- Correct `Content-Type: application/wasm` header for the .wasm file.
- Security headers.
- Cache headers for efficient delivery.

---

## 🔌 How JavaScript Communicates with C/WASM

```
JavaScript (app.js)
       │
       │  1. pixelatorWasm.pixelate(imageData, pixelSize)
       ▼
JavaScript Wrapper (wasm/pixelator_wrapper.js)
       │
       │  2. Allocate WASM memory: _get_buffer(bufferSize)
       │  3. Copy pixel bytes:     HEAPU8.set(data, ptr)
       │  4. Call C function:      _pixelate(ptr, width, height, pixelSize)
       │  5. Read result:          new Uint8ClampedArray(HEAPU8.buffer, ptr, size)
       │  6. Free memory:          _free_buffer(ptr)
       ▼
WASM Module (wasm/pixelator.wasm)  ← Compiled from src/pixelator.c
       │
       │  Runs the C pixelation algorithm on the pixel buffer
       ▼
JavaScript gets back the modified ImageData
       │
       ▼
Canvas displays the pixelated result
```

**Key concept:** `Module.HEAPU8` is a `Uint8Array` that directly maps to the WASM module's memory. Both JavaScript and C share this same memory space. JavaScript writes pixel data in, C reads and modifies it in-place, JavaScript reads it back out.

---

## ⚠️ Limitations

1. **WASM must be compiled first** — The generated `wasm/pixelator.js` and `wasm/pixelator.wasm` files must be built with Emscripten before deployment.
2. **Single-threaded** — WASM runs on the main JavaScript thread. For very large images (>2048px), processing may briefly freeze the UI. (Future: Web Workers)
3. **No WebP download** — The download is always PNG regardless of input format.
4. **Browser compatibility** — Requires a modern browser with WebAssembly support (Chrome 57+, Firefox 53+, Safari 11+, Edge 16+).
5. **Max image size** — Images larger than 2048px on the longest side are scaled down before processing.

---

## 🌱 Future Improvements

- [ ] Process WASM in a **Web Worker** to prevent UI freezing
- [ ] Add **more effects** (blur, sharpen, edge detection) — all in C
- [ ] **Batch processing** — pixelate multiple images at once
- [ ] **Custom palette** — allow the user to specify colors for the reduced mode
- [ ] **Video pixelation** — process frames from a webcam stream
- [ ] **Export as GIF** — animate between original and pixelated
- [ ] **WebGL acceleration** — use GPU for even faster processing

---

## 🎓 How to Explain This Project in a Viva

### What is pixelation?
> Pixelation is a visual effect where an image is divided into large square blocks, and each block is filled with a single color (the average color of all pixels in that block). This makes the image look like it's made of big, visible pixels — like old 8-bit video games.

### What is an image pixel?
> A pixel (short for "picture element") is the smallest unit of a digital image. An image is just a rectangular grid of pixels. Each pixel stores a color value.

### What is RGB?
> RGB stands for **Red, Green, Blue**. Every color can be represented by mixing different amounts of these three primary light colors. Each channel has a value from 0 to 255. For example:
> - Pure red: (255, 0, 0)
> - White: (255, 255, 255)
> - Black: (0, 0, 0)

### What is RGBA?
> RGBA adds a fourth channel: **Alpha**, which represents **transparency**. Alpha = 0 means fully transparent, Alpha = 255 means fully opaque. The HTML Canvas API always uses RGBA format.

### How is an image represented in memory?
> An image is stored as a flat array of bytes. For each pixel, we store 4 consecutive bytes: R, G, B, A. A 100×100 pixel image needs `100 × 100 × 4 = 40,000` bytes. The pixel at position (x, y) starts at index `(y × width + x) × 4`.

### What is a pointer?
> A pointer is a variable that stores a **memory address**. In C, `unsigned char *pixels` is a pointer — it points to the first byte of the pixel array. We can then use `pixels[idx]` to read or write any byte in the array. This is how C accesses large blocks of memory efficiently.

### Why are unsigned char values used?
> Each color channel value must be between 0 and 255. `unsigned char` is an 8-bit (1 byte) data type that stores values from 0 to 255 exactly — perfect for pixel values. `signed char` would only go from -128 to 127, which doesn't work for colors.

### How does the block algorithm work?
> 1. Start at the top-left corner of the image.
> 2. Jump `pixelSize` pixels at a time in both X and Y directions.
> 3. For each block position, read all pixels in that block.
> 4. Calculate the average R, G, and B values.
> 5. Set every pixel in that block to the average color.
> 6. Move to the next block. Repeat until the whole image is processed.

### What is time complexity?
> Time complexity describes how the algorithm's runtime grows with the input size. The pixelation algorithm is **O(W × H)** — it visits every pixel exactly twice (once to read, once to write). Doubling the image size doubles the processing time — linear growth.

### What is memory complexity?
> Memory complexity describes how much extra memory the algorithm uses. Our algorithm is **O(1)** — it only uses a few integer variables (for the running sums and averages) regardless of image size. The pixel buffer itself is not "extra" memory — it's the input.

### Why use Canvas?
> The HTML `<canvas>` element gives JavaScript access to raw pixel data via the `ImageData` API. We can read pixels (`getImageData`), pass them to WASM, receive the processed pixels back, and draw them to the canvas (`putImageData`). It's the standard browser API for programmatic image manipulation.

### Why no backend?
> All processing is done in WebAssembly running in the browser. There is no need for a server. This means:
> - User images stay private — they never leave the device.
> - No server costs.
> - No latency from network uploads.
> - Works offline once the page is loaded.

### Why is Vercel suitable?
> Vercel is a static hosting platform — it serves HTML, CSS, JS, and WASM files exactly as they are. Since our application has no backend server and no database, a static host is all we need. Vercel also provides free HTTPS, global CDN, and simple deployment from GitHub.

### Why C and not JavaScript for the algorithm?
> JavaScript is interpreted (or JIT-compiled) and has automatic memory management (garbage collection). C is compiled to machine code and gives direct control over memory. For pixel-by-pixel processing of large images, C runs significantly faster. Also, using C+WASM demonstrates understanding of system-level programming and how compiled languages integrate with web technologies.

---

## 🔒 Privacy

> **Your images are processed locally in your browser.**  
> No image data is ever sent to any server, API, or third-party service.  
> No cookies. No tracking. No analytics.  
> This tool works completely offline once loaded.

---

*Made with ❤️ and pixels.*
