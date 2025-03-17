# Thread-Safe Memory Allocator

This is a lightweight, thread-safe memory allocator designed for Windows. It allocates memory in fixed 64-byte blocks within 4MB chunks, using `VirtualAlloc` for memory management and atomic bitmaps for tracking block states. The allocator is built with performance and concurrency in mind, featuring minimal locking and safe multi-threaded operation.

## Features
- **Fixed-size allocation**: 64-byte blocks, 4MB chunks.
- **Thread safety**: Uses `std::atomic` for bitmap updates and `std::mutex` for synchronization.
- **Efficient memory tracking**: Bitmap-based block management (1 bit per block).
- **Windows-specific**: Relies on `VirtualAlloc` and `VirtualFree`.
- **Unit tests**: Comprehensive tests with Google Test, covering single-threaded and multi-threaded scenarios.

## Usage
1. Clone the repository:
   ```bash
   git clone https://github.com/Mtfl0n/MemoryAllocator.git
   ```
2. Build with a C++ compiler supporting C++11 (e.g., MSVC).

## Example
```cpp
MemoryAllocator allocator;
void* ptr = allocator.allocate(); // Allocate a 64-byte block
allocator.deallocate(ptr);        // Free the block
```

## TODO
- Add memory alignment support for SIMD/cache optimization.
- Improve performance with free block caching.
- Enhance multi-threaded contention handling.
