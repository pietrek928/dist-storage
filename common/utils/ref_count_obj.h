#pragma once

#include <atomic>


template<class Tobj>
class TrackRef {
    Tobj *obj = nullptr;

    public:
    TrackRef() {}  // Add +1 to object reference
    TrackRef(Tobj *obj): obj(obj) {
        inc_ref();
    }
    TrackRef(const TrackRef &ref2) : obj(ref2.obj) {
        inc_ref();
    }
    TrackRef &operator=(const TrackRef &ref2){
        unref();
        obj = ref2.obj;
        inc_ref();
        return *this;
    }
    TrackRef(TrackRef &&other) : obj(other.obj) { other.obj = nullptr; }
    ~TrackRef() { unref(); }
    void inc_ref() {
        if (obj) {
            obj->inc_ref();
        }
    }
    void unref() {
        if (obj) {
            obj->unref();
            obj = nullptr;
        }
    }

    void move_to_void_ref(void  **void_ref) {
        *void_ref = obj;
        obj = nullptr;
    }
    static TrackRef copy_void_ref(void *void_ref) {
        TrackRef ref;
        ref.obj = static_cast<Tobj *>(void_ref);
        return ref;
    }

    inline operator bool() {
        return obj;
    }
    inline Tobj *operator->() {
        return obj;
    }
    inline Tobj *get() {
        return obj;
    }
};

template<typename Tref>
class RefCountObject {
    std::atomic<int> ref_count_ = 0;

    public:
    auto ref() {
        return TrackRef<Tref>(static_cast<Tref *>(this));
    }
    void inc_ref() {
        ref_count_.fetch_add(1);
    }
    void unref() {
        if (ref_count_.fetch_sub(1) == 1) {
            delete this;
        }
    }
    // make constructor virtual - essential for freeinig object
    virtual ~RefCountObject() {}
};

template<class Tobj>
class ReferenceContainer
    : public RefCountObject<Tobj>, public Tobj {};

template<class Tobj>
class ReferenceContainerPtr
    : public RefCountObject<Tobj> {

    Tobj *obj = nullptr;

    public:
    ReferenceContainerPtr(Tobj *obj) : obj(obj) {}
    ~ReferenceContainerPtr() {
        if (obj) {
            delete obj;
        }
    }
    Tobj *get() {
        return obj;
    }
    Tobj operator->() {
        return *obj;
    }
};
