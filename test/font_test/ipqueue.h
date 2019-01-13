// An inter-process queue. Only works on Linux.

#include "loguru.hpp"

#include <scaffold/types.h>

// clang-format off
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <sys/mman.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <limits.h>
// clang-format on
#include <new>

namespace ipqueue {

template <typename T> struct alignas(64) QueueHeader; // Fwd decl

template <typename T> struct ShmQueue {
    // TODO: remove this restriction
    static_assert(std::is_trivially_copyable<T>::value, "");
    static_assert(std::is_trivially_copy_assignable<T>::value, "");
    static_assert(std::is_trivially_move_constructible<T>::value, "");
    static_assert(std::is_trivially_move_assignable<T>::value, "");
    static_assert(std::is_trivially_destructible<T>::value, "");

    QueueHeader<T> *_header;
    int _shm_fd;

    ShmQueue()
        : _header(nullptr)
        , _shm_fd(-1) {}

    ShmQueue(const ShmQueue &other) = delete;
    ShmQueue &operator=(const ShmQueue &other) = delete;

    ShmQueue(ShmQueue &&other) {
        _header = other._header;
        _shm_fd = other._shm_fd;

        other._header = nullptr;
        _shm_fd = -1;
    }

    ShmQueue &operator=(ShmQueue &&other) {
        if (this != &other) {
            _header = other._header;
            _shm_fd = other._shm_fd;
            other._header = nullptr;
            other._shm_fd = -1;
        }
    }

    ~ShmQueue() { _header = nullptr; }
};

// Create a new queue and initialize given queue object
template <typename T> void init_new(ShmQueue<T> &q, const char *pathname, size_t capacity);

// Initialize the given queue object using already existing queue in shared memory
template <typename T> void init_from_existing(ShmQueue<T> &q, const char *pathname);

// Push a new item. If queue is full we wait for space to become available.
template <typename T> void push_back(ShmQueue<T> &q, const T &item);

// Pops an item from the queue. Blocks if queue is empty.
template <typename T> T pop_front(ShmQueue<T> &q);

// -- Impl

template <typename T> struct alignas(64) QueueHeader {
    pthread_mutex_t _mutex;
    pthread_cond_t _space_available_condvar;
    pthread_cond_t _element_available_condvar;

    size_t _capacity; // Total number of slots available
    size_t _offset;   // Number of slots in wrap-around chunk
    size_t _size;     // Number of elements in queue

    char _name[PATH_MAX];
    char _error_msg[128];

    alignas(T) u8 _storage[];
};

template <typename T> inline size_t total_memory(size_t capacity) {
    return sizeof(QueueHeader<T>) + capacity * sizeof(T);
}

template <typename T> void init_new(ShmQueue<T> &q, const char *pathname, size_t capacity) {
    q._shm_fd = shm_open(pathname, O_CREAT | O_RDWR | O_EXCL, S_IRUSR | S_IWUSR);
    CHECK_F(q._shm_fd != -1, "ShmQueue - Failed to create shm %s - errno - %s", pathname, strerror(errno));

    size_t len = total_memory<T>(capacity);

    // Set the size of the shared file
    int r;

    r = ftruncate(q._shm_fd, len);

    CHECK_F(r != -1, "ShmQueue - Failed to ftruncate - %s - errno - %s", pathname, strerror(errno));

    // Allocate the memory required using mmap
    q._header = reinterpret_cast<QueueHeader<T> *>(
        mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FILE, q._shm_fd, 0));

    CHECK_F(q._header != MAP_FAILED, "ShmQueue - mmap failed while creating new queue: %s, errno - %s",
            pathname, strerror(errno));

    // Set up the mutex and condvar

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    memset(&q._header->_mutex, 0, sizeof(pthread_mutex_t));
    pthread_mutex_init(&q._header->_mutex, &mutex_attr);

    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);

    memset(&q._header->_element_available_condvar, 0, sizeof(pthread_cond_t));
    memset(&q._header->_space_available_condvar, 0, sizeof(pthread_cond_t));
    pthread_cond_init(&q._header->_element_available_condvar, &cond_attr);
    pthread_cond_init(&q._header->_space_available_condvar, &cond_attr);

    pthread_mutexattr_destroy(&mutex_attr);
    pthread_condattr_destroy(&cond_attr);

    q._header->_size = 0;
    q._header->_capacity = capacity;
    q._header->_offset = 0;

    strncpy((char *)q._header->_name, pathname, sizeof(q._header->_name) - 1);
    q._header->_name[sizeof(q._header->_name) - 1] = '\0';
    memset(q._header->_error_msg, 0, sizeof(q._header->_error_msg));
}

template <typename T> void init_from_existing(ShmQueue<T> &q, const char *pathname) {
    q._shm_fd = shm_open(pathname, O_RDWR, S_IRUSR | S_IWUSR);
    CHECK_F(q._shm_fd != -1, "ShmQueue - Failed to create shm %s", pathname);

    // First we just map the header. Then get the total length of the buffer and then map again.

    // Allocate the memory required using mmap
    q._header = reinterpret_cast<QueueHeader<T> *>(
        mmap(nullptr, sizeof(QueueHeader<T>), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FILE, q._shm_fd, 0));

    CHECK_F(q._header != MAP_FAILED, "ShmQueue - Failed to mmap header - %s - error - %", pathname,
            strerror(errno));

    const i32 len = total_memory<T>(q._header->_capacity);

    munmap(q._header, sizeof(QueueHeader<T>));

    q._header = reinterpret_cast<QueueHeader<T> *>(
        mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FILE, q._shm_fd, 0));

    CHECK_F(q._header != nullptr, "ShmQueue - Failed to mmap - %s", pathname);
}

template <typename T> void grow(ShmQueue<T> &q);

template <typename T> void push_back(ShmQueue<T> &q, const T &item) {
    int r;

    // First see if we have enough space
    r = pthread_mutex_lock(&q._header->_mutex);

    CHECK_F(r == 0, "pthread_mutex_lock");

    // LOG_F(INFO, "Locked queue");

    while (!(q._header->_size < q._header->_capacity)) {
        r = pthread_cond_wait(&q._header->_space_available_condvar, &q._header->_mutex);
        CHECK_F(r == 0, "pthread_cond_wait failed");
    }

    const i32 slot = (q._header->_offset + q._header->_size) % q._header->_capacity;

    // Copy construct the value
    new (&q._header->_storage[slot * sizeof(T)]) T(item);

    // LOG_F(INFO, "Pushed %i at %i", *((i32 *)&q._header->_storage[slot * sizeof(T)]), slot);

    ++q._header->_size;

    pthread_cond_signal(&q._header->_element_available_condvar);
    pthread_mutex_unlock(&q._header->_mutex);

    // LOG_F(INFO, "Done pushing");
}

template <typename T> T pop_front(ShmQueue<T> &q) {
    int r;

    r = pthread_mutex_lock(&q._header->_mutex);

    CHECK_F(r == 0, "pthread_mutex_lock failed");

    while (q._header->_size == 0) {
        r = pthread_cond_wait(&q._header->_element_available_condvar, &q._header->_mutex);
        CHECK_F(r == 0, "pthread_cond_wait failed");
    }

    const i32 head_slot = q._header->_offset;

    T *storage = reinterpret_cast<T *>(q._header->_storage);
    T item = std::move(storage[head_slot]);

    // LOG_F(INFO, "Popped %i from slot %i", item, head_slot);

    --q._header->_size;

    if (q._header->_size == 0) {
        q._header->_offset = 0;
    } else {
        q._header->_offset = (head_slot + 1) % q._header->_capacity;
    }

    pthread_cond_signal(&q._header->_space_available_condvar);
    pthread_mutex_unlock(&q._header->_mutex);

    return item;
}

} // namespace ipqueue
