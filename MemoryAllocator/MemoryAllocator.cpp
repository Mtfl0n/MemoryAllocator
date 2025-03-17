#include "gtest/gtest.h"               
#include <windows.h>                    
#include <thread>                 
#include <vector>                     
#include <iostream>                 

constexpr size_t CHUNK_SIZE = 4 * 1024 * 1024;                        
constexpr size_t BLOCK_SIZE = 64;                              
constexpr size_t BLOCKS_PER_CHUNK = CHUNK_SIZE / BLOCK_SIZE;                         

struct Chunk {                                         
    std::atomic<uint64_t> bitmap[BLOCKS_PER_CHUNK / 64];                                     
    void* memory;                                           
    Chunk* next;                                                  

    Chunk() : memory(nullptr), next(nullptr) {                
        memory = VirtualAlloc(nullptr, CHUNK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);                         
        if (!memory) {                                     
            std::cerr << "Failed to allocate memory" << std::endl;                 
            throw std::bad_alloc();                            
        }
    }

    ~Chunk() {                                         
        if (memory) {                                     
            VirtualFree(memory, 0, MEM_RELEASE);              
        }
    }

    Chunk(const Chunk&) = delete;                           
    Chunk& operator=(const Chunk&) = delete;           
};

class MemoryAllocator {                                  
private:
    std::atomic<Chunk*> chunkList{ nullptr };                            
    std::mutex mutex;                                         

    int find_first_free_block(const std::atomic<uint64_t>* bitmap) const {                         
        for (size_t i = 0; i < BLOCKS_PER_CHUNK / 64; ++i) {              
            uint64_t value = bitmap[i].load(std::memory_order_relaxed);                   
            if (value != ~0ULL) {                                      
                uint64_t inverted = ~value;                      
                unsigned long index;                     
                if (_BitScanForward64(&index, inverted)) {                
                    return static_cast<int>(i * 64 + index);                     
                }
            }
        }
        return -1;                                          
    }                      

public:
    MemoryAllocator() = default;                                
    ~MemoryAllocator() { cleanup(); }                     

    void* allocate() {                                     
        while (true) {                                     
            Chunk* chunk = chunkList;                    
            while (chunk) {                          
                int blockIdx = find_first_free_block(chunk->bitmap);        
                if (blockIdx != -1) {                 
                    uint64_t expected = chunk->bitmap[blockIdx / 64].load(std::memory_order_relaxed);                        
                    uint64_t desired = expected | (1ULL << (blockIdx % 64));               
                    if (chunk->bitmap[blockIdx / 64].compare_exchange_weak(      
                        expected, desired, std::memory_order_acq_rel)) {             
                        return static_cast<char*>(chunk->memory) + (blockIdx * BLOCK_SIZE);                
                    }
                }
                chunk = chunk->next;                  
            }

            Chunk* newChunk = new Chunk();                 
            newChunk->next = chunkList;                      
            chunkList = newChunk;                      

            uint64_t expected = 0;                       
            uint64_t desired = 1;                         
            if (newChunk->bitmap[0].compare_exchange_weak(expected, desired, std::memory_order_acq_rel)) {      
                return newChunk->memory;                
            }
        }
    }                              

    void deallocate(void* ptr) {                     
        if (!ptr) return;                                

        std::lock_guard<std::mutex> lock(mutex);            
        Chunk* chunk = chunkList;                     
        uintptr_t address = reinterpret_cast<uintptr_t>(ptr);          

        while (chunk) {                            
            uintptr_t chunkStart = reinterpret_cast<uintptr_t>(chunk->memory);         
            uintptr_t chunkEnd = chunkStart + CHUNK_SIZE;        

            if (address >= chunkStart && address < chunkEnd) {         
                if (address % BLOCK_SIZE != 0) {                
                    std::cerr << "Invalid pointer" << std::endl;      
                    return;                           
                }
                size_t blockIdx = (address - chunkStart) / BLOCK_SIZE;              
                uint64_t expected = chunk->bitmap[blockIdx / 64].load(std::memory_order_relaxed);         
                uint64_t desired = expected & ~(1ULL << (blockIdx % 64));               
                chunk->bitmap[blockIdx / 64].store(desired, std::memory_order_release);         
                return;                                 
            }
            chunk = chunk->next;                    
        }
        std::cerr << "Pointer not found" << std::endl;         
    }                      

    void cleanup() {                                
        std::lock_guard<std::mutex> lock(mutex);        
        Chunk* current = chunkList;                 
        while (current) {                          
            Chunk* next = current->next;               
            delete current;                                
            current = next;                        
        }
        chunkList = nullptr;                           
    }                
};

TEST(MemoryAllocatorTest, AllocateAndDeallocateSingleBlock) {         
    MemoryAllocator allocator;                  
    void* p1 = allocator.allocate();           
    ASSERT_NE(p1, nullptr) << "Выделение блока не удалось";           
    allocator.deallocate(p1);                
}

TEST(MemoryAllocatorTest, AllocateAndDeallocateMultipleBlocks) {       
    MemoryAllocator allocator;               
    void* p1 = allocator.allocate();          
    void* p2 = allocator.allocate();          
    void* p3 = allocator.allocate();          
    ASSERT_NE(p1, nullptr) << "Ошибка выделения p1";      
    ASSERT_NE(p2, nullptr) << "Ошибка выделения p2";      
    ASSERT_NE(p3, nullptr) << "Ошибка выделения p3";      
    ASSERT_NE(p1, p2) << "p1 и p2 пересекаются";           
    ASSERT_NE(p1, p3) << "p1 и p3 пересекаются";           
    ASSERT_NE(p2, p3) << "p2 и p3 пересекаются";           
    allocator.deallocate(p1);                
    allocator.deallocate(p2);                
    allocator.deallocate(p3);                
}

TEST(MemoryAllocatorTest, ReallocateAfterDeallocate) {       
    MemoryAllocator allocator;               
    void* p1 = allocator.allocate();          
    ASSERT_NE(p1, nullptr) << "Ошибка выделения p1";      
    allocator.deallocate(p1);                
    void* p2 = allocator.allocate();          
    ASSERT_NE(p2, nullptr) << "Ошибка повторного выделения";      
    EXPECT_EQ(p1, p2) << "Ожидалось повторное использование блока";         
}

TEST(MemoryAllocatorTest, AllocateLargeNumberOfBlocks) {       
    MemoryAllocator allocator;               
    const size_t numBlocks = 1000;                 
    std::vector<void*> pointers;                   
    for (size_t i = 0; i < numBlocks; ++i) {      
        void* p = allocator.allocate();      
        ASSERT_NE(p, nullptr) << "Ошибка выделения блока #" << i;          
        pointers.push_back(p);                 
    }
    for (void* p : pointers) {                  
        allocator.deallocate(p);              
    }
}

TEST(MemoryAllocatorTest, DeallocateInvalidPointer) {      
    MemoryAllocator allocator;               
    void* invalidPtr = reinterpret_cast<void*>(0x12345678);     
    allocator.deallocate(invalidPtr);          
}

TEST(MemoryAllocatorTest, MultithreadedAllocateDeallocate) {      
    MemoryAllocator allocator;               
    const size_t numThreads = 4;               
    const size_t numAllocationsPerThread = 100;       
    std::vector<std::thread> threads;           

    auto allocateDeallocate = [&allocator]() {        
        for (size_t i = 0; i < numAllocationsPerThread; ++i) {     
            void* p = allocator.allocate();        
            ASSERT_NE(p, nullptr) << "Ошибка выделения в потоке";      
            allocator.deallocate(p);             
        }
        };

    for (size_t i = 0; i < numThreads; ++i) {      
        threads.emplace_back(allocateDeallocate);       
    }

    for (auto& thread : threads) {               
        thread.join();                          
    }
}

int main(int argc, char** argv) {             
    ::testing::InitGoogleTest(&argc, argv);           
    MemoryAllocator al;                         
    void* p1 = al.allocate();                  
    std::cout << "p1 -----  " << p1 << "\n";        
    std::cout << "12 ----- dealloc" << '\n';
    void* p2 = al.allocate();                  
    std::cout << "p2 -----  " << p2 << "\n";        
    al.deallocate(p2);
    std::cout << "p2 ----- dealloc" << "\n";
    return RUN_ALL_TESTS();                           
}                

