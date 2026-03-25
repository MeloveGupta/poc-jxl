# jxl_poc

Standalone JPEG XL decode proof-of-concept using the libjxl C API.

## Dependencies

- libjxl (dev headers): `sudo apt install libjxl-dev`
- C++17 compiler

## Build

```
g++ -std=c++17 -o jxl_poc jxl_poc.cpp -ljxl
```

## Usage

```
./jxl_poc input.jxl
./jxl_poc input.jxl output.ppm
```

Second argument is optional - if given, writes a P6 PPM (RGB, alpha stripped).

## Output

```
manual magic detect: bare codestream
JxlSignatureCheck: valid (type=codestream)

--- image info ---
  dimensions:  4 x 4
  bit depth:   8
  channels:    3
  has alpha:   no
  animated:    no

decoded 4x4 (64 bytes)
wrote output.ppm