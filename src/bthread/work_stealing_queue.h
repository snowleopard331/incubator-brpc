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
        // ��ʼ������ҪΪ2����������
        if (capacity & (capacity - 1)) {
            LOG(ERROR) << "Invalid capacity=" << capacity
                       << " which must be power of 2";
            return -1;
        }
        // ��ʼ��ģ�����͵�����
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
    // ��bottom�����Ԫ��
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
    // ��bottom��ȡ����
    bool pop(T* val) {
        const size_t b = _bottom.load(butil::memory_order_relaxed);
        size_t t = _top.load(butil::memory_order_relaxed);
        if (t >= b) {
            // fast check since we call pop() in each sched.
            // Stale _top which is smaller should not enter this branch.
            return false;
        }
        // ��Ϊ��ʹ�top��ȡ��stealͬʱ���У�����˼�����Ȱ�bottom��1��������һ��Ԫ�أ�
        // ��ֹ��stealȡ����ֻ��һ��Ԫ�ص�ʱ����steal���������Һ���Ҫ��һ�㣬���
        // bottom�ļ�1һ��Ҫ��steal��ʱ��֪��������ͻ����һ��Ԫ�ض���õ�������
        const size_t newb = b - 1;
        _bottom.store(newb, butil::memory_order_relaxed);
        // ��֤����ͬ���Ĺؼ������д�����x86-64cpu�϶�Ӧ�ĵ���ָ����MFENCE�����Ա�֤
        // MFENCE֮���ָ����ִ��֮ǰMFENCEǰ����޸�ȫ�ֿɼ���Ҳ�Ϳ��Ա�֤t��ֵ��ɺ�
        // bottom��store�Ѿ�ȫ�ֿɼ���
        butil::atomic_thread_fence(butil::memory_order_seq_cst);
        t = _top.load(butil::memory_order_relaxed);
        if (t > newb) { // �ж��Ƿ��п�ȡԪ��
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
    // steal()����push()/pop()����������steal()����
    bool steal(T* val) {
        size_t t = _top.load(butil::memory_order_acquire);
        size_t b = _bottom.load(butil::memory_order_acquire);
        if (t >= b) {
            // Permit false negative for performance considerations.
            return false;
        }
        do {
            // ����ٹ�һ��������ӣ�ǰ������Ҳ�����ʵ����������������£�����top=1��
            // bottom=3��pop����newb=2��bottom=newb=2��Ȼ���ȡ��һ��t=top=1�����������
            // steal����������һ���ɹ��ˣ���ʱtop=2��ʧ�ܵļ���ѭ���������Ϊ����ͬ����
            // ԭ���ʱû����pop�õ�bottom=2��Ҳ����û��ȫ�ֿɼ�����ôsteal�߳�����Ȼ��
            // b=bottom=3�����cas�ܳɹ���top=3����Ϊpop�߳���ԭ����ȡ��t��t!=newb����Ҳ
            // �ɹ�������һ��Ԫ�ط��������Ρ�
            // ��һ��pop����������atomic_thread_fence(butil::memory_order_seq_cst)����
            // x86 - 64��ʵ��ʵ����ͨ���ǲ���mfenceָ����ָ�����ǰ���storeȫ�ֿɼ���
            // ����һ��������������steal�ĵ�һ��casѭ�����������µ�bottom�����ϵ�bottom��
            // ʧ�ܺ���ѭ������һ���ڶ��ζ����Ŀ϶����µ�bottom���漴��Ϊt >= bʧ�ܣ�
            // ������ֻ��pop�����ˡ����atomic_thread_fence(butil::memory_order_seq_cst)
            // ����mfenceʵ�ֵģ����������steal���butil::atomic_thread_fence(butil::memory_order_seq_cst)�ǿ��Բ���Ҫ�ģ����������Ǹ�issue��Ļظ���
            // ����ʵ�ֵĲ�ȷ���ԣ��Լ�Ϊ����ȷ����Ҳд��
            butil::atomic_thread_fence(butil::memory_order_seq_cst);    // Ϊ�˱�֤ȡԪ�ؾ�������µ���ȷ��
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
