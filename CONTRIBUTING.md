# Contributing to ReaBeat

Thank you for considering contributing to ReaBeat! Here's how you can help.

## Getting Started

### Prerequisites
- C++20 compiler (Clang 14+, GCC 12+, MSVC 2022+)
- CMake 3.22+
- ONNX Runtime (see vendor/onnxruntime/)

### Building

```bash
git clone https://github.com/b451c/ReaBeat.git
cd ReaBeat
git submodule update --init
# Download ONNX Runtime to vendor/onnxruntime/
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build .
```

Copy the built binary to REAPER's UserPlugins folder and restart REAPER.

## Code Style

- C++20, no exceptions in hot paths
- JUCE coding conventions for UI components
- All REAPER API calls wrapped in undo blocks
- `-Wall -Wextra` clean (zero warnings)
- No blocking dialogs (always-on-top window hides them)

## Pull Request Process

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/my-feature`)
3. Test in REAPER (unit tests verify code, not features)
4. Commit with clear messages
5. Push and open a Pull Request

## Reporting Bugs

Please include:
- REAPER version and OS
- Steps to reproduce
- Expected vs actual behavior
- Audio file details (genre, length, BPM) if detection-related

## Feature Requests

Open an issue with the `enhancement` label. Describe the use case, not just the feature.

## Architecture

See the Architecture section in [README.md](README.md) for module overview. Key principles:
- Detection pipeline is pure C++ (no Python dependency)
- All state changes are undoable (REAPER undo blocks)
- Beat positions are absolute seconds, not derived from tempo
