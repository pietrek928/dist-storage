#pragma once

#include <grpc/impl/codegen/grpc_types.h>
#include <core/util/ref_counted.h>
#include <core/util/ref_counted_ptr.h>


// A native gRPC ref-counted object that owns the Stub
template<class Tobj>
class RefCountedArgPtr : public grpc_core::RefCounted<RefCountedArgPtr<Tobj>> {
public:
    // Takes ownership of the unique_ptr returned by NewStub()
    explicit RefCountedArgPtr(std::unique_ptr<Tobj> stub)
        : obj_(std::move(stub)) {}

    // Convenience operators so it behaves like a pointer
    Tobj* get() const { return obj_.get(); }
    Tobj* operator->() const { return obj_.get(); }

private:
    std::unique_ptr<Tobj> obj_;
};

template <typename T>
struct GrpcRefCountedArgWrapper {
    static void* Copy(void* p) {
        if (!p) return nullptr;
        // Cast back to T* and increment the gRPC ref count.
        // T::Ref() returns a RefCountedPtr, .release() yields the raw pointer
        // while leaving the internal ref count incremented.
        return static_cast<void*>(static_cast<T*>(p)->Ref().release());
    }

    static void Destroy(void* p) {
        if (!p) return;
        // Decrement the gRPC ref count. If it hits 0, gRPC deletes it.
        static_cast<T*>(p)->Unref();
    }

    static int Cmp(void* a, void* b) {
        return (a < b) ? -1 : (a > b);
    }

    static const grpc_arg_pointer_vtable VTable;
};

template <typename T>
const grpc_arg_pointer_vtable GrpcRefCountedArgWrapper<T>::VTable = {
    GrpcRefCountedArgWrapper<T>::Copy,
    GrpcRefCountedArgWrapper<T>::Destroy,
    GrpcRefCountedArgWrapper<T>::Cmp
};

/**
 * MakeRefCountedArg: Takes ownership of one reference.
 */
template <const char* Key, typename T>
grpc_arg makeRefCountedArg(grpc_core::RefCountedPtr<T> ptr) {
    grpc_arg arg;
    arg.type = GRPC_ARG_POINTER;
    arg.key = const_cast<char*>(Key);
    // .release() transfers the reference ownership to the grpc_arg
    arg.value.pointer.p = static_cast<void*>(ptr.release());
    arg.value.pointer.vtable = &GrpcRefCountedArgWrapper<T>::VTable;
    return arg;
}

/**
 * GetRefCountedArg: Returns a RefCountedPtr safely.
 */
template <const char* Key, typename T>
grpc_core::RefCountedPtr<T> getRefCountedArg(const grpc_channel_args* args) {
    if (!args) return nullptr;
    std::string_view k(Key);
    for (size_t i = 0; i < args->num_args; ++i) {
        if (args->args[i].type == GRPC_ARG_POINTER && k == args->args[i].key) {
            auto* obj = static_cast<T*>(args->args[i].value.pointer.p);
            // Return a new RefCountedPtr, safely incrementing the count for the caller
            return obj->Ref();
        }
    }
    return nullptr;
}
