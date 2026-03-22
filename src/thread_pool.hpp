/// @file thread_pool.hpp
/// @brief Lightweight persistent thread pool with a blocking parallel-for.

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

/// Persistent thread pool that distributes work items across hardware threads.
/// All calls to run() must come from a single thread (the "main" thread).
///
/// Uses a monotonically increasing epoch counter instead of a boolean flag so
/// that back-to-back run() calls cannot deadlock workers waiting for an
/// acknowledgement phase.
class ThreadPool {
public:
  explicit ThreadPool(int numThreads = 0) {
    if (numThreads <= 0)
      numThreads = static_cast<int>(std::thread::hardware_concurrency());
    numThreads = std::max(1, numThreads);
    m_workerCount = numThreads - 1; // main thread participates as a worker
    m_workers.reserve(static_cast<std::size_t>(m_workerCount));
    for (int i = 0; i < m_workerCount; ++i)
      m_workers.emplace_back(&ThreadPool::workerLoop, this);
  }

  ~ThreadPool() {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_stop = true;
    }
    m_cv.notify_all();
    for (auto &w : m_workers)
      w.join();
  }

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  int threadCount() const { return m_workerCount + 1; }

  /// Execute body(i) for i in [0, count), distributed across all threads.
  /// Blocks until every item is processed and all workers are idle.
  void run(int count, const std::function<void(int)> &body) {
    if (count <= 0)
      return;
    if (m_workerCount == 0) {
      for (int i = 0; i < count; ++i)
        body(i);
      return;
    }

    // Prepare the new batch.  These stores are sequenced before the mutex
    // lock below, which provides the release that workers will acquire.
    m_body = &body;
    m_itemCount = count;
    m_nextItem.store(0, std::memory_order_relaxed);
    m_finishedWorkers.store(0, std::memory_order_relaxed);

    // Advance the epoch under the mutex so that no worker can miss the
    // notification (the predicate check inside cv::wait is serialised by
    // the same mutex).
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      ++m_epoch;
    }
    m_cv.notify_all();

    // The calling thread participates in work-stealing too.
    drainItems();

    // Block until every worker has finished this batch.
    {
      std::unique_lock<std::mutex> lock(m_finishMutex);
      m_finishCv.wait(lock, [this] {
        return m_finishedWorkers.load(std::memory_order_acquire) >= m_workerCount;
      });
    }
  }

private:
  void drainItems() {
    while (true) {
      const int i = m_nextItem.fetch_add(1, std::memory_order_relaxed);
      if (i >= m_itemCount)
        return;
      (*m_body)(i);
    }
  }

  void workerLoop() {
    int myEpoch = 0;
    for (;;) {
      // Wait until the epoch advances (new batch available) or shutdown.
      {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this, &myEpoch] {
          return m_stop || m_epoch > myEpoch;
        });
        if (m_stop)
          return;
        myEpoch = m_epoch;
      }

      // Process items via work-stealing.
      drainItems();

      // Signal completion — the last worker to finish notifies the main thread.
      const int done =
          m_finishedWorkers.fetch_add(1, std::memory_order_acq_rel) + 1;
      if (done >= m_workerCount) {
        std::lock_guard<std::mutex> lock(m_finishMutex);
        m_finishCv.notify_one();
      }
    }
  }

  int m_workerCount = 0;
  std::vector<std::thread> m_workers;

  // Epoch-based wake signal (protected by m_mutex for condition-variable use).
  std::mutex m_mutex;
  std::condition_variable m_cv;
  int m_epoch = 0;
  bool m_stop = false;

  // Work description (written by main thread before advancing the epoch).
  const std::function<void(int)> *m_body = nullptr;
  int m_itemCount = 0;
  std::atomic<int> m_nextItem{0};

  // Completion tracking.
  std::mutex m_finishMutex;
  std::condition_variable m_finishCv;
  std::atomic<int> m_finishedWorkers{0};
};
