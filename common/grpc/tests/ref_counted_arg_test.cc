#include <gtest/gtest.h>

#include <memory>
#include <string>

#include <grpc/ref_counted_arg.h>

namespace {

constexpr char kTestArgKey[] = "grpc.test.ref_counted_arg";
constexpr char kOtherArgKey[] = "grpc.test.other";

}  // namespace

TEST(RefCountedArgTest, MakeGetRoundTrip) {
    auto held = grpc_core::MakeRefCounted<RefCountedArgPtr<int>>(std::make_unique<int>(7));
    grpc_arg arg = makeRefCountedArg<kTestArgKey, RefCountedArgPtr<int>>(std::move(held));

    grpc_channel_args args;
    args.num_args = 1;
    args.args = &arg;

    auto got = getRefCountedArg<kTestArgKey, RefCountedArgPtr<int>>(&args);
    ASSERT_NE(got.get(), nullptr);
    ASSERT_NE(got->get(), nullptr);
    EXPECT_EQ(*got->get(), 7);

    got.reset();
    arg.value.pointer.vtable->destroy(arg.value.pointer.p);
}

TEST(RefCountedArgTest, MissingKeyReturnsNull) {
    auto held = grpc_core::MakeRefCounted<RefCountedArgPtr<int>>(std::make_unique<int>(1));
    grpc_arg arg = makeRefCountedArg<kTestArgKey, RefCountedArgPtr<int>>(std::move(held));

    grpc_channel_args args;
    args.num_args = 1;
    args.args = &arg;

    auto got = getRefCountedArg<kOtherArgKey, RefCountedArgPtr<int>>(&args);
    EXPECT_EQ(got.get(), nullptr);

    arg.value.pointer.vtable->destroy(arg.value.pointer.p);
}

TEST(RefCountedArgTest, NullArgsReturnsNull) {
    auto got = getRefCountedArg<kTestArgKey, RefCountedArgPtr<int>>(nullptr);
    EXPECT_EQ(got.get(), nullptr);
}

TEST(RefCountedArgTest, VtableCopyAndDestroy) {
    auto held = grpc_core::MakeRefCounted<RefCountedArgPtr<int>>(std::make_unique<int>(3));
    grpc_arg arg = makeRefCountedArg<kTestArgKey, RefCountedArgPtr<int>>(std::move(held));

    void* copied = arg.value.pointer.vtable->copy(arg.value.pointer.p);
    ASSERT_NE(copied, nullptr);
    EXPECT_EQ(copied, arg.value.pointer.p);

    arg.value.pointer.vtable->destroy(copied);
    arg.value.pointer.vtable->destroy(arg.value.pointer.p);
}
