# Changelog

## 1.3.0

- Remove pybind11
- Transition to ABI3 compatible wheel
- Minimum Python version is 3.9
- License in Apache 2

## 1.2.5

- Fix sdist

## 1.2.4

- Fixes for BSDs
- Fix compile on MUSL
- Update license classifier in `setup.py`

## 1.2.3

- Remove `-mfma` from compilation

## 1.2.2

- Remove AVX2 from x86

## 1.2.1

- Add macOS
- Fix 32-bit ARM

## 1.2.0

- Add 32-bit ARM

## 1.1.0

- Enable building from source without using meson

## 1.0.0

- Changed `Process10ms` to take `bytes` only (input audio)
- Changed `Process10ms` to return an object with `audio` and `is_speech`
- Removed dependency on numpy

## 0.0.1

- Initial version
