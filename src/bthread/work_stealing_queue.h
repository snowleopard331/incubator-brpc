// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

// bthread - A M:N threading library to make applications more concurrent.

// Date: Tue Jul 10 17:40:58 CST 2012

#ifndef BTHREAD_WORK_STEALING_QUEUE_H
#define BTHREAD_WORK_STEALING_QUEUE_H

#include "butil/macros.h"
#include "butil/atomicops.h"
#include "butil/logging.h"

namespace bthread {

template <typename T>
class WorkStealingQueue {
public:
    WorkStealingQueue()
        : _bottom(1)
        , _capacity(0)
        , _buffer(NULL)
        , _top(1) {
    }

    ~WorkStealingQueue() {
        delete [] _buffer;
        _buffer = NULL;
    }

    int init(size_t capacity) {
        if (_capacity != 0) {
            LOG(ERROR) << "Already initialized";
            return -1;
        }
        if (capacity == 0) {
            LOG(ERROR) << "Invalid capacity=" << capacity;
            return -1;
        }
        // 初始化容量要为2的整数次幂
        if (capacity & (capacity - 1)) {
            LOG(ERROR) << "Invalid capacity=" << capacity
                       << " which must be power of 2";
            return -1;
        }
        // 初始化模板类型的数组
        _buffer = new(std::nothrow) T[capacity];
        if (NULL == _buffer) {
            return -1;
        }
        _capacity = capacity;
        return 0;
    }

    // Push an item into the queue.
    // Returns true on pushed.
    // May run in parallel with steal().
    // Never run in parallel with pop() or another push().
    // 往bottom侧添加元素
    bool push(const T& x) {
        const size_t b = _bottom.load(butil::memory_order_relaxed);
        const size_t t = _top.load(butil::memory_order_acquire);
        if (b >= t + _capacity) { // Full queue.
            return false;
        }
        _buffer[b & (_capacity - 1)] = x;
        _bottom.store(b + 1, butil::memory_order_release);
        return true;
    }

    // Pop an item from the queue.
    // Returns true on popped and the item is written to `val'.
    // May run in parallel with steal().
    // Never run in parallel with push() or another pop().
    // 从bottom侧取数据
    bool pop(T* val) {
        const size_t b = _bottom.load(butil::memory_order_relaxed);
        size_t t = _top.load(butil::memory_order_relaxed);
        if (t >= b) {
            // fast check since we call pop() in each sched.
            // Stale _top which is smaller should not enter this branch.
            return false;
        }
        // 因为会和从top侧取的steal同时运行，核心思想是先把bottom减1，锁定掉一个元素，
        // 防止被steal取，在只有一个元素的时候会和steal竞争。而且很重要的一点，这个
        // bottom的减1一定要被steal及时感知到，否则就会出现一个元素多次拿到的问题
        const size_t newb = b - 1;
        _bottom.store(newb, butil::memory_order_relaxed);
        // 保证数据同步的关键，这行代码在x86-64cpu上对应的典型指令是MFENCE，可以保证
        // MFENCE之后的指令在执行之前MFENCE前面的修改全局可见，也就可以保证t赋值完成后
        // bottom的store已经全局可见了
        butil::atomic_thread_fence(butil::memory_order_seq_cst);
        t = _top.load(butil::memory_order_relaxed);
        if (t > newb) { // 判断是否有可取元素
            _bottom.store(b, butil::memory_order_relaxed);
            return false;
        }
        *val = _buffer[newb & (_capacity - 1)];
        if (t != newb) {
            return true;
        }
        // Single last element, compete with steal()
        const bool popped = _top.compare_exchange_strong(
            t, t + 1, butil::memory_order_seq_cst, butil::memory_order_relaxed);
        _bottom.store(b, butil::memory_order_relaxed);
        return popped;
    }

    // Steal one item from the queue.
    // Returns true on stolen.
    // May run in parallel with push() pop() or another steal().
    // steal()会与push()/pop()或者其他的steal()并发
    bool steal(T* val) {
        size_t t = _top.load(butil::memory_order_acquire);
        size_t b = _bottom.load(butil::memory_order_acquire);
        if (t >= b) {
            // Permit false negative for performance considerations.
            return false;
        }
        do {
            // 戈神举过一个相关例子，前面文章也有人问到了这个，这里阐述下，假设top=1，
            // bottom=3，pop先置newb=2，bottom=newb=2，然后读取了一个t=top=1，随后有两个
            // steal争抢，其中一个成功了，此时top=2，失败的继续循环，如果因为数据同步的
            // 原因此时没看到pop置的bottom=2，也就是没有全局可见，那么steal线程里仍然是
            // b=bottom=3，这次cas能成功置top=3，因为pop线程是原来读取的t，t!=newb成立也
            // 成功，导致一个元素返回了两次。
            // 而一旦pop函数里有了atomic_thread_fence(butil::memory_order_seq_cst)，在
            // x86 - 64的实际实现上通常是插入mfence指令，这个指令会让前面的store全局可见，
            // 这样一来，无论那两个steal的第一次cas循环读到的是新的bottom还是老的bottom，
            // 失败后再循环的那一个第二次读到的肯定是新的bottom，随即因为t >= b失败，
            // 这样就只有pop返回了。如果atomic_thread_fence(butil::memory_order_seq_cst)
            // 是用mfence实现的，这个场景的steal里的butil::atomic_thread_fence(butil::memory_order_seq_cst)是可以不需要的，看戈神在那个issue里的回复是
            // 担心实现的不确定性，以及为了明确所以也写了
            butil::atomic_thread_fence(butil::memory_order_seq_cst);    // 为了保证取元素竞争情况下的正确性
            b = _bottom.load(butil::memory_order_acquire);
            if (t >= b) {
                return false;
            }
            *val = _buffer[t & (_capacity - 1)];
        } while (!_top.compare_exchange_strong(t, t + 1,
                                               butil::memory_order_seq_cst,
                                               butil::memory_order_relaxed));
        return true;
    }

    size_t volatile_size() const {
        const size_t b = _bottom.load(butil::memory_order_relaxed);
        const size_t t = _top.load(butil::memory_order_relaxed);
        return (b <= t ? 0 : (b - t));
    }

    size_t capacity() const { return _capacity; }

private:
    // Copying a concurrent structure makes no sense.
    DISALLOW_COPY_AND_ASSIGN(WorkStealingQueue);

    butil::atomic<size_t> _bottom;
    size_t _capacity;
    T* _buffer;
    butil::atomic<size_t> BAIDU_CACHELINE_ALIGNMENT _top;
};

}  // namespace bthread

#endif  // BTHREAD_WORK_STEALING_QUEUE_H
