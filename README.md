# waifu2x ncnn Vulkan

ncnn implementation of waifu2x with pipelined WebP or PNG output. It runs on Intel / AMD / Nvidia / Apple-Silicon with Vulkan API.

waifu2x-ncnn-vulkan uses [ncnn project](https://github.com/Tencent/ncnn) as the universal neural network inference framework.

GPU upscaling, image loading, and CPU image encoding run as bounded concurrent stages. A job succeeds only after every requested output has been encoded and committed.

## Usages

### Example Command

```shell
waifu2x-ncnn-vulkan.exe -i input.jpg -o output.webp -n 2 -s 2 -f webp -q 85 -c 2
```

For a chapter or nested directory tree, generate a UTF-8 TSV list and process it in one invocation:

```text
D:\manga\chapter-001\001.png	D:\staging\chapter-001\001.webp
D:\manga\chapter-001\002.png	D:\staging\chapter-001\002.webp
```

```shell
waifu2x-ncnn-vulkan.exe -l chapter-001.tsv -n 2 -s 2 -j 1:2:12 -f webp -q 85 -c 2
```

`1:2:12` matches the tested Ryzen 5 5600 setup (one loader, two GPU jobs, twelve independent output encoders). Tune it for the target machine and image dimensions; the built-in default remains `1:2:2`.

### Full Usages

```console
Usage: waifu2x-ncnn-vulkan -i infile -o outfile [options]...
       waifu2x-ncnn-vulkan -l listfile [options]...

  -h                   show this help
  -v                   verbose output
  -i input-path        input image path (jpg/png/webp) or directory
  -o output-path       output image path or directory
  -l list-path         UTF-8 TSV with input-path<TAB>output-path per line
  -n noise-level       denoise level (-1/0/1/2/3, default=0)
  -s scale             upscale ratio (1/2/4/8/16/32, default=2)
  -t tile-size         tile size (>=32/0=auto, default=0) can be 0,0,0 for multi-gpu
  -m model-path        waifu2x model path (default=models-cunet)
  -g gpu-id            gpu device to use (-1=cpu, default=auto) can be 0,1,2 for multi-gpu
  -j load:proc:save    thread count for load/proc/save (default=1:2:2) can be 1:2,2,2:2 for multi-gpu
  -x                   enable tta mode
  -f format            output format (webp/png, default=webp)
  -q quality           WebP quality (0-100, default=85)
  -c method            WebP compression method (0-6, default=2)

WebP lossless=0 and thread_level=0 are fixed
```

- `input-path` accepts JPG, PNG, or WebP. Output can be lossy WebP or lossless PNG.
- `-f` and every explicit output extension must match. List mode does not permit mixed output formats. Without `-f`, the format is WebP.
- `-q` and `-c` are accepted only for WebP. PNG uses the platform PNG encoder and has no configurable encoder parameters.
- Directory mode processes supported images directly inside one directory. Use list mode to process nested directory trees in one process.
- `list-path` is UTF-8 TSV with one input and output path separated by a tab per line. Empty lines and lines beginning with `#` are ignored.
- Output parent directories must already exist. Final output files and their `.tmp` paths must not exist before the job starts.
- `noise-level` = noise level, large value means strong denoise effect, -1 = no effect
- `scale` = scale level, 1 = no scaling, 2 = upscale 2x
- `tile-size` = tile size, use smaller value to reduce GPU memory usage, default selects automatically
- `load:proc:save` = independent worker counts for image decoding, waifu2x processing, and output encoding. WebP encoders remain single-threaded internally, so parallelism comes from the `save` worker count. More workers increase peak memory use.
- The process exits with code 0 only when every input was decoded, processed, encoded, and committed successfully.

If you encounter a crash or error, try upgrading your GPU driver:

- Intel: https://downloadcenter.intel.com/product/80939/Graphics-Drivers
- AMD: https://www.amd.com/en/support
- NVIDIA: https://www.nvidia.com/Download/index.aspx

## Build from Source

1. Download and setup the Vulkan SDK from https://vulkan.lunarg.com/
  - For Linux distributions, you can either get the essential build requirements from package manager
```shell
dnf install vulkan-headers vulkan-loader-devel
```
```shell
apt-get install libvulkan-dev
```
```shell
pacman -S vulkan-headers vulkan-icd-loader
```

2. Check out this repository with all submodules

```shell
git submodule update --init --recursive
```

3. Build with CMake
  - You can pass -DUSE_STATIC_MOLTENVK=ON option to avoid linking the vulkan loader library on MacOS

```shell
mkdir build
cd build
cmake ../src
cmake --build . -j 4
```

## Sample Images

### Original Image

![origin](images/0.jpg)

### Upscale 2x with ImageMagick

```shell
convert origin.jpg -resize 200% output.png
```

![browser](images/1.png)

### Upscale 2x with ImageMagick Lanczo4 Filter

```shell
convert origin.jpg -filter Lanczos -resize 200% output.png
```

![browser](images/4.png)

### Upscale 2x with waifu2x noise=2 scale=2

```shell
waifu2x-ncnn-vulkan.exe -i origin.jpg -o output.webp -n 2 -s 2
```

![waifu2x](images/2.png)

## Original waifu2x Project

- https://github.com/nagadomi/waifu2x
- https://github.com/lltcggie/waifu2x-caffe

## Other Open-Source Code Used

- https://github.com/Tencent/ncnn for fast neural network inference on ALL PLATFORMS
- https://github.com/webmproject/libwebp for encoding and decoding Webp images on ALL PLATFORMS
- https://github.com/nothings/stb for decoding images on Linux / MacOS
- https://github.com/tronkko/dirent for listing files in directory on Windows
