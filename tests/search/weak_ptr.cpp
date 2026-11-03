#include "common.h"
#include <cassert>
#include <cstdio>
#include "core/WeakPtr.h"

extern "C" void shared_ptr_increase_ref_count(vu32& count) { count = count + 1; }
struct Tracked
{
    static inline int alive = 0;
    Tracked() { alive++; }
    ~Tracked() { alive--; }
};

int main()
{
    {
        auto liveOwner = SharedPtr<Tracked>::MakeShared();
        WeakPtr<Tracked> onlyWeak(liveOwner);
        onlyWeak.Reset();
        onlyWeak.Reset();
        assert(!onlyWeak.Lock() && liveOwner.GetPointer());
        liveOwner.Reset();
        assert(Tracked::alive == 0);
    }
    auto owner = SharedPtr<Tracked>::MakeShared();
    WeakPtr<Tracked> first(owner), second(owner);
    first.Reset();
    assert(!first.Lock());
    assert(second.Lock());
    first.Reset();
    first = owner;
    first = first;
    assert(first.Lock());
    owner.Reset();
    assert(Tracked::alive == 0);
    assert(!first.Lock() && !second.Lock());
    first.Reset();
    second.Reset();
    puts("Weak focus references detach once and expire with their owner");
}
