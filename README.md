# jxl_poc

JPEG XL decode proof-of-concept using the libjxl C API.

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
