/*
    ██████ ██   ██  ██████   ██████
   ██      ██   ██ ██    ██ ██         Clean Header-Only Classes
   ██      ███████ ██    ██ ██         Copyright (C)2020 Julian Storer
   ██      ██   ██ ██    ██ ██
    ██████ ██   ██  ██████   ██████    https://github.com/julianstorer/choc

   This file is part of the CHOC C++ collection - see the github page to find out more.

   The code in this file is provided under the terms of the ISC license:

   Permission to use, copy, modify, and/or distribute this software for any purpose with
   or without fee is hereby granted, provided that the above copyright notice and this
   permission notice appear in all copies.

   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO
   THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT
   SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR
   ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
   CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE
   OR PERFORMANCE OF THIS SOFTWARE.
*/

#ifndef CHOC_POOL_HEADER_INCLUDED
#define CHOC_POOL_HEADER_INCLUDED

#include <vector>
#include <cstdlib>
#include "../platform/choc_Assert.h"

namespace choc::memory
{

//==============================================================================
/**
    A pool-based object allocator.

    A pool provides a way to quickly allocate objects whose lifetimes are tied to
    the lifetime of the pool rather than managed in the traditional ways.

    Calling Pool::allocate() will return a reference to a new object which will
    survive until the pool itself is later deleted or reset, at which point all
    the objects will be destroyed.

    Because all objects are allocated linearly from large heap blocks, allocation
    has very low overhead and is compact. This class also doesn't attempt to be
    thread-safe, which also helps to make it fast.

    Obviously a pool doesn't suit all use-cases, but in situations where you need
    to quickly allocate a large batch of objects and then use them for the lifetime of
    a finite task, it can be a very efficient tool. It's especially helpful if the group
    of objects have complicated ownership patterns which would make normal ownership
    techniques cumbersome.
*/
class Pool
{
public:
    Pool();
    ~Pool() = default;

    Pool (Pool&&) = default;
    Pool (const Pool&) = delete;
    Pool& operator= (Pool&&) = default;
    Pool& operator= (const Pool&) = delete;

    /// Resets the pool, deleting all the objects that have been allocated.
    void reset();

    /// Returns a reference to a newly-constructed object in the pool.
    /// The caller must not attempt to delete the object that is returned, it'll be
    /// automatically deleted when the pool itself is reset or deleted.
    template <typename ObjectType, typename... Args>
    ObjectType& allocate (Args&&... constructorArgs);


private:
    //==============================================================================
    static constexpr const size_t itemAlignment = 16;
    static constexpr const size_t blockSize = 65536;

    struct Item;
    struct Block;
    std::vector<std::unique_ptr<Block>> blocks;
    void addBlock();
};


//==============================================================================
//        _        _           _  _
//     __| |  ___ | |_   __ _ (_)| | ___
//    / _` | / _ \| __| / _` || || |/ __|
//   | (_| ||  __/| |_ | (_| || || |\__ \ _  _  _
//    \__,_| \___| \__| \__,_||_||_||___/(_)(_)(_)
//
//   Code beyond this point is implementation detail...
//
//==============================================================================

inline Pool::Pool()   { reset(); }

inline void Pool::reset()
{
    blocks.clear();
    blocks.reserve (32);
    addBlock();
}

struct Pool::Item
{
    size_t size;
    using Destructor = void(void*);
    Destructor* destructor;
    alignas(itemAlignment) char objectSpace[1];
};

struct Pool::Block
{
    Block() = default;
    Block (Block&&) = delete;
    Block (const Block&) = delete;

    ~Block()
    {
        for (size_t i = 0; i < nextItemOffset;)
        {
            auto item = getItem (i);

            if (item->destructor != nullptr)
                item->destructor (item->objectSpace);

            i += item->size;
        }
    }

    static constexpr const size_t itemHeaderSize = offsetof (Item, objectSpace);

    static constexpr size_t getSpaceNeeded (size_t size)    { return (size + (itemHeaderSize + itemAlignment - 1u)) & ~(itemAlignment - 1u); }
    bool hasSpaceFor (size_t size) const noexcept           { return nextItemOffset + size <= space.size(); }
    Item* getItem (size_t position) noexcept                { return reinterpret_cast<Item*> (space.data() + position); }

    Item& allocateItem (size_t size)
    {
        auto i = getItem (nextItemOffset);
        i->size = size;
        i->destructor = nullptr;
        nextItemOffset += size;
        return *i;
    }

    size_t nextItemOffset = 0;
    alignas(itemAlignment) std::array<char, blockSize - 32> space;
};

inline void Pool::addBlock()
{
    blocks.emplace_back (std::make_unique<Block>());
}

template <typename ObjectType, typename... Args>
ObjectType& Pool::allocate (Args&&... constructorArgs)
{
    static constexpr auto itemSize = Block::getSpaceNeeded (sizeof (ObjectType));

    static_assert (itemSize <= sizeof (Block::space),
                   "Object size is larger than the maximum for the Pool class");

    if (! blocks.back()->hasSpaceFor (itemSize))
        addBlock();

    auto& newItem = blocks.back()->allocateItem (itemSize);
    auto allocatedObject = new (newItem.objectSpace) ObjectType (std::forward<Args> (constructorArgs)...);

    if constexpr (! std::is_trivially_destructible<ObjectType>::value)
        newItem.destructor = [] (void* t) { static_cast<ObjectType*> (t)->~ObjectType(); };

    return *allocatedObject;
}


} // namespace choc::memory

#endif // CHOC_POOL_HEADER_INCLUDED
