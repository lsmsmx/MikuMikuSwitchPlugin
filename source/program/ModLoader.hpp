#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <cstring>
#include "Allocator.hpp"

struct libcxx_string {
    union {
        struct { uint64_t cap; uint64_t size; char* data; } l;
        struct { unsigned char size_flag; char data[23]; } s;
    } u;

    libcxx_string() { std::memset((void*)this, 0, sizeof(libcxx_string)); }

    bool is_long() const { return u.s.size_flag & 1; }
    const char* c_str() const { return is_long() ? u.l.data : u.s.data; }
    size_t length() const { return is_long() ? u.l.size : (u.s.size_flag >> 1); }

    void assign(const char* str, size_t len) {
        if (is_long()) GameOperatorDelete(u.l.data);
        if (len < 23) {
            u.s.size_flag = static_cast<unsigned char>(len << 1);
            if (len > 0) std::memcpy((void*)u.s.data, (const void*)str, len);
            u.s.data[len] = 0;
        } else {
            size_t capacity = (len + 16) & ~15;
            u.l.data = (char*)GameOperatorNew(capacity);
            u.l.size = len;
            u.l.cap = capacity | 1;
            std::memcpy((void*)u.l.data, (const void*)str, len);
            u.l.data[len] = 0;
        }
    }

    libcxx_string& operator=(const char* str) {
        assign(str, std::strlen(str));
        return *this;
    }
    libcxx_string& operator=(const std::string& str) {
        assign(str.c_str(), str.length());
        return *this;
    }
    bool operator==(const std::string& other) const {
        return other == c_str();
    }
    bool operator==(const char* other) const {
        return std::strcmp(c_str(), other) == 0;
    }
    bool operator<(const std::string& other) const {
        return std::strcmp(c_str(), other.c_str()) < 0;
    }
    ~libcxx_string() {
        if (is_long()) GameOperatorDelete(u.l.data);
    }
    operator std::string() const { return std::string(c_str(), length()); }
};

// Template comparator for string classes (std::string, prj::string, libcxx_string)
template <typename T>
inline int compare_keys(const T& a, const libcxx_string& b) {
    return std::strcmp(a.c_str(), b.c_str());
}

// Overload for raw const char* strings
inline int compare_keys(const char* a, const libcxx_string& b) {
    return std::strcmp(a, b.c_str());
}

struct libcxx_list_node {
    libcxx_list_node* next;
    libcxx_list_node* prev;
    libcxx_string value;
};

struct libcxx_list {
    libcxx_list_node* end_next;
    libcxx_list_node* end_prev;
    size_t size;

    void push_back(const char* str) {
        size_t len = std::strlen(str);
        libcxx_list_node* node = (libcxx_list_node*)GameOperatorNew(sizeof(libcxx_list_node));

        std::memset((void*)node, 0, sizeof(libcxx_list_node));
        node->value.u.s.size_flag = 0;
        node->value.assign(str, len);

        libcxx_list_node* end_node = reinterpret_cast<libcxx_list_node*>(this);
        libcxx_list_node* last = this->end_prev;

        node->prev = last;
        node->next = end_node;

        last->next = node;
        this->end_prev = node;

        this->size++;
    }
};

// Base vector template suitable for arbitrary types and pointers
template <typename T>
struct libcxx_vector_base {
    T* begin_;
    T* end_;
    T* cap_;

    size_t size() const { return end_ - begin_; }
    size_t capacity() const { return cap_ - begin_; }
    T* begin() const { return begin_; }
    T* end() const { return end_; }
};

// String vector implementation inheriting from base template
struct libcxx_vector : public libcxx_vector_base<libcxx_string> {
    void insert_front(const std::vector<std::string>& strings) {
        size_t count = strings.size();
        if (count == 0) return;

        size_t old_size = size();
        size_t new_size = old_size + count;

        if (new_size > capacity()) {
            size_t new_cap = capacity() * 2;
            if (new_cap < new_size) new_cap = new_size;

            libcxx_string* new_buffer = (libcxx_string*)GameOperatorNew(new_cap * sizeof(libcxx_string));
            std::memset((void*)new_buffer, 0, new_cap * sizeof(libcxx_string));

            for (size_t i = 0; i < old_size; ++i) {
                std::memcpy((void*)&new_buffer[i + count], (const void*)&begin_[i], sizeof(libcxx_string));
            }

            begin_ = new_buffer;
            end_ = new_buffer + new_size;
            cap_ = new_buffer + new_cap;
        } else {
            std::memmove((void*)&begin_[count], (const void*)&begin_[0], old_size * sizeof(libcxx_string));
            end_ += count;
        }

        for (size_t i = 0; i < count; ++i) {
            begin_[i].u.s.size_flag = 0;
            begin_[i].assign(strings[i].c_str(), strings[i].length());
        }
    }
};

// Arithmetic key comparison for libcxx_map (uint32_t, int32_t, etc.)
template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
inline int compare_keys(const T& a, const T& b) {
    return (a > b) - (a < b);
}

// libc++ std::map structures (Clang ABI)
struct libcxx_map_node_base {
    libcxx_map_node_base* left;
    libcxx_map_node_base* right;
    libcxx_map_node_base* parent;
    bool is_black;
};

template <typename Key, typename Value>
struct libcxx_map_node : public libcxx_map_node_base {
    Key key;
    Value value;
};

template <typename Key, typename Value>
struct libcxx_map {
    using node_type = libcxx_map_node<Key, Value>;

    void* begin_node;
    void* root_node;
    size_t size;

    struct iterator {
        node_type* node;
        const libcxx_map* map;

        bool operator==(const iterator& other) const { return node == other.node; }
        bool operator!=(const iterator& other) const { return node != other.node; }

        std::pair<const Key, Value>& operator*() const {
            return *reinterpret_cast<std::pair<const Key, Value>*>(&node->key);
        }
        std::pair<const Key, Value>* operator->() const {
            return reinterpret_cast<std::pair<const Key, Value>*>(&node->key);
        }

        iterator& operator++() {
            if (node == nullptr) return *this;

            if (node->right != nullptr && node->right != (libcxx_map_node_base*)((uintptr_t)map + 8)) {
                node = (node_type*)node->right;
                while (node->left != nullptr && node->left != (libcxx_map_node_base*)((uintptr_t)map + 8)) {
                    node = (node_type*)node->left;
                }
            } else {
                node_type* p = (node_type*)node->parent;
                while (p != nullptr && p != (node_type*)((uintptr_t)map + 8) && node == (node_type*)p->right) {
                    node = p;
                    p = (node_type*)p->parent;
                }
                node = p;
            }
            return *this;
        }
    };

    libcxx_map() {
        begin_node = (void*)((uintptr_t)this + 8);
        root_node = nullptr;
        size = 0;
    }

    iterator begin() const {
        return iterator{ (node_type*)begin_node, this };
    }

    iterator end() const {
        return iterator{ (node_type*)((uintptr_t)this + 8), this };
    }

    template <typename SearchKey>
    iterator find(const SearchKey& search_key) const {
        node_type* curr = (node_type*)root_node;
        node_type* end_node = (node_type*)((uintptr_t)this + 8);

        while (curr != nullptr && curr != end_node) {
            int cmp = compare_keys(search_key, curr->key);
            if (cmp == 0) {
                return iterator{ curr, this };
            }
            if (cmp < 0) {
                curr = (node_type*)curr->left;
            } else {
                curr = (node_type*)curr->right;
            }
        }
        return end();
    }

    // Iterative tree cleanup to prevent stack overflow on large trees
    void clear() {
        node_type* end_node = (node_type*)((uintptr_t)this + 8);
        node_type* root = (node_type*)root_node;

        if (root == nullptr || root == end_node) {
            root_node = nullptr;
            begin_node = (void*)end_node;
            size = 0;
            return;
        }

        // Iterative depth-first traversal using a heap stack
        std::vector<node_type*> stack;
        stack.push_back(root);

        while (!stack.empty()) {
            node_type* node = stack.back();
            stack.pop_back();

            node_type* left = (node_type*)node->left;
            node_type* right = (node_type*)node->right;

            if (left != nullptr && left != end_node)
                stack.push_back(left);
            if (right != nullptr && right != end_node)
                stack.push_back(right);

            node->key.~Key();
            node->value.~Value();
            GameOperatorDelete(node);
        }

        root_node = nullptr;
        begin_node = (void*)end_node;
        size = 0;
    }

    // Safe for repeated invocations on persistent objects
    ~libcxx_map() {
        clear();
    }
};

static_assert(sizeof(libcxx_map<int, int>) == 24, "libcxx_map size must be exactly 24 bytes");

class ModLoader {
public:
    static std::vector<std::string> modDirectoryPaths;
    static void initMod(const std::string& path);
    static void init();
};
