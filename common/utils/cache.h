#pragma once

#include <map>
#include <unordered_map>
#include <mutex>
#include <chrono>


template <class Tcache, class Tkey, class Tvalue>
class CacheHolder {
    Tcache *cache_ptr;
    Tkey key;
    Tvalue value;

    public:
    CacheHolder(Tcache *cache_ptr, const Tkey &key, const Tvalue &value)
        : cache_ptr(cache_ptr), key(key), value(value) {}

    operator Tvalue() const {
        return value;
    }

    ~CacheHolder() {
        if (cache_ptr) {
            cache_ptr->release(key);
        }
    }
};

template<class Tkey, class Tvalue>
class Cache {
    struct CacheItem {
        double create_timestamp = 0;
        double access_timestamp = 0;
        int refs = 0;
        Tvalue value;
    };

    typedef std::unordered_map<Tkey, CacheItem> Tcache_map;

    std::mutex lock;
    Tcache_map cache_items;
    std::multimap<double, Tkey> key_by_age;
    std::multimap<double, Tkey> key_by_access;

    size_t max_size;
    double max_age;
    double max_inactivity;

    void remove_keys_from_index() {
        for (auto it = key_by_age; it != key_by_age.end(); ) {
            if (
                cache_items.find(it->second) == cache_items.end()
            ) {
                it = key_by_age.erase(it);
            } else {
                it++;
            }
        }
        for (auto it = key_by_access; it != key_by_access.end(); ) {
            if (
                cache_items.find(it->second) == cache_items.end()
            ) {
                it = key_by_access.erase(it);
            } else {
                it++;
            }
        }
    }

    void flush_unsafe() {
        auto it = key_by_age.begin();
        auto timestamp = get_timestamp();
        while (it != key_by_age.end()) {
            if (it->first < timestamp - max_age) {
                if (it->second.refs == 0) {
                    auto it2 = cache_items.find(it->second);
                    if (it2 != cache_items.end()) {
                        cache_items.erase(it2);
                    }
                    it = key_by_age.erase(it);
                } else {
                    it ++;
                }
            } else {
                break;
            }
        }

        it = key_by_access.begin();
        while (it != key_by_access.end()) {
            if (it->first < timestamp - max_inactivity) {
                if (it->second.refs == 0) {
                    auto it2 = cache_items.find(it->second);
                    if (it2 != cache_items.end()) {
                        cache_items.erase(it2);
                    }
                    it = key_by_access.erase(it);
                } else {
                    it++;
                }
            } else {
                break;
            }
        }
        while (it != key_by_access.end() && cache_items.size() > max_size) {
            if (it->second.refs == 0) {
                auto it2 = cache_items.find(it->second);
                if (it2 != cache_items.end()) {
                    cache_items.erase(it2);
                }
                it = key_by_access.erase(it);
            } else {
                it++;
            }
        }
    }

    double get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        auto seconds = std::chrono::duration<double>(duration).count();
        return seconds;
    }

    public:
    Cache(size_t max_size, double max_age, double max_inactivity)
        : max_size(max_size), max_age(max_age), max_inactivity(max_inactivity) {}

    template<class Tret = Tvalue, typename Tcreate>
    Tret get_reserve(const Tkey &key, Tcreate && create) {
        {
            std::lock_guard<std::mutex> guard(lock);
            auto it = cache_items.find(key);
            if (it != cache_items.end()) {
                it->second.refs++;
                it->second.access_timestamp = get_timestamp();
                return CacheHolder<Cache<Tkey, Tvalue>, Tkey, Tvalue>(this, key, it->second.value);
            }
        }
        Tvalue val = create(key);
        {
            std::lock_guard<std::mutex> guard(lock);
            auto timestamp = get_timestamp();
            auto it = cache_items.emplace(key, {
                .create_timestamp = timestamp,
                .access_timestamp = timestamp,
                .refs = 1,
                .value = std::move(val)
            });
            return CacheHolder<Cache<Tkey, Tvalue>, Tkey, Tvalue>(this, key, it->second.value);
        }
    }

    void release(const Tkey &key) {
        std::lock_guard<std::mutex> guard(lock);
        auto it = cache_items.find(key);
        if (it != cache_items.end()) {
            it->second.refs--;
        }
        flush_unsafe();
    }

    void flush() {
        std::lock_guard<std::mutex> guard(lock);
        flush_unsafe();
    }

    void flush_all() {
        std::lock_guard<std::mutex> guard(lock);
        for (auto it = cache_items.begin(); it != cache_items.end(); ) {
            if (it->second.refs == 0) {
                it = cache_items.erase(it);
            } else {
                it++;
            }
        }
        remove_keys_from_index();
    }
};
