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

#ifndef BTHREAD_SYS_FUTEX_H
#define BTHREAD_SYS_FUTEX_H

#include "butil/build_config.h"         // OS_MACOSX
#include <unistd.h>                     // syscall
#include <time.h>                       // timespec
#if defined(OS_LINUX)
#include <syscall.h>                    // SYS_futex
#include <linux/futex.h>                // FUTEX_WAIT, FUTEX_WAKE

namespace bthread {

#ifndef FUTEX_PRIVATE_FLAG
#define FUTEX_PRIVATE_FLAG 128
#endif

inline int futex_wait_private(
    void* addr1, int expected, const timespec* timeout) {
    return syscall(SYS_futex, addr1, (FUTEX_WAIT | FUTEX_PRIVATE_FLAG),
                   expected, timeout, NULL, 0);
}

inline int futex_wake_private(void* addr1, int nwake) {
    /*
        这里之所以通过syscall()传参，而不是直接调用的方式，来调用它。是因为
        SYS_futex没有被glibc export成库函数。我们通常使用的fork()、open()、
        write()等函数虽然也被称为系统调用，但其实是glibc把系统调用给export
        出来的封装函数

        通常说的futex，它是一种用户态和内核态混合的同步机制，可以简单理解为
        是一种效率较高的同步机制。pthread的很多API大多基于futex实现

        futex系统调用的API声明如下：
        int futex(int *uaddr, int op, int val, const struct timespec *timeout,
                 int *uaddr2, int val3);
        参数解析：
            1. uaddr指针指向一个整型，存储一个整数。
            2. op表示要执行的操作类型，比如唤醒(FUTEX_WAKE)、等待(FUTEX_WAIT)
            3. val表示一个值，注意：对于不同的op类型，val语义不同。
                1) 对于等待操作：如果uaddr存储的整型与val相同则继续休眠等待。
                    等待时间就是timeout参数。
                2) 对于唤醒操作：val表示，最多唤醒val 个阻塞等待uaddr上的“消费者”
                    （之前对同一个uaddr调用过FUTEX_WAIT，姑且称之为消费者，其实在
                    brpc语境中，就是阻塞的worker）。
            4. timeout表示超时时间，仅对op类型为等待时有用。就是休眠等待的最长时间。在
            5. uaddr2和val3可以忽略。
        返回值解析：
            1. 对于等待操作：成功返回0，失败返回-1
            2. 对于唤醒操作：成功返回唤醒的之前阻塞在futex上的“消费者”个数。失败返回-1

        所以futex_wake_private()里面的syscall()等价于：
        futex(&_pending_signal, (FUTEX_WAKE|FUTEX_PRIVATE_FLAG), num_task, NULL, NULL, 0);
        FUTEX_WAKE是唤醒操作，FUTEX_PRIVATE_FLAG是一个标记，表示不和其他进程共享，
        可以减少开销。由于是唤醒操作，在brpc语境下，其返回值就是阻塞的worker个数。
        它的返回值会一路透传给futex_wake_private()以及PL的signal()函数
    */
    return syscall(SYS_futex, addr1, (FUTEX_WAKE | FUTEX_PRIVATE_FLAG),
                   nwake, NULL, NULL, 0);
}

inline int futex_requeue_private(void* addr1, int nwake, void* addr2) {
    return syscall(SYS_futex, addr1, (FUTEX_REQUEUE | FUTEX_PRIVATE_FLAG),
                   nwake, NULL, addr2, 0);
}

}  // namespace bthread

#elif defined(OS_MACOSX)

namespace bthread {

int futex_wait_private(void* addr1, int expected, const timespec* timeout);

int futex_wake_private(void* addr1, int nwake);

int futex_requeue_private(void* addr1, int nwake, void* addr2);

}  // namespace bthread

#else
#error "Unsupported OS"
#endif

#endif // BTHREAD_SYS_FUTEX_H
