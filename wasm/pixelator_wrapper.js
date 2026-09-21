/*
 * pixelator_wrapper.js
 * --------------------
 * JavaScript bridge between the UI (app.js) and the WebAssembly module.
 *
 * Responsibilities:
 *   - Load the Emscripten-generated WASM module
 *   - Allocate / free WASM heap memory
 *   - Copy pixel data in and out of WASM memory
 *   - Call the C functions exported from pixelator.c
 *
 * The actual pixel-art algorithm lives in src/pixelator.c.
 * This file contains NO image-processing logic.
 */

'use strict';

class PixelatorWasm {

    constructor() {
        this.module  = null;
        this.isReady = false;
    }

    /* ------------------------------------------------------------------
       load(onReady, onError)
       Injects wasm/pixelator.js into the page and waits for the
       Emscripten runtime to initialise.
    ------------------------------------------------------------------ */
    load(onReady, onError) {

        /* Already loaded? */
        if (typeof Module !== 'undefined' && Module.calledRun) {
            this.module  = Module;
            this.isReady = true;
            if (onReady) onReady();
            return;
        }

        /* Pre-configure Emscripten Module BEFORE the script is injected. */
        window.Module = {
            onRuntimeInitialized: () => {
                this.module  = window.Module;
                this.isReady = true;
                if (onReady) onReady();
            },
            onAbort: (reason) => {
                console.error('WASM aborted:', reason);
                if (onError) onError('WebAssembly aborted: ' + reason);
            }
        };

        const script    = document.createElement('script');
        script.src      = 'wasm/pixelator.js';
        script.onerror  = () => {
            if (onError) onError(
                'Could not load wasm/pixelator.js. ' +
                'Run build.ps1 to compile the C source first.'
            );
        };
        document.head.appendChild(script);
    }

    /* ------------------------------------------------------------------
       _fn(name)  →  the exported C function
    ------------------------------------------------------------------ */
    _fn(name) {
        if (!this.isReady) throw new Error('WASM is not ready yet.');
        const f = this.module['_' + name];
        if (!f) throw new Error(`WASM export '_${name}' not found.`);
        return f;
    }

    /* ------------------------------------------------------------------
       _call(imageData, fnName, extraArgs)
       Core helper:
         1. malloc in WASM heap
         2. copy ImageData bytes in
         3. call C function
         4. copy result back
         5. free WASM heap
       Returns a new ImageData with the processed pixels.
    ------------------------------------------------------------------ */
    _call(imageData, fnName, extraArgs = []) {
        const { width, height, data } = imageData;
        const size = width * height * 4;

        const ptr = this._fn('get_buffer')(size);
        if (!ptr) throw new Error('WASM malloc failed.');

        try {
            /* Copy pixels → WASM */
            this.module.HEAPU8.set(data, ptr);

            /* Call the C function */
            this._fn(fnName)(ptr, width, height, ...extraArgs);

            /* Read result back */
            return new ImageData(
                new Uint8ClampedArray(
                    this.module.HEAPU8.buffer, ptr, size
                ),
                width,
                height
            );
        } finally {
            this._fn('free_buffer')(ptr);
        }
    }

    /* ------------------------------------------------------------------
       pixelateAdvanced(imageData, opts)
       ----------------------------------
       Calls the new C `pixelate_advanced()` function.

       opts = {
         paletteSize  : 8 | 16 | 32 | 64   (default 16)
         ditherMode   : 0 | 1 | 2           (default 0)
                         0 = none
                         1 = Bayer 4×4
                         2 = Floyd-Steinberg
         edgeStrength : 0-100              (default 30)
         contrast     : 0-100              (default 0)
         grayscale    : 0 | 1              (default 0)
       }

       The imageData should already be at the LOW pixel-grid resolution
       (e.g. 150 × 100).  JavaScript upscales the result with
       nearest-neighbor after this call.
    ------------------------------------------------------------------ */
    pixelateAdvanced(imageData, opts = {}) {
        const paletteSize  = opts.paletteSize  ?? 16;
        const ditherMode   = opts.ditherMode   ??  0;
        const edgeStrength = opts.edgeStrength ?? 30;
        const contrast     = opts.contrast     ??  0;
        const grayscale    = opts.grayscale    ??  0;

        return this._call(imageData, 'pixelate_advanced', [
            paletteSize,
            ditherMode,
            edgeStrength,
            contrast,
            grayscale
        ]);
    }

    /* ------------------------------------------------------------------
       Legacy methods (kept for backward compatibility)
    ------------------------------------------------------------------ */

    pixelate(imageData, pixelSize) {
        return this._call(imageData, 'pixelate', [pixelSize]);
    }

    pixelateGrayscale(imageData, pixelSize) {
        return this._call(imageData, 'pixelate_grayscale', [pixelSize]);
    }

    pixelateReduced(imageData, pixelSize, colorLevels = 4) {
        return this._call(imageData, 'pixelate_reduced', [pixelSize, colorLevels]);
    }
}

/* Singleton used by app.js */
const pixelatorWasm = new PixelatorWasm();
