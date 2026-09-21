/**
 * app.js — Pixel Image Generator
 * ================================
 *
 * UI + image processing orchestration layer.
 *
 * Workflow:
 *
 *   Original Image
 *        ↓
 *   Reduce to a small pixel grid
 *        ↓
 *   C / WebAssembly processing
 *        ↓
 *   Nearest-neighbor upscale
 *        ↓
 *   Sharp pixel-art image
 *
 * The actual pixel/color algorithms are still implemented
 * in src/pixelator.c and compiled to WebAssembly.
 */

'use strict';


/* ================================================================
   CONSTANTS
   ================================================================ */

/**
 * Maximum dimension for processing.
 *
 * Very large images are first reduced so that WASM processing
 * does not consume excessive memory.
 */
const MAX_PROCESS_SIZE = 2048;

/**
 * Maximum accepted file size.
 */
const MAX_FILE_SIZE = 20 * 1024 * 1024;

/**
 * Accepted image formats.
 */
const ACCEPTED_TYPES = [
    'image/png',
    'image/jpeg',
    'image/webp'
];


/* ================================================================
   STATE
   ================================================================ */

const state = {

    // Original image after the initial safety resize
    originalImageData: null,

    originalWidth: 0,
    originalHeight: 0,

    // Whether a result currently exists
    hasResult: false,

    // WASM state
    wasmReady: false,

    // Prevent multiple simultaneous processing operations
    processing: false,

    // Whether the uploaded image had to be resized
    wasResized: false
};


/* ================================================================
   DOM REFERENCES
   ================================================================ */

const $ = id => document.getElementById(id);

const dom = {

    // Header
    wasmStatus: $('wasmStatus'),

    // Upload
    dropzone: $('dropzone'),
    fileInput: $('fileInput'),

    // Messages
    errorBanner: $('errorBanner'),
    errorText: $('errorText'),

    infoBanner: $('infoBanner'),
    infoText: $('infoText'),

    // Controls
    controlsSection: $('controlsSection'),

    contrastSlider: $('contrastSlider'),
    contrastDisplay: $('contrastDisplay'),
    
    edgeSlider: $('edgeSlider'),
    edgeDisplay: $('edgeDisplay'),

    grayscaleToggle: $('grayscaleToggle'),

    imageDimensions: $('imageDimensions'),
    resizeNotice: $('resizeNotice'),
    imageInfo: $('imageInfo'),

    generateBtn: $('generateBtn'),
    resetBtn: $('resetBtn'),

    // Results
    resultsSection: $('resultsSection'),

    originalCanvas: $('originalCanvas'),
    resultCanvas: $('resultCanvas'),

    canvasLoading: $('canvasLoading'),

    downloadBtn: $('downloadBtn')
};


/* ================================================================
   WASM INITIALIZATION
   ================================================================ */

function initWasm() {

    setWasmStatus(
        'loading',
        'Loading WebAssembly…'
    );

    pixelatorWasm.load(

        // Success
        () => {

            state.wasmReady = true;

            setWasmStatus(
                'ready',
                'WebAssembly Ready ✓'
            );

            if (state.originalImageData) {
                dom.generateBtn.disabled = false;
            }
        },

        // Error
        (errMsg) => {

            state.wasmReady = false;

            setWasmStatus(
                'error',
                'WASM Failed to Load'
            );

            showError(
                'Could not load the WebAssembly module. ' +
                'Please make sure pixelator.c was compiled successfully ' +
                'and wasm/pixelator.js and wasm/pixelator.wasm exist.'
            );

            console.error(
                'WASM load error:',
                errMsg
            );
        }
    );
}


/**
 * Update WASM status badge.
 */
function setWasmStatus(statusState, text) {

    dom.wasmStatus.className =
        'status-badge ' + statusState;

    dom.wasmStatus
        .querySelector('.status-text')
        .textContent = text;
}


/* ================================================================
   FILE UPLOAD
   ================================================================ */

function setupUpload() {

    // Click upload area
    dom.dropzone.addEventListener(
        'click',
        () => dom.fileInput.click()
    );


    // Keyboard accessibility
    dom.dropzone.addEventListener(
        'keydown',
        e => {

            if (
                e.key === 'Enter' ||
                e.key === ' '
            ) {

                e.preventDefault();

                dom.fileInput.click();
            }
        }
    );


    // File picker
    dom.fileInput.addEventListener(
        'change',
        e => {

            const file = e.target.files[0];

            if (file) {
                handleFile(file);
            }

            // Allow selecting the same file again
            dom.fileInput.value = '';
        }
    );


    // Drag over
    dom.dropzone.addEventListener(
        'dragover',
        e => {

            e.preventDefault();

            dom.dropzone.classList.add(
                'drag-over'
            );
        }
    );


    // Drag leave
    dom.dropzone.addEventListener(
        'dragleave',
        e => {

            if (
                !dom.dropzone.contains(
                    e.relatedTarget
                )
            ) {

                dom.dropzone.classList.remove(
                    'drag-over'
                );
            }
        }
    );


    // Drop
    dom.dropzone.addEventListener(
        'drop',
        e => {

            e.preventDefault();

            dom.dropzone.classList.remove(
                'drag-over'
            );

            const file =
                e.dataTransfer.files[0];

            if (file) {
                handleFile(file);
            }
        }
    );
}


/* ================================================================
   HANDLE FILE
   ================================================================ */

function handleFile(file) {

    hideBanner('errorBanner');
    hideBanner('infoBanner');


    // Validate file
    if (!file) {

        showError(
            'No file was selected.'
        );

        return;
    }


    // Validate type
    if (!ACCEPTED_TYPES.includes(file.type)) {

        showError(
            `Unsupported file type: "${file.type || 'unknown'}". ` +
            'Please upload a PNG, JPG, JPEG, or WEBP file.'
        );

        return;
    }


    // Validate size
    if (file.size > MAX_FILE_SIZE) {

        const sizeMB =
            (file.size / (1024 * 1024))
                .toFixed(1);

        showError(
            `Image is too large (${sizeMB} MB). ` +
            'Maximum allowed size is 20 MB.'
        );

        return;
    }


    // Read file
    const reader = new FileReader();


    reader.onerror = () => {

        showError(
            'Failed to read the file. ' +
            'The file may be corrupted.'
        );
    };


    reader.onload = e => {

        const img = new Image();


        img.onerror = () => {

            showError(
                'Failed to load the image. ' +
                'The file may be corrupted or unsupported.'
            );
        };


        img.onload = () => {

            loadImageToCanvas(img);
        };


        img.src = e.target.result;
    };


    reader.readAsDataURL(file);
}


/* ================================================================
   LOAD IMAGE
   ================================================================ */

function loadImageToCanvas(img) {

    let {
        naturalWidth: w,
        naturalHeight: h
    } = img;


    /* ------------------------------------------------------------
       Safety resize for very large images
       ------------------------------------------------------------ */

    state.wasResized = false;


    if (
        w > MAX_PROCESS_SIZE ||
        h > MAX_PROCESS_SIZE
    ) {

        const scale =
            MAX_PROCESS_SIZE /
            Math.max(w, h);

        w = Math.round(w * scale);
        h = Math.round(h * scale);

        state.wasResized = true;
    }


    /* ------------------------------------------------------------
       Create canvas
       ------------------------------------------------------------ */

    const offscreen =
        document.createElement('canvas');

    offscreen.width = w;
    offscreen.height = h;


    const ctx =
        offscreen.getContext('2d');


    /*
     * White background.
     *
     * This prevents transparent images from
     * becoming black during processing.
     */
    ctx.fillStyle = '#ffffff';

    ctx.fillRect(
        0,
        0,
        w,
        h
    );


    /*
     * High-quality initial loading.
     */
    ctx.imageSmoothingEnabled = true;

    ctx.drawImage(
        img,
        0,
        0,
        w,
        h
    );


    /* ------------------------------------------------------------
       Extract pixels
       ------------------------------------------------------------ */

    const imageData =
        ctx.getImageData(
            0,
            0,
            w,
            h
        );


    /* ------------------------------------------------------------
       Store original
       ------------------------------------------------------------ */

    state.originalImageData =
        imageData;

    state.originalWidth = w;
    state.originalHeight = h;


    /* ------------------------------------------------------------
       Display original
       ------------------------------------------------------------ */

    displayOriginalCanvas(
        dom.originalCanvas,
        imageData
    );


    /* ------------------------------------------------------------
       UI information
       ------------------------------------------------------------ */

    dom.imageDimensions.textContent =
        `${img.naturalWidth} × ${img.naturalHeight}px`;


    if (state.wasResized) {

        dom.imageDimensions.textContent +=
            ` → ${w} × ${h}px`;
    }


    dom.resizeNotice.hidden =
        !state.wasResized;

    dom.imageInfo.hidden = false;


    if (state.wasResized) {

        showInfo(
            `Your image was scaled to ${w}×${h}px ` +
            `for processing.`
        );
    }


    /* ------------------------------------------------------------
       Show controls/results
       ------------------------------------------------------------ */

    dom.controlsSection.hidden = false;

    dom.resultsSection.hidden = false;


    /* ------------------------------------------------------------
       Enable generate button
       ------------------------------------------------------------ */

    dom.generateBtn.disabled =
        !state.wasmReady;


    /* ------------------------------------------------------------
       Automatically generate first preview
       ------------------------------------------------------------ */

    if (state.wasmReady) {

        runPixelation();
    }
}


/* ================================================================
   ORIGINAL CANVAS
   ================================================================ */

function displayOriginalCanvas(
    canvas,
    imageData
) {

    canvas.width =
        imageData.width;

    canvas.height =
        imageData.height;


    const ctx =
        canvas.getContext('2d');


    ctx.imageSmoothingEnabled =
        true;


    ctx.putImageData(
        imageData,
        0,
        0
    );
}


/* ================================================================
   PIXEL ART RESOLUTION
   ================================================================ */

/**
 * Converts the original image into a small pixel grid.
 *
 * Example:
 *
 * Original:
 * 1200 × 800
 *
 * Pixel size:
 * 10
 *
 * Processing image:
 * 120 × 80
 *
 * After C/WASM:
 * 120 × 80
 *
 * Final:
 * 1200 × 800
 *
 * The final enlargement uses nearest-neighbor
 * so every pixel stays sharp.
 */

function createLowResolutionImage(
    sourceImageData,
    pixelSize
) {

    const sourceWidth =
        sourceImageData.width;

    const sourceHeight =
        sourceImageData.height;


    /*
     * Calculate reduced dimensions.
     */
    const lowWidth =
        Math.max(
            1,
            Math.round(
                sourceWidth / pixelSize
            )
        );

    const lowHeight =
        Math.max(
            1,
            Math.round(
                sourceHeight / pixelSize
            )
        );


    /* ------------------------------------------------------------
       Source canvas
       ------------------------------------------------------------ */

    const sourceCanvas =
        document.createElement('canvas');

    sourceCanvas.width =
        sourceWidth;

    sourceCanvas.height =
        sourceHeight;


    const sourceCtx =
        sourceCanvas.getContext('2d');


    sourceCtx.putImageData(
        sourceImageData,
        0,
        0
    );


    /* ------------------------------------------------------------
       Low-resolution canvas
       ------------------------------------------------------------ */

    const lowCanvas =
        document.createElement('canvas');

    lowCanvas.width =
        lowWidth;

    lowCanvas.height =
        lowHeight;


    const lowCtx =
        lowCanvas.getContext('2d');


    /*
     * We WANT smoothing here.
     *
     * This converts groups of original pixels
     * into representative low-resolution pixels.
     */
    lowCtx.imageSmoothingEnabled = true;

    lowCtx.imageSmoothingQuality = 'high';


    lowCtx.drawImage(
        sourceCanvas,
        0,
        0,
        sourceWidth,
        sourceHeight,
        0,
        0,
        lowWidth,
        lowHeight
    );


    return lowCtx.getImageData(
        0,
        0,
        lowWidth,
        lowHeight
    );
}


/* ================================================================
   NEAREST-NEIGHBOR UPSCALE
   ================================================================ */

/**
 * Enlarges a small pixel-art ImageData back to the
 * original dimensions.
 *
 * imageSmoothingEnabled = false is the important part.
 */

function upscalePixelArt(
    lowResolutionImage,
    targetWidth,
    targetHeight
) {

    /* ------------------------------------------------------------
       Low-resolution canvas
       ------------------------------------------------------------ */

    const lowCanvas =
        document.createElement('canvas');

    lowCanvas.width =
        lowResolutionImage.width;

    lowCanvas.height =
        lowResolutionImage.height;


    const lowCtx =
        lowCanvas.getContext('2d');


    lowCtx.putImageData(
        lowResolutionImage,
        0,
        0
    );


    /* ------------------------------------------------------------
       Final canvas
       ------------------------------------------------------------ */

    const finalCanvas =
        document.createElement('canvas');

    finalCanvas.width =
        targetWidth;

    finalCanvas.height =
        targetHeight;


    const finalCtx =
        finalCanvas.getContext('2d');


    /*
     * CRITICAL:
     *
     * Disable browser interpolation.
     */
    finalCtx.imageSmoothingEnabled =
        false;

    finalCtx.imageSmoothingQuality =
        'low';


    /*
     * Nearest-neighbor enlargement.
     */
    finalCtx.drawImage(
        lowCanvas,
        0,
        0,
        lowResolutionImage.width,
        lowResolutionImage.height,
        0,
        0,
        targetWidth,
        targetHeight
    );


    return finalCtx.getImageData(
        0,
        0,
        targetWidth,
        targetHeight
    );
}


/* ================================================================
   DISPLAY RESULT
   ================================================================ */

function displayResultCanvas(
    canvas,
    imageData
) {

    canvas.width =
        imageData.width;

    canvas.height =
        imageData.height;


    const ctx =
        canvas.getContext('2d');


    ctx.imageSmoothingEnabled =
        false;

    ctx.imageSmoothingQuality =
        'low';


    ctx.putImageData(
        imageData,
        0,
        0
    );
}


/* ================================================================
   PIXELATION — WASM
   ================================================================ */

function runPixelation() {

    if (!state.wasmReady) {

        showError(
            'WebAssembly is not ready yet. ' +
            'Please wait or refresh the page.'
        );

        return;
    }


    if (!state.originalImageData) {

        showError(
            'Please upload an image first.'
        );

        return;
    }


    if (state.processing) {
        return;
    }


    state.processing = true;


    hideBanner(
        'errorBanner'
    );


    /* ------------------------------------------------------------
       Read controls
       ------------------------------------------------------------ */

    // Read pixel size from checked radio button
    const pixelSizeRadio = document.querySelector('input[name="pixelSize"]:checked');
    const pixelSize = pixelSizeRadio ? parseInt(pixelSizeRadio.value, 10) : 8;

    // Read palette size from checked radio button
    const paletteSizeRadio = document.querySelector('input[name="paletteSize"]:checked');
    const paletteSize = paletteSizeRadio ? parseInt(paletteSizeRadio.value, 10) : 16;

    // Read dithering mode from checked radio button
    const ditheringRadio = document.querySelector('input[name="dithering"]:checked');
    const ditherMode = ditheringRadio ? parseInt(ditheringRadio.value, 10) : 0;

    // Sliders
    const contrast = parseInt(dom.contrastSlider.value, 10);
    const edgeStrength = parseInt(dom.edgeSlider.value, 10);
    
    // Toggle
    const isGrayscale = dom.grayscaleToggle.checked;


    /* ------------------------------------------------------------
       Loading UI
       ------------------------------------------------------------ */

    dom.canvasLoading.hidden = false;
    dom.generateBtn.disabled = true;
    dom.downloadBtn.disabled = true;

    /*
     * Allow browser to display spinner before
     * synchronous WASM processing begins.
     */
    setTimeout(() => {
        try {
            /* ====================================================
               STEP 1
               Create a LOW-RESOLUTION image
               ==================================================== */

            const lowResolutionData = createLowResolutionImage(
                state.originalImageData,
                pixelSize
            );

            /* ====================================================
               STEP 2
               Clone low-resolution pixels
               ==================================================== */

            const srcData = new ImageData(
                new Uint8ClampedArray(lowResolutionData.data),
                lowResolutionData.width,
                lowResolutionData.height
            );

            /* ====================================================
               STEP 3
               Run C/WASM
               ==================================================== */

            const processedData = pixelatorWasm.pixelateAdvanced(srcData, {
                paletteSize,
                ditherMode,
                edgeStrength,
                contrast,
                grayscale: isGrayscale ? 1 : 0
            });


            /* ====================================================
               STEP 4
               UPSCALE USING NEAREST-NEIGHBOR
               ==================================================== */

            const finalImageData =
                upscalePixelArt(
                    processedData,
                    state.originalWidth,
                    state.originalHeight
                );


            /* ====================================================
               STEP 5
               DISPLAY
               ==================================================== */

            displayResultCanvas(
                dom.resultCanvas,
                finalImageData
            );


            /* ----------------------------------------------------
               Mark result available
               ---------------------------------------------------- */

            state.hasResult = true;

            dom.downloadBtn.disabled =
                false;


        } catch (err) {

            console.error(
                'Pixelation error:',
                err
            );


            showError(
                'Processing failed: ' +
                err.message +
                '. Please try a different image or pixel size.'
            );


        } finally {

            dom.canvasLoading.hidden =
                true;

            dom.generateBtn.disabled =
                false;

            state.processing =
                false;
        }

    }, 0);
}


/* ================================================================
   DOWNLOAD
   ================================================================ */

function downloadResult() {

    if (!state.hasResult) {

        showError(
            'No pixelated image to download yet.'
        );

        return;
    }


    try {

        const dataUrl =
            dom.resultCanvas.toDataURL(
                'image/png'
            );


        const link =
            document.createElement('a');


        link.download =
            'pixel-art.png';


        link.href =
            dataUrl;


        link.click();


    } catch (err) {

        console.error(
            'Download error:',
            err
        );


        showError(
            'Could not download the image: ' +
            err.message
        );
    }
}


/* ================================================================
   RESET
   ================================================================ */

function resetApp() {

    /* ------------------------------------------------------------
       Clear state
       ------------------------------------------------------------ */

    state.originalImageData =
        null;

    state.originalWidth =
        0;

    state.originalHeight =
        0;

    state.hasResult =
        false;

    state.processing =
        false;

    state.wasResized =
        false;


    /* ------------------------------------------------------------
       Clear canvases
       ------------------------------------------------------------ */

    clearCanvas(
        dom.originalCanvas
    );

    clearCanvas(
        dom.resultCanvas
    );


    /* ------------------------------------------------------------
       Reset pixel-art settings
       ------------------------------------------------------------ */

    const setRadio = (name, value) => {
        const radio = document.querySelector(`input[name="${name}"][value="${value}"]`);
        if (radio) radio.checked = true;
    };

    setRadio('stylePreset', 'classic');
    setRadio('pixelSize', '8');
    setRadio('paletteSize', '16');
    setRadio('dithering', '0');

    dom.contrastSlider.value = 0;
    dom.contrastDisplay.textContent = '0';
    
    dom.edgeSlider.value = 0;
    dom.edgeDisplay.textContent = '0';

    dom.grayscaleToggle.checked = false;

    /* ------------------------------------------------------------
       Hide sections
       ------------------------------------------------------------ */

    dom.controlsSection.hidden = true;
    dom.resultsSection.hidden = true;
    dom.imageInfo.hidden = true;

    dom.generateBtn.disabled = true;
    dom.downloadBtn.disabled = true;

    hideBanner('errorBanner');
    hideBanner('infoBanner');

    window.scrollTo({ top: 0, behavior: 'smooth' });
}

function clearCanvas(canvas) {
    canvas.width = 0;
    canvas.height = 0;
}

/* ================================================================
   UI HELPERS
   ================================================================ */

function showError(message) {
    dom.errorText.textContent = message;
    dom.errorBanner.hidden = false;
    dom.errorBanner.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
}

function showInfo(message) {
    dom.infoText.textContent = message;
    dom.infoBanner.hidden = false;
}

function hideBanner(id) {
    const el = document.getElementById(id);
    if (el) el.hidden = true;
}

/* ================================================================
   CONTROLS
   ================================================================ */

function setupControls() {

    const setRadio = (name, value) => {
        const radio = document.querySelector(`input[name="${name}"][value="${value}"]`);
        if (radio) radio.checked = true;
    };

    // Listen to all radio button changes
    document.querySelectorAll('input[type="radio"]').forEach(radio => {
        radio.addEventListener('change', (e) => {
            if (e.target.name === 'stylePreset') {
                const preset = e.target.value;
                if (preset === 'classic') {
                    setRadio('pixelSize', '8');
                    setRadio('paletteSize', '16');
                    setRadio('dithering', '0');
                    dom.contrastSlider.value = 0;
                    dom.edgeSlider.value = 30;
                } else if (preset === '16bit') {
                    setRadio('pixelSize', '6');
                    setRadio('paletteSize', '32');
                    setRadio('dithering', '1');
                    dom.contrastSlider.value = 10;
                    dom.edgeSlider.value = 20;
                } else if (preset === 'high') {
                    setRadio('pixelSize', '4');
                    setRadio('paletteSize', '64');
                    setRadio('dithering', '2');
                    dom.contrastSlider.value = 20;
                    dom.edgeSlider.value = 40;
                }
                dom.contrastDisplay.textContent = dom.contrastSlider.value;
                dom.edgeDisplay.textContent = dom.edgeSlider.value;
            } else {
                // If any other radio is changed manually, switch style preset to 'custom'
                setRadio('stylePreset', 'custom');
            }
            scheduleLivePreview();
        });
    });

    // Sliders
    dom.contrastSlider.addEventListener('input', () => {
        dom.contrastDisplay.textContent = dom.contrastSlider.value;
        setRadio('stylePreset', 'custom');
        scheduleLivePreview();
    });

    dom.edgeSlider.addEventListener('input', () => {
        dom.edgeDisplay.textContent = dom.edgeSlider.value;
        setRadio('stylePreset', 'custom');
        scheduleLivePreview();
    });

    // Grayscale
    dom.grayscaleToggle.addEventListener('change', () => {
        scheduleLivePreview();
    });

    // Buttons
    dom.generateBtn.addEventListener('click', runPixelation);
    dom.resetBtn.addEventListener('click', resetApp);
    dom.downloadBtn.addEventListener('click', downloadResult);
}


/* ================================================================
   LIVE PREVIEW
   ================================================================ */

let livePreviewTimer = null;

const LIVE_PREVIEW_DELAY = 300;


function scheduleLivePreview() {

    if (
        !state.wasmReady ||
        !state.originalImageData
    ) {

        return;
    }


    clearTimeout(
        livePreviewTimer
    );


    livePreviewTimer =
        setTimeout(
            () => {

                runPixelation();

            },
            LIVE_PREVIEW_DELAY
        );
}


/* ================================================================
   INITIALIZATION
   ================================================================ */

document.addEventListener(
    'DOMContentLoaded',
    () => {

        setupUpload();

        setupControls();

        initWasm();
    }
);