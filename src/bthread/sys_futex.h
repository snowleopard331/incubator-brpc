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
        ����֮����ͨ��syscall()���Σ�������ֱ�ӵ��õķ�ʽ����������������Ϊ
        SYS_futexû�б�glibc export�ɿ⺯��������ͨ��ʹ�õ�fork()��open()��
        write()�Ⱥ�����ȻҲ����Ϊϵͳ���ã�����ʵ��glibc��ϵͳ���ø�export
        �����ķ�װ����

        ͨ��˵��futex������һ���û�̬���ں�̬��ϵ�ͬ�����ƣ����Լ����Ϊ
        ��һ��Ч�ʽϸߵ�ͬ�����ơ�pthread�ĺܶ�API������futexʵ��

        futexϵͳ���õ�API�������£�
        int futex(int *uaddr, int op, int val, const struct timespec *timeout,
                 int *uaddr2, int val3);
        ����������
            1. uaddrָ��ָ��һ�����ͣ��洢һ��������
            2. op��ʾҪִ�еĲ������ͣ����绽��(FUTEX_WAKE)���ȴ�(FUTEX_WAIT)
            3. val��ʾһ��ֵ��ע�⣺���ڲ�ͬ��op���ͣ�val���岻ͬ��
                1) ���ڵȴ����������uaddr�洢��������val��ͬ��������ߵȴ���
                    �ȴ�ʱ�����timeout������
                2) ���ڻ��Ѳ�����val��ʾ����໽��val �������ȴ�uaddr�ϵġ������ߡ�
                    ��֮ǰ��ͬһ��uaddr���ù�FUTEX_WAIT�����ҳ�֮Ϊ�����ߣ���ʵ��
                    brpc�ﾳ�У�����������worker����
            4. timeout��ʾ��ʱʱ�䣬����op����Ϊ�ȴ�ʱ���á��������ߵȴ����ʱ�䡣��
            5. uaddr2��val3���Ժ��ԡ�
        ����ֵ������
            1. ���ڵȴ��������ɹ�����0��ʧ�ܷ���-1
            2. ���ڻ��Ѳ������ɹ����ػ��ѵ�֮ǰ������futex�ϵġ������ߡ�������ʧ�ܷ���-1

        ����futex_wake_private()�����syscall()�ȼ��ڣ�
        futex(&_pending_signal, (FUTEX_WAKE|FUTEX_PRIVATE_FLAG), num_task, NULL, NULL, 0);
        FUTEX_WAKE�ǻ��Ѳ�����FUTEX_PRIVATE_FLAG��һ����ǣ���ʾ�����������̹���
        ���Լ��ٿ����������ǻ��Ѳ�������brpc�ﾳ�£��䷵��ֵ����������worker������
        ���ķ���ֵ��һ·͸����futex_wake_private()�Լ�PL��signal()����
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
