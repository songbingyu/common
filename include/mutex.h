// Copyright (c) 2014, Baidu.com, Inc. All Rights Reserved
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Author: yanshiguang02@baidu.com

#ifndef  BAIDU_COMMON_LOCK_MUTEX_H_
#define  BAIDU_COMMON_LOCK_MUTEX_H_

#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include <boost/noncopyable.hpp>
#include "timer.h"

namespace baidu {
namespace common {

// #define MUTEX_DEBUG

static void PthreadCall(const char* label, int result) {
    if (result != 0) {
        fprintf(stderr, "pthread %s: %s\n", label, strerror(result));
        abort();
    }
}

// A Mutex represents an exclusive lock.
class Mutex : boost::noncopyable {
public:
    Mutex()
        : owner_(0), msg_(NULL), msg_threshold_(0), lock_time_(0) {
        pthread_mutexattr_t attr;
        PthreadCall("init mutexattr", pthread_mutexattr_init(&attr));
        PthreadCall("set mutexattr", pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
        PthreadCall("init mutex", pthread_mutex_init(&mu_, &attr));
        PthreadCall("destroy mutexattr", pthread_mutexattr_destroy(&attr));
    }
    ~Mutex() {
        PthreadCall("destroy mutex", pthread_mutex_destroy(&mu_));
    }
    // Lock the mutex.
    // Will deadlock if the mutex is already locked by this thread.
    void Lock(const char* msg = NULL, int64_t msg_threshold = 5000) {
        #ifdef MUTEX_DEBUG
        int64_t s = (msg) ? timer::get_micros() : 0;
        #endif
        PthreadCall("mutex lock", pthread_mutex_lock(&mu_));
        AfterLock(msg, msg_threshold);
        #ifdef MUTEX_DEBUG
        if (msg && lock_time_ - s > msg_threshold) {
            char buf[32];
            common::timer::now_time_str(buf, sizeof(buf));
            printf("%s [Mutex] %s wait lock %.3f ms\n", buf, msg, (lock_time_ -s) / 1000.0);
        }
        #endif
    }
    // Unlock the mutex.
    void Unlock() {
        BeforeUnlock();
        PthreadCall("mutex unlock", pthread_mutex_unlock(&mu_));
    }
    // Crash if this thread does not hold this mutex.
    void AssertHeld() {
        if (0 == pthread_equal(owner_, pthread_self())) {
            abort();
        }
    }
private:
    void AfterLock(const char* msg, int64_t msg_threshold) {
        #ifdef MUTEX_DEBUG
        msg_ = msg;
        msg_threshold_ = msg_threshold;
        if (msg_) {
            lock_time_ = timer::get_micros();
        }
        #endif
        (void)msg;
        (void)msg_threshold;
        owner_ = pthread_self();
    }
    void BeforeUnlock() {
        #ifdef MUTEX_DEBUG
        if (msg_ && timer::get_micros() - lock_time_ > msg_threshold_) {
            char buf[32];
            common::timer::now_time_str(buf, sizeof(buf));
            printf("%s [Mutex] %s locked %.3f ms\n", 
                   buf, msg_, (timer::get_micros() - lock_time_) / 1000.0);
        }
        msg_ = NULL;
        #endif
        owner_ = 0;
    }
private:
    friend class CondVar;
    pthread_mutex_t mu_;
    pthread_t owner_;
    const char* msg_;
    int64_t msg_threshold_;
    int64_t lock_time_;
};

// Mutex lock guard
class MutexLock : boost::noncopyable {
public:
    explicit MutexLock(Mutex *mu, const char* msg = NULL, int64_t msg_threshold = 5000)
      : mu_(mu) {
        mu_->Lock(msg, msg_threshold);
    }
    ~MutexLock() {
        mu_->Unlock();
    }
private:
    Mutex *const mu_;
};

// Conditional variable
class CondVar : boost::noncopyable {
public:
    explicit CondVar(Mutex* mu) : mu_(mu) {
        PthreadCall("init condvar", pthread_cond_init(&cond_, NULL));
    }
    ~CondVar() {
        PthreadCall("destroy condvar", pthread_cond_destroy(&cond_));
    }
    void Wait(const char* msg = NULL) {
        int64_t msg_threshold = mu_->msg_threshold_;
        mu_->BeforeUnlock();
        PthreadCall("condvar wait", pthread_cond_wait(&cond_, &mu_->mu_));
        mu_->AfterLock(msg, msg_threshold);
    }
    // Time wait in ms, return true iff signalled
    bool TimeWait(int timeout, const char* msg = NULL) {
        timespec ts;
        struct timeval tv;
        gettimeofday(&tv, NULL);
        int64_t usec = tv.tv_usec + timeout * 1000LL;
        ts.tv_sec = tv.tv_sec + usec / 1000000;
        ts.tv_nsec = (usec % 1000000) * 1000;
        int64_t msg_threshold = mu_->msg_threshold_;
        mu_->BeforeUnlock();
        int ret = pthread_cond_timedwait(&cond_, &mu_->mu_, &ts);
        mu_->AfterLock(msg, msg_threshold);
        return (ret == 0);
    }
    void Signal() {
        PthreadCall("signal", pthread_cond_signal(&cond_));
    }
    void Broadcast() {
        PthreadCall("broadcast", pthread_cond_broadcast(&cond_));
    }
private:
    Mutex* mu_;
    pthread_cond_t cond_;
};

} // namespace common

using common::Mutex;
using common::MutexLock;
using common::CondVar;

} // namespace baidu

#endif  // COMMON_LOCK_MUTEX_H_

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
