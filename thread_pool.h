// thread_pool.h
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <bits/stdc++.h>
using namespace std;

// 任务队列
template <class T>
class SafeQueue{
private:
    queue<T> m_queue; //利用模板函数构造队列，参数类型是函数
    mutex m_mutex; // 访问互斥信号量

public:
    SafeQueue() {}
    SafeQueue(SafeQueue &&other) {}
    ~SafeQueue() {}

    bool queue_empty(); // 判断队列是否为空
    int queue_size(); //返回队列的长度
    void enqueue(T &t); //向队列中添加任务函数
    bool dequeue(T &t); //从队列中取出任务
};


// 线程池
class ThreadPool{
private:
    // 内置工作线程类
    class ThreadWorker{
    private:
        int m_id; //工作id
        ThreadPool *m_pool; // 所属线程池
    public:
        // 构造函数
        ThreadWorker(ThreadPool *pool, const int id) : m_pool(pool), m_id(id) {}
        void operator()(); //重载()操作
    };

    bool m_shutdown; // 线程池是否关闭
    SafeQueue<function<void()>> m_queue; //因为已经封装好了任务函数为void()
    vector<thread> m_threads; //工作线程队列
    mutex m_conditional_mutex; // 线程休眠互斥变量
    condition_variable m_conditional_lock; // 条件变量，让线程处于休眠或者唤醒

public:
    // 构造函数，默认线程数为4，默认初始化vector为4个thread，默认线程池关闭
    ThreadPool(const int n_threads = 4) : m_threads(vector<thread>(n_threads)), m_shutdown(false) {}
    ThreadPool(const ThreadPool &) = delete; // 将复制构造函数设为删除，不能通过复制进行拷贝构造
    ThreadPool(ThreadPool &&) = delete; // 将移动构造函数设置为删除
    ThreadPool &operator=(const ThreadPool &) = delete; // 将复制赋值运算符设为删除
    ThreadPool &operator=(ThreadPool &&) = delete; //将移动赋值运算符设为删除

    void init(); //创建线程池
    void shutdown(); // 关闭线程池
    // 提交函数
    template <typename F, typename... Args>
    auto submit(F &&f, Args &&...args) -> std::future<decltype(f(args...))>;
};

#endif
